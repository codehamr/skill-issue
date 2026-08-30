// =============================================================================
// render/shaders.inc  —  every GLSL source string (GLSL_ENV, WORLD_FS, SKY_FS,
// BLUR/GLARE/HUD). Proof: the shot-* battery + the shimmer rig (CLAUDE.md).
// One TU: included once by game.c, in definition order — moving a definition
// out is a semantic change.
// Contract: constants crossing the C/GLSL boundary (WORLD_GROUND_R) are
// stringized here, never re-typed; per-pixel terms carry their own gate
// argument AT the gate. GLSL_ENV is one backslash-continued macro — no tool
// that edits line-wise may touch it blindly.
// =============================================================================
// ---------------------------------------------------------------------------
// Renderer — GL 3.3 core. World: ONE static VBO (solids + a 32x32 baked ground +
// decor) shaded PBR, plus a shadow map, the sky and aerial perspective. The per-frame
// scene batch, the typeface and the HUD batch each have their own banner below.
// ---------------------------------------------------------------------------

// The sky model, the tonemap and the light rig live in ONE shared snippet because three
// consumers must agree to the last digit: the sky pass, the world's aerial perspective
// (distant geometry fades into the sky actually BEHIND it, not a hand-matched constant)
// and the environment reflection that gives metal its sheen. Everything here is LINEAR
// radiance; `tonemap` is the only place that converts to display values.
//
// ONE copy of the sun: sun_set in C derives the direction, the extinguished sun
// colour, the three sky colours, the exposure and the shadow box from a single
// drawn elevation, and rig_upload hands that one struct to BOTH shader programs.
// It used to be three #defines stringized into the GLSL with C re-typing them for
// the shadow projection — four readers of a constant that could only agree by hand,
// and the reason this comment used to warn about shadows cast from a sun the shader
// no longer had. Now nothing can be re-typed, because nothing is typed twice.
#define GLSL_NUM2(v) #v
#define GLSL_NUM(v)  GLSL_NUM2(v)
// The floor's baseline roughness, and it lives up here because the FRAGMENT SHADER
// needs it too: mesh_ground_vert writes looseness into the roughness channel as a
// signed offset from this value, and the ripple field reads it back to decide where
// loose sand exists at all.
#define WORLD_GROUND_R 0.86f   // dry dusty floor: a wide, weak sheen at grazing
#define GLSL_ENV \
  /* Smooth value noise, SHARED. It lives here rather than in WORLD_FS because
     the sky pass needs the same function for its cloud layer, and two shaders
     each carrying their own copy of a noise basis is two chances for the ground
     and the sky to disagree about what a metre of noise is. */ \
  /* NO SINE. The fract(sin(dot(...))*k) idiom is one line and it is also, on a
     software rasteriser, a transcendental per lookup — and every noise here
     costs four of them. Measured on llvmpipe: the cloud deck alone evaluates
     twenty-four of those per sky pixel and the glitter field six per ground
     pixel, which was 0.87 and 0.61 ms/frame respectively. The fract-multiply
     construction is pure ALU, has better distribution than sin at large
     coordinates (where sin's argument reduction runs out of mantissa and the
     "random" numbers start repeating), and cost 1.1 ms/frame less. */ \
  "float vhash2(vec2 p){\n" \
  "  vec3 q = fract(vec3(p.xyx) * 0.1031);\n" \
  "  q += dot(q, q.yzx + 33.33);\n" \
  "  return fract((q.x + q.y) * q.z);\n" \
  "}\n" \
  "vec3 vhash3(vec2 p){\n" \
  "  vec3 q = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));\n" \
  "  q += dot(q, q.yxz + 33.33);\n" \
  "  return fract((q.xxy + q.yzz) * q.zyx);\n" \
  "}\n" \
  "float vnoise2(vec2 p){\n" \
  "  vec2 i = floor(p), f = fract(p);\n" \
  "  f = f * f * (3.0 - 2.0 * f);\n" \
  "  return mix(mix(vhash2(i), vhash2(i + vec2(1.0, 0.0)), f.x),\n" \
  "             mix(vhash2(i + vec2(0.0, 1.0)), vhash2(i + vec2(1.0, 1.0)), f.x), f.y);\n" \
  "}\n" \
  /* THE LIGHT RIG, and every one of these is now map state rather than a
     constant. NEVER near-overhead: a sun at 62 degrees lights three
     90-degrees-apart faces of the same box to within a few percent of each
     other — no azimuthal cue anywhere in the frame, nothing for a cast shadow
     to subtract, and every material reads as overcast satin however good its
     BRDF is. sun_set draws between 6.5 and 54 degrees for exactly that reason.
     Sun up, sky down: a clear sky is not brighter than the ground it lights,
     and it only was here once because the ambient was paying for what the sun
     should have.
     EXPOSURE is here rather than baked into each light because every value
     above is a radiance in the same unit as the sky the player can see, so ONE
     number decides where mid-grey lands and the balance between sun, sky and
     reflection is never re-derived per surface. */ \
  "uniform vec3 SUN_DIR;\n" \
  "uniform vec3 SUN_COL;\n" \
  "uniform vec3 SKY_HOR;\n" \
  "uniform vec3 SKY_TOP;\n" \
  "uniform vec3 SKY_GND;\n" \
  "uniform float EXPOSURE;\n" \
  "uniform float SUN_DISC;\n" \
  /* THE AIR. rgb is the dust's tint, w how much of it there is; atmo_tint /
     atmo_haze in C are the one authority and this is their only reader. The
     medium is EXPONENTIAL IN HEIGHT — dust settles — and that single fact is
     what both halves below are derived from, which is why they cannot drift:
     the horizon band the sky draws and the aerial perspective the world fades
     into are one function evaluated along two different rays. */ \
  "uniform vec4 uAtmo;\n" \
  /* Scale height of the dust layer, in metres, as its reciprocal — the world's
     fog integral wants 1/H and the sky's slant path wants nothing else. 9 m
     over a 2.75 m wall means a wall's foot is measurably hazier than its cap,
     which is the whole reason to integrate a profile rather than a distance. */ \
  "const float FOG_INVH = 0.1111;\n" \
  /* One number turns "how hazy is the horizon" into "how thick is the air",
     so a theme tunes ONE value and both halves move together. */ \
  "const float FOG_RHO_K = 0.030;\n" \
  /* Dust forward-scatters hard, so the quarter of the sky the sun is in is
     where the haze is bright and the opposite quarter is where it is grey.
     That azimuthal split is the single strongest mood cue a clear-sky arena
     has, and without it a haze band is just a grey smear.
     SCHLICK, NOT HENYEY-GREENSTEIN, and the reason is where this runs: a
     world fragment evaluates sky_env up to three times (the environment
     reflection, the water's own reflection and the aerial perspective), so
     one sqrt in here is three per pixel. Schlick's rational fit to HG needs
     none — k = 1.55g - 0.55g^3 is the standard mapping and at g = 0.58 the
     two agree to within 4% of the forward peak, which is a tuning constant's
     worth of difference in a term that already carries one. */ \
  /* uHaze.rgb is the whole tinted ambient half — uAtmo.rgb * (SUN_COL*0.055 +
     SKY_HOR*0.30) — and uHazeS is uAtmo.rgb * SUN_COL * 1.20, both folded in C
     because both are constant for the entire draw. A GLSL compiler will not
     hoist a uniform-only expression out of a fragment shader, so leaving them
     written out meant six vector multiplies and three adds per sky_env call,
     three times per world pixel, to recompute the same two colours. */ \
  /* THE DERIVED COLOURS, FOLDED IN C. The light rig used to be `const`, so the
     GLSL compiler constant-folded every colour built out of it — the two ends
     of the hemisphere ambient, the ground bounce, the sky's own sun azimuth.
     Making the sun map state took that folding away and handed the fragment
     shader a mix, three scales and a normalize per pixel to rebuild the same
     four values: measured at 6 ms/frame on the llvmpipe proxy, i.e. more than
     the entire glare chain, for arithmetic that cannot change within a draw.
     rig_upload folds them once instead. */ \
  "uniform vec3 uHaze, uHazeS, uAmbD, uAmbU, uBounce;\n" \
  "vec3 haze_rad(float mu){\n" \
  "  float k = 0.792;\n" \
  "  float den = 1.0 - k * mu;\n" \
  "  float ph = 0.0293337 / max(den * den, 1.0e-4);\n" \
  "  return uHaze + uHazeS * ph;\n" \
  /* 0.95 on the forward peak and not 1.20: at 1.20 the quarter of the sky the
     sun is in went past the tonemap's shoulder, and with the aerial
     perspective fading everything into that same colour, a bot at twenty
     metres between the player and a low sun stopped being a silhouette and
     became milk. The lobe still runs about twenty to one against the opposite
     quarter, which is where the mood is; what came off was the part that was
     only clipping. */ \
  "}\n" \
  "vec3 sky_env(vec3 d, float disc){\n" \
  "  float t = clamp(d.y, -1.0, 1.0);\n" \
  /* ONE pow, not two. A ternary over two pow calls is flattened to a select by
     the driver, so both powers are computed on every sky and every world
     fragment (twice on a world fragment — env_gloss and the aerial term).
     abs(t) is t or -t exactly by sign, and at t == 0 both forms give 0. */ \
  "  bool up = t > 0.0;\n" \
  "  vec3 c = mix(SKY_HOR, up ? SKY_TOP : SKY_GND, pow(abs(t), up ? 0.42 : 0.45));\n" \
  "  float s = max(dot(d, SUN_DIR), 0.0);\n" \
  /* THE SUN IS 0.53 DEGREES ACROSS, and it was drawn at four times that. The
     old term was pow(s, 900), whose half-power point is 2.25 degrees off axis —
     a soft blob the size of four suns, which is why the sky read as an
     overcast lightbox with a bright patch in it rather than as a clear day.
     A hard disc plus a real aureole is the correct decomposition, and it is
     also what makes the aureole readable: the two used to be one number.
     The disc's edge is antialiased on the DERIVATIVE of the cosine rather than
     on a fixed epsilon. A fixed epsilon either loses the disc entirely or
     strobes as the pixel grid crosses its rim, because the whole disc spans
     about a hundred-thousandth of the cosine's range. */ \
  /* EVERYTHING INSIDE THIS BRANCH IS MULTIPLIED BY `disc`, and only the sky
     pass ever passes a non-zero one. The three lobes used to sit outside it —
     multiplied by disc, so arithmetically absent, but evaluated: two pow calls
     on every world fragment, three times over, for a result that was then
     scaled to nothing. The branch is uniform-valued per draw, so it costs
     nothing to take. */ \
  "  float lobes = 0.0;\n" \
  "  if (disc > 0.0) {\n" \
  "    float e = max(fwidth(s), 1.0e-7);\n" \
  "    lobes = smoothstep(0.99998917 - e, 0.99998917 + e, s) * SUN_DISC\n" \
  "          + SUN_DISC * 0.065 * pow(s, 2600.0) + 0.13 * pow(s, 110.0);\n" \
  "  }\n" \
  /* Two aureoles, not one. The tight one (~110) is the corona a clear sky has
     around a low sun; the broad one (8) is the forward-scattered haze over the
     whole quarter of the sky the sun is in. */ \
  /* THREE lobes, not two. The disc is 255 and the aureole's plateau is 184, and
     with nothing between them the transition happens in three pixels: a hard
     white cut-out pasted on a smooth sky, which reads as a hole punched in the
     backdrop rather than as a light source. The middle lobe is the GLARE — a
     degree and a third wide, which is what a camera and an eye both do with a
     source this bright. */ \
  /* THE GLARE SCALES WITH THE SOURCE, and it did not. The 1.3-degree lobe was
     a fixed 1.7 while the disc under it was being dimmed for a low sun, so a
     sunset ended up with a white glare blob exactly where the orange orb was
     supposed to be — the disc was fixed and the thing sitting on top of it was
     not. 0.065 * SUN_DISC reproduces the old 1.7 at a midday sun to two
     figures and follows it down. The BROAD lobe below stays fixed and stays
     outside the branch: it is the whole quarter of the sky forward-scattering,
     which is what a world fragment's aerial perspective has to fade into, and
     it does not stop at dusk. s^8 by three squarings — pow() here was a
     transcendental per sky_env call for an exponent that is a power of two. */ \
  "  float s2 = s * s, s4 = s2 * s2;\n" \
  "  c += SUN_COL * (lobes + 0.085 * s4 * s4);\n" \
  /* ...AND THEN THE AIR IN FRONT OF ALL OF IT. The haze goes on LAST because
     it is extinction as well as in-scatter: it has to dim the sun disc and the
     aureole the same way it dims the blue behind them, or the sky ends up with
     a hard sun sitting in soft air. The slant path is 1/sin(elevation) less the
     vertical one, which is exactly 0 at the zenith — so the top of the sky
     keeps its own colour whatever the arena's dust does, and only the bottom
     of it turns. */ \
  /* A 1/sin(elevation) slant path is the honest profile for a medium that
     reaches to infinity, and over a 48 m arena it is also far too BROAD: it
     put a third of the haze at 25 degrees up and took the blue out of the
     whole sky. The cube pulls the band down onto the horizon where the long
     low rays actually are — 86% at 8 degrees, 17% at 25, nothing at 45 — so
     the arena stands under a hazy rim and a clean sky rather than inside a
     sepia box. */ \
  "  float st = max(1.0 - t, 0.0);\n" \
  "  float sl = uAtmo.w * st * st * st / max(t, 0.06);\n" \
  /* THE BRANCH IS THE POINT, not a micro-optimization. Every ray that leaves
     the scene going UP — which is every environment reflection off a floor and
     most of the sky pass — has a slant path of nearly nothing, and paying the
     phase function for it was 25 ms/frame on the wet theme alone.
     1 - exp(-sl) is a second-order Pade rather than the exp: 0.813 against
     0.879 at the horizon, exact at both ends and monotone between, for one
     divide instead of a transcendental. The haze is a tuned quantity, so a
     3-point difference in its own knee is inside the tuning. */ \
  "  if (sl > 0.004)\n" \
  "    c = mix(c, haze_rad(s), 1.0 - 1.0 / (1.0 + sl * (1.0 + 0.5 * sl)));\n" \
  "  return c;\n" \
  "}\n" \
  /* The environment a SPECULAR surface sees, which is not the one the sky pass
     draws. Every reflecting surface in the frame used to sample sky_env at a
     fixed disc of 0.35, so a 0.86-rough concrete floor and a polished bolt
     mirrored the SAME 900-exponent pinprick sun at the same strength: the one
     cue that separates a matte metal from a bright one was absent from the only
     term that carries metal at all. Here both the lobe's width and its energy
     come from the roughness — a mirror gets the disc, a rough casting gets
     nothing but the gradient, and everything between gets a broad glossy smear
     that slides across the part as it turns, which is what "it reflects a
     little" actually looks like. The gradient half is shared with the sky pass
     BY CONSTRUCTION rather than by a matched constant: same function, disc off. */ \
  "vec3 env_gloss(vec3 d, float rough){\n" \
  "  vec3 c = sky_env(d, 0.0);\n" \
  "  float g = 1.0 - clamp(rough, 0.0, 1.0);\n" \
  "  float g2 = g * g;\n" \
  "  float s = max(dot(d, SUN_DIR), 0.0);\n" \
  "  return c + SUN_COL * (g2 * g2 * 3.2 * pow(s, mix(24.0, 760.0, g)));\n" \
  "}\n" \
  /* Filmic curve (ACES fit) then a gamma-2.0 encode — the albedo decode below
     is x*x, so an unlit surface round-trips to exactly its authored color. */ \
  /* THE HERO RIG IS THE SAME LIGHT THE ARENA IS STANDING IN, only aimed by the
     camera instead of by the sun: the fill takes its hue from the SKY and the
     rim from the SUN, so a weapon held up at dusk is lit orange from behind and
     dim violet from above like everything else in the frame. They used to be
     two fixed cool-white constants, which was invisible under a fixed midday
     sun and became the loudest thing in the picture the moment the sun could
     move — a blue-white rifle in a gold arena.
     THE MAGNITUDE FOLLOWS THE ARENA, COMPRESSED. rig_upload divides by the
     rig's own luminance so the colour is free to turn, then puts the strength
     back on a 0.7 power of how much light the arena actually has. Pinning it
     flat was the first attempt and it was visibly wrong: at a 7-degree sun the
     weapon and the sleeve came out at midday brightness in a dusk frame and
     became the brightest object in it, which is the one thing a first-person
     weapon must not be. Following it 1:1 is the other failure — a weapon as
     dark as the arena is a weapon the player cannot read — and this rig was
     always a stylisation, so it sits between the two. */ \
  "uniform vec3 VM_FILL, VM_RIM;\n" \
  /* GGX + Smith, once. Every light — sun, viewmodel fill, viewmodel rim — is the
     same call with a different direction and radiance, which is the only way
     three lights stay consistent with each other. */ \
  /* F90 IS THE OTHER END OF THE FRESNEL AND IT WAS HARDCODED TO WHITE. Schlick is
     mix(F0, F90, (1-VoH)^5); writing it as F0 + (1-F0)*x pins F90 at 1 and costs
     exactly the same instructions as mix(F0, F90, x). Making it a parameter is
     therefore FREE, and it is what a coated lens needs: an AR coating is a notch
     filter, so its grazing colour is not its normal-incidence colour. Everything that
     is not glass passes vec3(1.0) and is bit-identical. */ \
  "vec3 lit(vec3 n, vec3 V, vec3 L, vec3 diff, vec3 F0, vec3 f90, float rough, vec3 rad){\n" \
  "  float NoL = max(dot(n, L), 0.0);\n" \
  "  if (NoL <= 0.0) return vec3(0.0);\n" \
  "  float NoV = max(dot(n, V), 1e-4);\n" \
  "  vec3 H = normalize(L + V);\n" \
  "  float NoH = max(dot(n, H), 0.0), VoH = max(dot(V, H), 0.0);\n" \
  "  float a = rough * rough, a2 = a * a;\n" \
  "  float dn = NoH * NoH * (a2 - 1.0) + 1.0;\n" \
  "  float D = a2 / (3.14159265 * dn * dn);\n" \
  "  float kv = a * 0.5;\n" \
  "  float Vis = 0.25 / max((NoL*(1.0-kv)+kv) * (NoV*(1.0-kv)+kv), 1e-4);\n" \
  /* NOT pow(x, 5.0). Mesa's nir_opt_algebraic lowers fpow for exponents 1, 2, 3 and
     4 only — the exponent-5 rule matches an ALREADY-lowered exp2(log2(a)*5), and
     llvmpipe does not set lower_fpow. So `pow(x, 5.0)` here is a real
     exp2(log2_safe(x)*y), i.e. two polynomial evaluations, and THIS function is called
     three to four times per viewmodel pixel (sun, fill, rim, muzzle flash). Three muls
     instead, and the Schlick term is exact either way. */ \
  "  float fv = 1.0 - VoH, fv2 = fv * fv;\n" \
  "  vec3 F = mix(F0, f90, fv2 * fv2 * fv);\n" \
  "  return (diff + D * Vis * F) * NoL * rad;\n" \
  "}\n" \
  /* THE TOOL DECIDES WHICH WAY THE HIGHLIGHT RUNS. `lit` models a surface whose
     micro-facets are isotropic; a machined one's are not, and the half of that a STILL
     frame shows is a roughness wobble while the half that reads in MOTION is the
     stretch. This is GGX with two widths (Burley 2012 / Filament's
     D_GGX_Anisotropic), and it is `lit` EXACTLY when the two are equal — substituting
     at == ab == a gives dot(d,d) = a2*(1 + NoH^2*(a2-1)) = a2*dn, so D = a2/(pi*dn^2),
     which is the isotropic line verbatim. Arithmetic, not a claim, and it is what lets
     the world keep the cheap call.
     IT MAY ONLY SPREAD A LOBE, NEVER SHARPEN ONE. The textbook remap stretches one
     axis by (1+k) and squeezes the other by (1-k); the squeezed axis then ends up
     TIGHTER than any isotropic surface on the weapon — at k = 0.5 a 0.44 receiver's
     cross-tool lobe comes out at an effective 0.31, narrower than the barrel's
     authored 0.24 and brighter than isotropic at the same time. The micro-structure
     block writes per-pixel noise into `rough` a few lines earlier, and a 19% wobble
     through a lobe that tight is a binary in/out decision: the receiver comes out
     under a black-and-white dither. Spreading only is also energy-exact, because the
     NDF carries its own 1/(pi*at*ab): the peak dims by exactly 1/(1+k) as the streak
     lengthens by (1+k), which is what a tool mark does.
     THE STREAK RUNS PERPENDICULAR TO THE GROOVES. A surface grooved along x has
     h = h(y) alone, so its normals tilt only toward y and the lobe spreads in y. It is
     the same statement as a CD's radial glare across its circular tracks, and it is
     why a TURNED barrel — circumferential marks — throws its streak ALONG the bore. */ \
  "vec3 lit_ax(vec3 n, vec3 V, vec3 L, vec3 diff, vec3 F0, vec3 f90, float rough,\n" \
  "            vec3 T, float aniso, vec3 rad){\n" \
  "  float NoL = max(dot(n, L), 0.0);\n" \
  "  if (NoL <= 0.0) return vec3(0.0);\n" \
  "  float NoV = max(dot(n, V), 1e-4);\n" \
  "  vec3 H = normalize(L + V);\n" \
  "  float NoH = max(dot(n, H), 0.0), VoH = max(dot(V, H), 0.0);\n" \
  "  vec3 B = cross(n, T);\n" \
  "  float a = rough * rough;\n" \
  "  float at = a * (1.0 + aniso), ab = a;\n" \
  "  float a2 = at * ab;\n" \
  "  vec3 dv = vec3(ab * dot(T, H), at * dot(B, H), a2 * NoH);\n" \
  "  float b2 = a2 / max(dot(dv, dv), 1e-12);\n" \
  "  float D = a2 * b2 * b2 * (1.0 / 3.14159265);\n" \
  "  float kv = a * 0.5;\n" \
  "  float Vis = 0.25 / max((NoL*(1.0-kv)+kv) * (NoV*(1.0-kv)+kv), 1e-4);\n" \
  "  float fv = 1.0 - VoH, fv2 = fv * fv;\n" \
  "  vec3 F = mix(F0, f90, fv2 * fv2 * fv);\n" \
  "  return (diff + D * Vis * F) * NoL * rad;\n" \
  "}\n" \
  "vec3 tonemap(vec3 x){\n" \
  "  x *= EXPOSURE;\n" \
  "  x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);\n" \
  "  x = sqrt(clamp(x, 0.0, 1.0));\n" \
  /* Half an LSB of interleaved-gradient noise. A smooth sky quantised to 8 bits
     shows about eighty hard Mach bands ten pixels apart; dither turns the step
     into grain the eye integrates away, and it costs three instructions. */ \
  "  float dth = fract(52.9829189 * fract(dot(gl_FragCoord.xy,\n" \
  "                                          vec2(0.06711056, 0.00583715))));\n" \
  "  return x + (dth - 0.5) * 0.0039216;\n" \
  "}\n"

static const char *WORLD_VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "layout(location=1) in vec3 aNrm;\n"
  "layout(location=2) in vec3 aCol;\n"
  "layout(location=3) in vec3 aMat;\n"   // roughness, metalness, ambient occl.
  "uniform mat4 uVP;\n"
  "uniform mat4 uLightVP;\n"
  "uniform float uSmTexel;\n"            // shadow texel size in world metres
  "out vec3 vNrm; out vec3 vCol; out vec3 vWorld; out vec3 vMat; out vec4 vLit;\n"
  // The shadow lookup is offset ALONG THE NORMAL by a texel and a half rather than
  // biased in depth: a depth bias has to be tuned per slope and leaks light under
  // contact points at grazing angles ("peter-panning"), while a normal offset moves the
  // sample sideways out of the surface's own shadow and needs no slope term at all.
  "void main(){ vNrm=aNrm; vCol=aCol; vMat=aMat; vWorld=aPos;\n"
  "  vLit = uLightVP * vec4(aPos + normalize(aNrm) * uSmTexel * 1.7, 1.0);\n"
  "  gl_Position=uVP*vec4(aPos,1.0); }\n";

// Depth-only pass that fills the shadow map.
static const char *DEPTH_VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "uniform mat4 uVP;\n"
  "void main(){ gl_Position=uVP*vec4(aPos,1.0); }\n";
static const char *DEPTH_FS =
  "#version 330 core\n"
  "void main(){}\n";

// One metal/dielectric BRDF for everything solid: GGX specular + Smith visibility, F0
// from metalness, and — the term that actually sells metal — an environment reflection
// sampled from the same sky the player can see.
static const char *WORLD_FS =
  "#version 330 core\n"
  GLSL_ENV
  "in vec3 vNrm; in vec3 vCol; in vec3 vWorld; in vec3 vMat; in vec4 vLit;\n"
  "uniform vec3 uEye;\n"
  "uniform float uDetail;\n"
  "uniform float uEmis;\n"
  "uniform float uShadow;\n"              // 0 = no shadow map bound
  "uniform float uVm;\n"                  // 1 = viewmodel: add the hero rig
  "uniform vec3 uMuzC;\n"                 // muzzle-flash radiance (0 = no shot)
  "uniform vec3 uMuzL;\n"                 // direction from the gun to the flash
  "uniform vec3 uMuzP;\n"                 // ...and where it is, for the WORLD
  "uniform vec3 uFill, uRim;\n"
  // The weapon's OWN frame, published by vm_build once per viewmodel pass.
  "uniform vec3 uMdlO, uMdlX, uMdlZ;\n"   // no uMdlY: nothing reads an up axis
  // THE FLOOR'S OWN TREATMENT, one vec4: ripple strength, glitter strength, standing-
  // water level and the wind heading.
  "uniform vec4 uSurf;\n"
  "uniform vec2 uSmStep;\n"               // one shadow texel in UV
  "uniform sampler2DShadow uSm;\n"
  "out vec4 oCol;\n"
  // 4x4 taps of a hardware-compared shadow map: each tap is itself bilinear, so sixteen
  // of them at half-texel spacing give a penumbra about three texels wide — enough that
  // a 48 m arena in one map still reads as a soft edge instead of a staircase.
  // NO SINE HERE EITHER — the same argument GLSL_ENV's vhash2 already carries, and this
  // is the one survivor. vnoise1 calls this twice and the weapon block calls vnoise1
  // twice, so a weapon fragment was paying FOUR transcendentals. The multiply-fract
  // construction is pure ALU and distributes BETTER at the large arguments this block
  // works at (xb * 3400), where sin's argument reduction runs out of mantissa and the
  // "random" numbers start repeating.
  "float vhash1(float x){\n"
  "  float p = fract(x * 0.1031);\n"
  "  p *= p + 33.33;\n"
  "  p *= p + p;\n"
  "  return fract(p);\n"
  "}\n"
  "float vnoise1(float x){\n"
  "  float i = floor(x), f = fract(x);\n"
  "  f = f * f * (3.0 - 2.0 * f);\n"
  "  return mix(vhash1(i), vhash1(i + 1.0), f);\n"
  "}\n"
  "float sun_shadow(){\n"
  "  if (uShadow < 0.5) return 1.0;\n"
  "  vec3 p = vLit.xyz / vLit.w;\n"
  "  if (p.z >= 1.0) return 1.0;\n"
  "  p = p * 0.5 + 0.5;\n"
  "  float s = 0.0;\n"
  "  for (int y = -1; y <= 2; y++)\n"
  "    for (int x = -1; x <= 2; x++)\n"
  "      s += texture(uSm, vec3(p.xy + vec2(float(x) - 0.5, float(y) - 0.5)\n"
  "                                    * uSmStep, p.z));\n"
  "  return s * 0.0625;\n"
  "}\n"
  "void main(){\n"
  "  if (uEmis > 0.5) { oCol = vec4(vCol, 1.0); return; }\n"
  "  vec3 n = normalize(vNrm);\n"
  "  vec3 alb = vCol * vCol;\n"                 // authored display -> linear
  "  vec3 dv = vWorld - uEye;\n"
  "  float d = length(dv);\n"
  "  vec3 V = -dv / d;\n"
  // ...AND THE RANGE FADE IS NOT COMPUTED WHERE IT IS MULTIPLIED BY ZERO. `uDetail` is
  // 0 for every figure and the whole viewmodel and 1 for the two world draws, so this
  // exp ran on every weapon and every body to be scaled to nothing. The branch is
  // uniform-valued per draw, so taking it costs nothing.
  "  float det = 0.0;\n"
  "  if (uDetail > 0.0) det = exp(-d * 0.035);\n"
  // GATED ON uDetail, WHICH IS ZERO FOR EVERY FIGURE AND THE WHOLE VIEWMODEL. uDetail
  // has three writers: the sky pass sets 0, the two world draws set 1.
  "  float rough;\n"
  "  if (uDetail > 0.0) {\n"
  "  vec3 cell = floor(vWorld * 14.0);\n"       // ~7 cm grain, world-anchored
  "  float h = fract(sin(dot(cell, vec3(127.1, 311.7, 74.7))) * 43758.5453);\n"
  // Two octaves, not one: a single 7 cm grain is film grain, and what makes concrete
  // read as concrete is patchiness a hand's width across UNDER that grain.
  "  vec2 cp2 = abs(n.y) > 0.5 ? vWorld.xz\n"
  "           : (abs(n.x) > 0.5 ? vWorld.zy : vWorld.xy);\n"
  "  float h2 = vnoise2(cp2 * 1.7);\n"
  "  alb *= 1.0 + (h - 0.5) * 0.075 * det + (h2 - 0.5) * 0.085 * uDetail;\n"
  "  rough = clamp(vMat.x + (h2 - 0.5) * 0.16 * uDetail, 0.045, 1.0);\n"
  "  } else rough = clamp(vMat.x, 0.045, 1.0);\n"
  // ...and the metalness is its MAGNITUDE: the sign is the tool direction. See
  // mat_lathe. Nothing outside gun_build writes a negative, so the world is unchanged.
  "  float metal = abs(vMat.y), ao = vMat.z;\n"
  // --- WEAPON MICRO-STRUCTURE, IN THE WEAPON'S OWN FRAME --------------------
  // One roughness and one albedo per PART makes a receiver flat a single unbroken sweep
  // of highlight however good the BRDF over it is, which is most of why the rifle read
  // as one charcoal casting.
  "  vec3 gT = uMdlX;\n"
  "  float gani = 0.0;\n"
  "  if (uVm > 0.5 && metal > 0.35) {\n"
  "    vec3 q = vWorld - uMdlO;\n"
  "    float zb = dot(q, uMdlZ);\n"
  // The coordinate ACROSS the bore and IN this face's own surface is the cross product
  // of the two, which needs neither a UV set nor a triplanar blend to be right on every
  // face of every part.
  //
  // ...AND ITS GUARD IS 0.06, NOT 1e-3. |cross(uMdlZ, n)| is the sine of the angle
  // between the face and the bore, so normalizing it amplifies whatever noise the
  // interpolator left in `n` by 1/|tv|: at the old 1e-3 that is a thousandfold, i.e. a
  // per-pixel RANDOM tangent basis on every face square to the bore — every end cap,
  // the muzzle crown, the butt pad. As a noise LOOKUP, which is all this coordinate
  // used to be, that was invisible; as a TANGENT FRAME it is the loudest defect on the
  // weapon. The anisotropy fades out across the same span, so the lobe is exactly
  // isotropic where the frame has no direction.
  "    vec3 tv = cross(uMdlZ, n);\n"
  "    float tl2 = dot(tv, tv);\n"
  "    vec3 Tx = tl2 > 1.0e-6 ? tv * inversesqrt(tl2) : uMdlX;\n"
  "    float xb = tl2 > 1.0e-6 ? dot(q, Tx) : dot(q, uMdlX);\n"
  "    float bore = 1.0 - abs(dot(n, uMdlZ));\n"
  // TURNED marks run AROUND the bore, so the lobe spreads ALONG it; broached marks run
  // along it, so the lobe spreads across. Tx is already perpendicular to n, so the
  // cross of the two is unit length and needs no normalize.
  "    gT = vMat.y < 0.0 ? cross(n, Tx) : Tx;\n"
  "    gani = 0.55 * smoothstep(0.0036, 0.09, tl2);\n"
  // THE TOOL MARKS WERE UNDER NYQUIST IN NORMAL PLAY, which is worse than absent: at
  // 3400 strokes/m the wavelength is 0.29 mm and an ADS frame puts 0.35 mm in a pixel,
  // so the term was 0.85 pixels per wavelength — broadband noise, and the reason its
  // amplitude had already been "backed off from the first pass that made them visible
  // at all". It was backed off because it was aliasing. The octave now snaps to the
  // pixel footprint in power-of-two steps (the same construction the floor's glitter
  // uses, and for the same reason: a frequency that slides makes the pattern swim),
  // holding 3-9 pixels per wavelength at every framing from the ADS view to a 40 mm
  // macro.
  "    vec2 duv = vec2(dFdx(xb), dFdx(zb)), dvv = vec2(dFdy(xb), dFdy(zb));\n"
  "    float fp = sqrt(max(dot(duv, duv), dot(dvv, dvv)));\n"
  "    float oct = exp2(floor(log2(max(fp * 13600.0, 1.0))));\n"
  "    float gh = vnoise2(vec2(xb, zb) * (670.0 / max(oct * 0.25, 1.0)));\n"
  "    float br = vnoise1(xb * (3400.0 / oct)) - 0.5;\n"
  "    float la = (vnoise1(zb * (2100.0 / oct)) - 0.5) * (1.0 - bore * 0.55);\n"
  // Amplitudes: the mottle is multiplicative because the grooves are cut INTO the
  // coating, the tooling is additive because it is cut through it.
  "    rough = clamp(rough * (0.90 + 0.19 * gh)\n"
  "                  + br * 0.055 * bore + la * 0.048, 0.035, 1.0);\n"
  "    alb *= 1.0 + (gh - 0.5) * 0.085 + br * 0.040 * bore;\n"
  "  }\n"
  // Geometric specular antialiasing.
  "  if (uDetail > 0.0 && abs(n.y) < 0.5) {\n"  // vertical faces: panel seams
  "    float sc = abs(n.x) > 0.5 ? vWorld.z : vWorld.x;\n"
  "    float sf = abs(fract(sc / 3.0) - 0.5) * 3.0;\n"
  "    float sm = 1.0 - smoothstep(0.02, 0.05, sf);\n"
  "    alb *= 1.0 - 0.075 * det * sm;\n"
  "    rough = mix(rough, 1.0, 0.5 * det * sm);\n"
  "  }\n"
  // --- THE FLOOR ----------------------------------------------------------
  // Half the screen is this surface, and it used to be a 2 m checker plus an 8 m seam
  // grid: a tiled hall on an outdoor arena, carrying less information than a wall.
  "  float wet = 0.0, glint = 0.0;\n"
  "  if (n.y > 0.95 && vWorld.y < 0.05) {\n"
  "    vec2 P = vWorld.xz;\n"
  // ONE sqrt, not two: sqrt is monotone and correctly rounded, so the max of the
  // lengths is the sqrt of the max of the squared lengths, bit for bit.
  "    vec2 dpx = dFdx(P), dpy = dFdy(P);\n"
  "    float fp = sqrt(max(dot(dpx, dpx), dot(dpy, dpy)));\n"
  // GATED ON THE THEME. surf_params returns uSurf.x == 0 for GRIT and WET, and
  // then nv/loose/rv/lam/vis feed only the else branch below, which is
  // `clamp(rough + 0.0, ...)` on a rough already inside that range — a literal
  // no-op costing two vnoise2 and three smoothsteps per floor pixel.
  "    if (uSurf.x > 0.0) {\n"
  "    float nv = max(dot(n, V), 0.0);\n"
  // --- AEOLIAN RIPPLES ----------------------------------------------------
  // NOT fbm: isotropic noise on a floor reads as dirt or as nothing.
  "    float loose = clamp((vMat.x - " GLSL_NUM(WORLD_GROUND_R) ") * 8.0 + 0.75,\n"
  "                        0.0, 1.0)\n"
  "               * smoothstep(0.14, 0.52, vnoise2(P * 0.13));\n"
  // The two fades are computed BEFORE the field they fade, so a pixel whose ripples
  // would be filtered away to nothing does not pay to compute them — past about fifteen
  // metres, and at every grazing angle, that is most of the floor in frame.
  "    float rv = vnoise2(P * 0.105);\n"
  "    float lam = 0.125 * (0.78 + 0.44 * rv);\n"
  // THE FOOTPRINT FADE IS A LEGIBILITY DECISION, NOT AN ALIASING FIX, and the
  // instrument says so. At lam * 1.8 the field is still at full strength when
  // the pixel footprint is one and a half TIMES the wavelength — measured on
  // the overview camera, fp = 85 mm against a 125 mm ripple, 1.5 samples a
  // cycle — and once a low sun started raking across it the whole floor read
  // as corduroy rather than as sand. The natural conclusion is "that is
  // aliasing", and it is WRONG: the shimmer rig (1x against 4x supersampled
  // and downsampled, over the floor region) reads 2.901 before and 2.881
  // after, i.e. unchanged, on frames whose pixels differ over a third of the
  // time. The supersampled reference shows the same lattice, because at this
  // scale the pattern is resolved and simply reads as fabric.
  // So: lam * 0.5 is the Nyquist limit and lam * 0.16 is about six samples a
  // cycle, and the fade is pulled back to them because a ripple you cannot
  // read as relief should not be drawn as one — not because it was flickering.
  // It costs the ripples past ~8 m at eye height, which is about where real
  // sand stops showing them, and everything the fade takes is handed to
  // ROUGHNESS by the branch below.
  "    float vis = (1.0 - smoothstep(lam * 0.16, lam * 0.5, fp))\n"
  "              * smoothstep(0.10, 0.42, nv) * loose;\n"
  "    if (vis > 0.004) {\n"
  // The heading barely moves, and that is the correction.
  "      float ang = uSurf.w + 0.16 * (vnoise2(P * 0.028) - 0.5);\n"
  "      vec2 wd = vec2(cos(ang), sin(ang));\n"
  // The warp has to be TWO octaves and weak.
  "      float u = dot(P, wd) / lam\n"
  "             + 0.055 * vnoise2(P * 0.31) + 0.030 * vnoise2(P * 3.10);\n"
  "      float sq = fract(u);\n"
  // Asymmetric, and the lee face is 0.34 rather than the measured 0.28: at 0.28
  // the crest is a razor line one pixel wide that the normal cannot filter, and
  // a ripple crest is a rounded ridge of loose grains, not a fold.
  "      float hh = sq < 0.66 ? sq / 0.66 : (1.0 - sq) / 0.34;\n"
  "      float dh = sq < 0.66 ? 1.0 / 0.66 : -1.0 / 0.34;\n"
  // Two independent fades, and both are needed. `fp` is Nyquist. `nv` is the
  // OTHER failure: a bump on a flat quad has no parallax and no self-occlusion,
  // so edge-on it stops reading as relief and starts reading as a printed
  // pattern — exactly where the footprint is largest anyway, so the two fades
  // agree about direction and only disagree about which artefact they prevent.

  // 9 mm on a 12.5 cm wavelength, i.e. a 14-degree lee face.
  "      float amp = 0.0090 * uSurf.x * vis * (0.62 + 0.50 * rv);\n"
  "      vec2 gr = wd * (dh * amp / lam);\n"
  "      n = normalize(n - vec3(gr.x, 0.0, gr.y));\n"
  // The albedo term is deliberately weak.
  "      alb *= 1.0 + (hh - 0.5) * 0.075 * uSurf.x * vis;\n"
  "      rough = clamp(rough + 0.11 * uSurf.x * (1.0 - vis), 0.045, 1.0);\n"
  "    } else {\n"
  // What the early-out skipped still has to arrive as ROUGHNESS, or there is a
  // visible ring around the player where relief turns to plastic.
  "      rough = clamp(rough + 0.11 * uSurf.x, 0.045, 1.0);\n"
  "    }\n"
  "    }\n"
  // --- GRIT ---------------------------------------------------------------
  // The other floor: dry warm concrete with dust and gravel on it.
  "    float gvis = 1.0 - smoothstep(0.02, 0.09, fp);\n"
  "    if (gvis > 0.004) {\n"
  "      float gz = vnoise2(P * 23.0);\n"
  "      alb *= 1.0 + (gz - 0.5) * 0.16 * det * gvis;\n"
  "      rough = clamp(rough + (gz - 0.5) * 0.10 * det * gvis, 0.045, 1.0);\n"
  "    }\n"
  // --- SUN GLITTER --------------------------------------------------------
  // Mica in sand, and it is three invariants rather than a noise function.
  "    if (uSurf.y > 0.0) {\n"
  "      vec3 Rv = reflect(-V, n);\n"
  "      float corr = pow(max(dot(Rv, SUN_DIR), 0.0), 3.5) * det;\n"
  "      if (corr > 0.0015) {\n"
  "        vec3 Hs = normalize(SUN_DIR + V);\n"
  // THREE TIMES the footprint, not one.
  "        float lod = max(log2(max(fp / 0.0038, 1.0)), 0.0);\n"
  "        float l0 = floor(lod), lf = lod - l0;\n"
  "        float spark = 0.0;\n"
  // The upper level is only mixed in while the cross-fade is actually between two of
  // them.
  "        int lmax = lf > 0.02 ? 2 : 1;\n"
  "        for (int L = 0; L < lmax; L++) {\n"
  "          float cs = 0.0038 * exp2(l0 + float(L)) * 3.0;\n"
  "          vec2 gi = floor(P / cs);\n"
  "          vec3 r3 = vhash3(gi + vec2(11.3, 7.7) * (l0 + float(L)));\n"
  "          if (r3.z > 0.928) {\n"
  "            vec3 fn = normalize(vec3((r3.x - 0.5) * 0.21, 1.0,\n"
  "                                     (r3.y - 0.5) * 0.21));\n"
  "            float dt = max(dot(fn, Hs), 0.0);\n"
  // A soft DISC inside the cell rather than the whole cell: a flat-topped
  // square of light is a pixel block, and a spark is round.
  "            vec2 cf = fract(P / cs) - 0.5;\n"
  "            float cw = 1.0 - smoothstep(0.14, 0.46, length(cf));\n"
  "            spark += (L == 0 ? 1.0 - lf : lf) * cw\n"
  "                   * exp(-2400.0 * (1.0 - dt));\n"
  "          }\n"
  "        }\n"
  "        glint = spark * corr * uSurf.y;\n"
  "      }\n"
  "    }\n"
  // --- STANDING WATER -----------------------------------------------------
  // One height field, TWO thresholds.
  "    if (uSurf.z > 0.0) {\n"
  "      float hgt = vnoise2(P * 0.155) * 0.64 + vnoise2(P * 0.41) * 0.36;\n"
  "      float pool = smoothstep(-0.045, 0.045, uSurf.z - hgt);\n"
  // The halo is WIDER than the film by a real margin.
  "      float damp = smoothstep(-0.13, 0.13, uSurf.z + 0.125 - hgt);\n"
  // 0.72, NOT 0.32, and the first number was a misread of the source it came from.
  "      alb *= mix(1.0, 0.72, damp) * mix(1.0, 0.76, pool);\n"
  // NEVER a literal zero.
  "      rough = mix(rough, 0.05, pool);\n"
  "      n = normalize(mix(n, vec3(0.0, 1.0, 0.0), pool));\n"
  // A COMPLETELY still film is a mirror, and a mirror the size of a room lying on a
  // floor reads as a hole in the world rather than as water.
  "      float ws = 1.0 - smoothstep(0.02, 0.10, fp);\n"
  "      vec2 wv = vec2(vnoise2(P * 9.0 + vec2(3.1, 1.7)) - 0.5,\n"
  "                     vnoise2(P * 9.0 - vec2(5.3, 2.9)) - 0.5);\n"
  "      n = normalize(n + vec3(wv.x, 0.0, wv.y) * 0.05 * pool * ws);\n"
  "      wet = pool;\n"
  "    }\n"
  "  }\n"
  // GEOMETRIC SPECULAR ANTIALIASING, IN THE SPACE THE DERIVATION IS IN. This added the
  // normal variance to ALPHA (rough*rough); Tokuyoshi & Kaplanyan add it to ALPHA
  // SQUARED, and the difference is not cosmetic. Measured against the authored
  // roughnesses in this file, adding to alpha multiplies a 0.44 receiver's roughness by
  // 1.01 and a 0.05 lens's by 1.02 — i.e. the term did essentially nothing on the
  // smooth parts it exists for. In alpha^2 the same variance takes the lens to 2.4x.
  // And the KAPPA CLAMP bounds the KERNEL, where min(1.0, ...) bounded the RESULT: a
  // 45-degree chamfer band gives |dn| = 0.765 inside the one quad that straddles it,
  // which drove the old form to a fully matte 1.0 — a one-pixel dead line down every
  // arris on the weapon, i.e. down the parts this pass is about.
  "  vec3 dnx = dFdx(n), dny = dFdy(n);\n"
  "  float saa = min(0.3183099 * (dot(dnx, dnx) + dot(dny, dny)), 0.18);\n"
  "  float ral = rough * rough;\n"
  "  rough = sqrt(clamp(ral * ral + saa, 0.0, 1.0));\n"
  "  rough = sqrt(rough);\n"
  // Water is a dielectric with an index of 1.33, so its reflectance at normal incidence
  // is 0.02 — HALF the 0.04 the substrate under it has.
  "  vec3 F0 = mix(mix(vec3(0.04), alb, metal), vec3(0.02), wet);\n"
  "  vec3 diff = alb * (1.0 - metal);\n"
  "  vec3 f90 = vec3(1.0);\n"
  // A COATED LENS IS THE ONE SURFACE WHOSE WHOLE BEHAVIOUR LIVES IN THE FRESNEL. It was
  // authored as a dielectric with a dark blue-green albedo, which under metalness 0
  // lands in `diff` — where a lens has none, and where the colour can only be a
  // near-black diffuse tint. Uncoated crown glass reflects 4.3% on axis; a broadband
  // multilayer coating takes that under 0.5%, which is exactly why a scope's pupil is
  // the darkest thing on a rifle. So: F0 goes to the coating's residual, the AUTHORED
  // colour becomes F90 — the rim, where the coating's notch has walked off its design
  // wavelength — and the diffuse goes away. Zero extra instructions; the existing
  // env * Fe path then gives a near-black reflection on axis rising to a coloured one
  // at the edge, which is the whole cue.
  "  if (vMat.x < 0.14 && metal < 0.5) {\n"
  "    F0 = vec3(0.004, 0.010, 0.006);\n"
  "    f90 = alb * 8.0;\n"
  "    diff = vec3(0.0);\n"
  "  }\n"
  "  float NoV = max(dot(n, V), 1e-4);\n"
  // Diffuse irradiance: the same sky, smoothed to a hemisphere lobe. Sun-facing AO is
  // only partly applied — a crease is dark in ambient light, not in a direct beam.
  "  vec3 amb = mix(uAmbD, uAmbU, n.y * 0.5 + 0.5);\n"
  "  vec3 irr = amb * ao;\n"
  // THE ENVIRONMENT TERM *IS* THE METAL — a metal has no diffuse, so with only a sun
  // lobe it is black everywhere the highlight is not — and on the weapon it was closed
  // with a fudge: a Schlick rim against max(1-rough, F0), times a flat (1 - 0.62*rough)
  // further on to stand in for the energy a rough lobe loses. The analytic split-sum
  // fit is the actual hemispherical integral of GGX x Fresnel and carries both of those
  // honestly, for one exp2 against the pow it replaces.
  //
  // ...AND IT IS GATED ON uVm, WHICH IS THE POINT. This is a WEAPON pass. The fit and
  // the fudge disagree most at GRAZING angles on ROUGH surfaces — which is the distant
  // floor, i.e. half of every frame — and swapping them moved the arena by 0.09/255
  // over 8.9% of its subpixels. The arena's look is settled and tuned; a change to it
  // has to be argued on its own, not smuggled in behind a rifle. The branch is
  // uniform-valued per draw, so it costs nothing to take.
  "  vec3 Fe;\n"
  "  if (uVm > 0.5) {\n"
  "    vec4 dfr = rough * vec4(-1.0, -0.0275, -0.572, 0.022)\n"
  "             + vec4(1.0, 0.0425, 1.04, -0.04);\n"
  "    float a004 = min(dfr.x * dfr.x, exp2(-9.28 * NoV)) * dfr.x + dfr.y;\n"
  "    vec2 dfg = vec2(-1.04, 1.04) * a004 + dfr.zw;\n"
  "    Fe = F0 * dfg.x + dfg.y;\n"
  "  } else {\n"
  "    float f5 = 1.0 - NoV, f5s = f5 * f5;\n"
  "    Fe = (F0 + (max(vec3(1.0 - rough), F0) - F0) * (f5s * f5s * f5))\n"
  "       * (1.0 - 0.62 * rough);\n"
  "  }\n"
  // NO MULTIPLE-SCATTERING COMPENSATION. The standard form is 1 + F0 * (1/Ess - 1),
  // which wants the DIRECTIONAL ALBEDO out of a real DFG lookup table; `dfg.y` from the
  // analytic fit is the split-sum BIAS, not Ess, and it falls to 0.0014 at the floor's
  // own roughness — so the "compensation" came out at 29x and 46% of the frame clipped
  // to white. `dfg.x` already carries the roughness-dependent energy loss, which is the
  // half that was actually missing.
  // SIXTEEN SHADOW TAPS ONLY WHERE THE ANSWER IS READ. `sh` has exactly three readers:
  // lit() with SUN_COL * sh (and lit returns vec3(0) before it touches its radiance
  // argument when dot(n, SUN_DIR) <= 0), the glint term, and the puddle term — both
  // guarded.
  "  float sh = 0.0;\n"
  "  if (dot(n, SUN_DIR) > 0.0 || glint > 0.0 || wet > 0.0)\n"
  "    sh = mix(0.45, 1.0, ao) * sun_shadow();\n"
  "  vec3 env = env_gloss(reflect(-V, n), rough) * mix(1.0, ao, 0.75);\n"
  // INSIDE THE FILM, THE SUN DISC IS SUPPRESSED. env_gloss hands a 0.05-rough surface a
  // lobe about seven hundred wide, i.e. a near-delta reflection of a near-delta sun:
  // one pixel of pure white that no filter in the frame can average, sliding across the
  // water as the camera moves.
  "  if (wet > 0.0) {\n"
  "    vec3 Rw = reflect(-V, n);\n"
  "    vec3 wenv = sky_env(Rw, 0.0)\n"
  "              + SUN_COL * 0.42 * pow(max(dot(Rw, SUN_DIR), 0.0), 90.0);\n"
  // ...AND IT IS SCALED BY THE SUN TERM, which is a sky-VISIBILITY proxy and not a
  // fudge: a film of water a hand's width from a three-metre wall has half its
  // hemisphere behind that wall, so it cannot reflect a whole sky.
  "    env = mix(env, wenv * mix(1.0, ao, 0.75) * mix(0.45, 1.0, sh), wet);\n"
  "  }\n"

  // Grazing-angle sheen.
  "  vec3 sheen = diff * rough * rough * pow(1.0 - NoV, 4.0) * 0.85 * amb;\n"
  // Bounce off the floor: down-facing surfaces near the ground pick up warm light
  // reflected off it.
  "  float bnc = 0.0;\n"
  "  if (n.y < 0.0) bnc = -n.y * exp(-max(vWorld.y, 0.0) * 0.85);\n"
  // ...AND SIDEWAYS, which is what the down-facing version was missing.
  "  float bncs = (1.0 - abs(n.y)) * exp(-max(vWorld.y, 0.0) * 2.2);\n"
  "  vec3 bounce = diff * (bnc + bncs * 0.55) * uBounce;\n"
  // THE TERNARY IS PARENTHESISED, and the reason is a bug this file has now paid for
  // twice in two forms: written as `col = c ? a : b;` followed by the four continuation
  // lines, the SEMICOLON ended the statement and turned `+ diff * irr + env * Fe +
  // sheen + bounce` into a dead expression statement that compiles clean. The frame
  // then carried the direct sun lobe and nothing else — black shadows, black on every
  // face the sun did not reach — and the SKY was pixel-identical, which is what made it
  // read as a material change rather than as a dropped statement.
  "  vec3 col = (gani > 0.0\n"
  "     ? lit_ax(n, V, SUN_DIR, diff, F0, f90, rough, gT, gani, SUN_COL * sh)\n"
  "     : lit(n, V, SUN_DIR, diff, F0, f90, rough, SUN_COL * sh))\n"
  "           + diff * irr\n"
  "           + env * Fe\n"
  "           + sheen * mix(0.55, 1.0, ao) + bounce;\n"
  // The glitter rides in AS A LIGHT, after the shadow term and scaled by it: a
  // spark in a wall's shadow is a spark with no sun on it, and adding this
  // before `sh` was the difference between mica and fairy lights.
  "  if (glint > 0.0) col += SUN_COL * glint * sh * 0.32;\n"
  // The viewmodel is not in the world: it has its own projection and its own depth
  // clear, and the world shadow map does not cover it.
  "  if (uVm > 0.5) {\n"
  "    if (gani > 0.0) {\n"
  "      col += lit_ax(n, V, uFill, diff, F0, f90, rough, gT, gani, VM_FILL);\n"
  "      col += lit_ax(n, V, uRim, diff, F0, f90, rough, gT, gani, VM_RIM);\n"
  "    } else {\n"
  "      col += lit(n, V, uFill, diff, F0, f90, rough, VM_FILL);\n"
  "      col += lit(n, V, uRim, diff, F0, f90, rough, VM_RIM);\n"
  "    }\n"
  // A shot is the brightest light source in the player's world for two ticks, and it
  // used to illuminate nothing: the additive flash ribbon was drawn in front of a rifle
  // that stayed exactly as dark as it had been the tick before, so firing read as a
  // decal stuck on the screen rather than as an event happening to the weapon.
  "    if (uMuzC.r > 0.001) col += lit(n, V, uMuzL, diff, F0, f90, rough, uMuzC);\n"
  "  }\n"
  // ...AND THE SAME SHOT LIGHTS THE WORLD, which it did not: a wall 1.4 m in front of
  // the muzzle brightened by 0.07% on the bright flash frame.
  "  if (uVm < 0.5 && uMuzC.r > 0.001) {\n"
  "    vec3 md = uMuzP - vWorld;\n"
  "    float md2 = dot(md, md);\n"
  // k = 0.6 and radiance 8.0, swept against the wall-patch measurement rather than
  // guessed: 1/(1+d^2*0.6) puts a wall at 1.4 m up 7.2% in mean luminance on the bright
  // flash frame (it was +0.25%, i.e. nothing), 8 m at 0.0% and 24 m at 0.0%.
  "    float att = 1.0 / (1.0 + md2 * 0.6);\n"
  "    if (att > 0.002)\n"
  "      col += lit(n, V, normalize(md), diff, F0, f90, rough, uMuzC * att);\n"
  "  }\n"
  // --- AERIAL PERSPECTIVE -------------------------------------------------
  // NOT A FUNCTION OF DISTANCE. The old term was 1 - exp(-d*d*k), which says
  // the air is the same everywhere; it is not, because dust settles. The
  // density falls off exponentially with height and what this ray crosses is
  // the INTEGRAL of that profile along it — closed form, so it costs two exp
  // and a divide rather than a march.
  //
  // The difference is the whole point and it is visible on one wall: a 2.75 m
  // solid is measurably hazier at its foot than at its cap, so it separates
  // from the wall behind it instead of sharing its value. A distance fog
  // cannot do that at any strength.
  //
  // The divide is by the HEIGHT DIFFERENCE, not by the ray's y component: the
  // two are the same quantity scaled by d, and writing it this way keeps the
  // horizontal ray — where the integral degenerates to a plain length — one
  // branch away rather than one cancellation away. The branch's threshold is
  // in metres of height, so it is the same threshold at every distance.
  //
  // The colour is sky_env along the SAME ray, which already carries the haze
  // band: fully-fogged geometry lands on exactly the sky that would be behind
  // it, so the join at a distant wall's top edge holds by construction and not
  // by a matched constant.
  "  float dy = vWorld.y - uEye.y;\n"
  "  float ea = exp(-max(uEye.y, 0.0) * FOG_INVH);\n"
  "  float eb = exp(-max(vWorld.y, 0.0) * FOG_INVH);\n"
  "  float od = uAtmo.w * FOG_RHO_K * d\n"
  "           * (abs(dy) > 0.05 ? (ea - eb) / (dy * FOG_INVH) : ea);\n"
  // A THIRD sky_env, and the nearest geometry in the frame does not need it.
  // The whole viewmodel sits 30 cm from the eye and every wall inside about a
  // metre and a half integrates to under 0.002 of optical depth, i.e. under
  // half a display level. Worth 1.7 ms on a first-person ADS frame (130.2 ->
  // 128.5 on the llvmpipe proxy) and NOTHING on a camera-override shot, which
  // is the honest way to read it: this gate pays where the viewmodel is, and a
  // rig driven by `cam` never draws one.
  "  if (od > 0.002)\n"
  "    col = mix(col, sky_env(-V, 0.0), 1.0 - exp(-od));\n"
  "  oCol = vec4(tonemap(col), 1.0);\n"
  "}\n";

// Sky: fullscreen triangle at depth 1.0 drawn after the world (LEQUAL passes only where
// nothing else did — near-zero overdraw).
static const char *SKY_VS =
  "#version 330 core\n"
  "out vec2 vNdc;\n"
  "void main(){\n"
  "  vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
  "  vNdc = p;\n"
  "  gl_Position = vec4(p, 1.0, 1.0);\n"
  "}\n";

// ...and ONE THIN CLOUD DECK, which lives in this pass and NOWHERE else — sky_env is
// also the horizon fog colour and the environment every specular surface reflects, and
// a cloud field in either is a wall that changes colour as you walk and a rifle that
// mirrors weather.
static const char *SKY_FS =
  "#version 330 core\n"
  GLSL_ENV
  "in vec2 vNdc; out vec4 oCol;\n"
  "uniform vec3 uFwd, uRight, uUp; uniform vec2 uTan;\n"
  // (cos, sin) of the arena's OWN wind heading, and the deck's coverage.
  "uniform vec2 uWindD; uniform float uCov;\n"
  // The sun's heading, normalized once on the CPU: it is a uniform, so doing it
  // per pixel is a rsqrt for a value that cannot change within the draw.
  "uniform vec2 uSunXZ;\n"
  // BANDS, NOT BLOBS. A deck sampled on an isotropic grid is cotton wool; what
  // a sky actually shows is the wind, drawn out along it. The lookup runs in
  // the WIND'S frame and is squashed 4:1 across it, so the streaks lie along
  // the same heading that combs the sand into ripples and drags the drifts on
  // the lee of every wall — one wind, three places it shows.
  "float deck(vec2 p){\n"
  "  vec2 q0 = vec2(dot(p, uWindD), dot(p, vec2(-uWindD.y, uWindD.x)));\n"
  "  q0 = vec2(q0.x * 0.52, q0.y * 1.28);\n"
  "  float w = vnoise2(q0 * 0.55) - 0.5;\n"
  "  vec2 q = q0 + vec2(w, -w * 0.6) * 1.15;\n"
  "  return vnoise2(q) * 0.62 + vnoise2(q * 2.31) * 0.38;\n"
  "}\n"
  "void main(){\n"
  "  vec3 dir = normalize(uFwd + uRight * (vNdc.x * uTan.x) + uUp * (vNdc.y * uTan.y));\n"
  "  vec3 c = sky_env(dir, 1.0);\n"
  "  float up = dir.y;\n"
  // 0.012, not 0.03: at a low sun the interesting deck is exactly the part
  // lying ON the horizon, and the old cut-in threw that band away and started
  // the clouds a hand's width above the one place they had to be.
  "  if (up > 0.012) {\n"
  "    float t = min(1.0 / max(up, 0.05), 16.0);\n"
  "    vec2 p = dir.xz * t * 0.86;\n"
  "    float n = deck(p);\n"
  "    float dens = smoothstep(1.0 - uCov, 1.0 - uCov + 0.20, n);\n"
  "    dens *= smoothstep(0.012, 0.20, up);\n"
  "    if (dens > 0.002) {\n"
  // One offset sample TOWARD the sun, no march: where the deck is thick on the sun
  // side, this pixel is behind it and its underside goes grey.
  "      float d2 = smoothstep(1.0 - uCov, 1.0 - uCov + 0.20,\n"
  "                            deck(p + uSunXZ * 0.42));\n"
  "      float litc = exp(-d2 * 1.9) * (1.0 - exp(-d2 * 2.0 - 0.55));\n"
  "      vec3 base = sky_env(dir, 0.0);\n"
  // A CLOUD IS LIT BY THE SUN, NOT TINTED BY THE SKY BEHIND IT. The old term
  // was base * 1.34..2.05 — a multiply on whatever was behind the pixel — so
  // at dusk, when the sky behind goes violet, the cloud went violet too and
  // the one thing a sunset sky is FOR never happened. Splitting it into an
  // ambient half (the sky, which does light the deck from every side) and a
  // direct half (SUN_COL, which carries the reddening) is what makes the deck
  // burn orange over a blue-violet zenith.
  "      vec3 cl = base * mix(0.58, 1.02, litc)\n"
  "              + SUN_COL * (0.020 + 0.42 * litc * litc);\n"
  // THE RIM IS WHERE THE LIGHT COMES THROUGH. dens*(1-dens) peaks exactly on
  // the edge of a band and is zero in its body and in clear sky, so this adds
  // a bright fringe only where the deck is thin — and only toward the sun,
  // which is the whole reason a backlit cloud has a burning edge and a
  // front-lit one does not.
  "      float s2 = max(dot(dir, SUN_DIR), 0.0);\n"
  "      cl += SUN_COL * (dens * (1.0 - dens)) * 2.6 * pow(s2, 6.0);\n"
  "      c = mix(c, cl, dens * 0.86);\n"
  "    }\n"
  "  }\n"
  "  oCol = vec4(tonemap(c), 1.0);\n"
  "}\n";

// Frosted-glass menu backdrop: an attribute-less fullscreen triangle and one
// dual-Kawase tap, ping-ponged a few times at quarter resolution — the
// logarithmic-cost blur every modern menu uses, in two tiny shaders.
static const char *BLUR_VS =
  "#version 330 core\n"
  "out vec2 vUV;\n"
  "void main(){ vec2 p = vec2(gl_VertexID==1?3.0:-1.0, gl_VertexID==2?3.0:-1.0);\n"
  "  vUV = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
static const char *BLUR_FS =
  "#version 330 core\n"
  "in vec2 vUV; uniform sampler2D uTex; uniform vec2 uPix;\n"
  "out vec4 oCol;\n"
  "void main(){\n"
  "  vec3 c = texture(uTex, vUV).rgb * 4.0;\n"
  "  c += texture(uTex, vUV + uPix).rgb;\n"
  "  c += texture(uTex, vUV - uPix).rgb;\n"
  "  c += texture(uTex, vUV + vec2(uPix.x, -uPix.y)).rgb;\n"
  "  c += texture(uTex, vUV - vec2(uPix.x, -uPix.y)).rgb;\n"
  "  oCol = vec4(c * 0.125, 1.0); }\n";   // exact, and three fdiv fewer

// GLARE — the bloom and the light shafts, and they are ONE chain because they are
// one phenomenon: bright things in front of a dusty atmosphere spill, and the
// spill runs along the line to the sun when there is anything between. Building
// them separately would mean two bright-passes, two quarter-res targets and two
// composites for a result that is added together anyway.
//
// Everything here runs at quarter resolution, so ten taps cost 0.6 of a full-res
// tap. That is the entire budget: no HDR buffer, no extra attachment, no depth
// read — the input is the frame that was going to be presented regardless, pulled
// down through the SAME blit and the SAME dual-Kawase ping-pong the frosted menu
// backdrop already owned.
//
// THE OCCLUSION IS FREE AND EXACT. A shaft is a march toward the sun through the
// bright-pass, so a wall between this pixel and the sun contributes nothing and
// the shaft simply stops at its edge — no shadow lookup, no geometry, no list of
// occluders that can disagree with the one the depth pass drew.
static const char *GLARE_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uTex;\n"
  "uniform vec3 uSunUV;\n"   // xy = the sun's place on screen, z = shaft strength
  "uniform vec2 uKnee;\n"    // luminance knee: what counts as bright
  "uniform vec3 uGain;\n"    // x bloom, y shafts, z composite
  "uniform vec4 uGrade;\n"   // vignette, grain, grain seed, aspect
  "uniform float uMode;\n"   // 0 = build the glare, 1 = add it back
  "const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);\n"
  "void main(){\n"
  // --- THE COMPOSITE, AND IT IS ALSO THE GRADE ---------------------------
  // ONE PASS DOES BOTH, and the blend mode is why. Under ONE/SRC_ALPHA the
  // result is src.rgb + dst.rgb * src.a: the colour channels carry everything
  // that ADDS light (the bloom, the shafts, the grain's bright half, the edge
  // tint) and the alpha channel carries everything that MULTIPLIES it (the
  // vignette, the grain's dark half). A grade written the obvious way needs a
  // second full-res pass to multiply by, or the frame in a texture to read
  // back; this needs neither.
  //
  // The consequence is a real constraint and it is worth stating: a blend
  // factor is clamped to [0,1] against a fixed-point target, so the alpha half
  // can only ever DARKEN. That is why the grain is split across both channels
  // rather than written as one signed term.
  "  if (uMode > 0.5) {\n"
  "    vec2 q = (vUV - 0.5) * vec2(uGrade.w, 1.0);\n"
  "    float r2 = dot(q, q) * 1.30;\n"
  // Interleaved-gradient noise: three instructions, and unlike a value-noise
  // hash it has no visible cell structure at one sample per pixel. The seed is
  // driven by the SIM TICK and not by the rendered-frame counter — grain that
  // moves with the render clock would make every harness screenshot depend on
  // how many frames preceded it, which is the same trap the menu caret's lack
  // of a blink documents.
  "    float g = fract(52.9829189 * fract(dot(gl_FragCoord.xy + uGrade.z,\n"
  "                                           vec2(0.06711056, 0.00583715))));\n"
  "    float gn = (g - 0.5) * uGrade.y;\n"
  "    float vig = 1.0 - uGrade.x * r2 * r2;\n"
  // The edges go cool as well as dark. A vignette that only darkens reads as a
  // lens fault; one that also loses a little warmth reads as the falloff of a
  // real lens, and it costs an add.
  "    vec3 add = texture(uTex, vUV).rgb * uGain.z\n"
  "             + max(gn, 0.0) * 0.42\n"
  "             + vec3(-0.008, -0.002, 0.010) * r2;\n"
  "    oCol = vec4(add, clamp(vig * (1.0 + min(gn, 0.0)), 0.0, 1.0));\n"
  "    return;\n"
  "  }\n"
  // A LUMINANCE KNEE, NOT A SUBTRACTION. Subtracting a threshold from each
  // channel shifts the HUE of everything that glows — a warm 0.9 highlight comes
  // out orange-red — and the one thing this pass must not do is recolour the
  // sunset it is there to sell. Scaling by a smoothstep on the luminance keeps
  // the colour and only decides how much of it spills.
  "  vec3 s0 = texture(uTex, vUV).rgb;\n"
  "  vec3 core = s0 * smoothstep(uKnee.x, uKnee.y, dot(s0, LUMA));\n"
  "  vec3 acc = vec3(0.0);\n"
  "  if (uSunUV.z > 0.0) {\n"
  "    vec2 d = (uSunUV.xy - vUV) * (0.85 / 10.0);\n"
  "    vec2 uv = vUV;\n"
  "    float w = 1.0;\n"
  "    for (int i = 0; i < 10; i++) {\n"
  "      vec3 s = texture(uTex, uv).rgb;\n"
  "      acc += s * smoothstep(uKnee.x, uKnee.y, dot(s, LUMA)) * w;\n"
  "      w *= 0.90;\n"
  "      uv += d;\n"
  "    }\n"
  "    acc *= uSunUV.z * 0.1;\n"
  "  }\n"
  "  oCol = vec4(core * uGain.x + acc * uGain.y, 1.0);\n"
  "}\n";

static const char *HUD_VS =
  "#version 330 core\n"
  "layout(location=0) in vec2 aPos;\n"   // pixels, origin top-left
  "layout(location=1) in vec2 aUV;\n"
  "layout(location=2) in vec4 aCol;\n"
  "layout(location=3) in float aMode;\n" // <1 = SDF threshold, >=2 = raw alpha
  "uniform vec2 uScreen;\n"
  "out vec2 vUV; out vec4 vCol; out float vMode;\n"
  "void main(){ vUV=aUV; vCol=aCol; vMode=aMode;\n"
  "  gl_Position=vec4(2.0*aPos.x/uScreen.x-1.0, 1.0-2.0*aPos.y/uScreen.y, 0.0, 1.0); }\n";

// uImg selects what the bound texture MEANS: 0 = the glyph atlas, 1 = an image (RGB
// straight through, tinted for the scope's slight glass warmth).
static const char *HUD_FS =
  "#version 330 core\n"
  "in vec2 vUV; in vec4 vCol; in float vMode;\n"
  "uniform sampler2D uTex; uniform int uImg;\n"
  "out vec4 oCol;\n"
  "void main(){\n"
  "  if (uImg == 1) { oCol = vec4(texture(uTex, vUV).rgb * vCol.rgb, vCol.a); return; }\n"
  "  float v = texture(uTex, vUV).r;\n"
  "  float a;\n"
  "  if (vMode >= 2.0) a = v;\n"
  "  else { float w = max(fwidth(v), 1e-4); a = smoothstep(vMode - w, vMode + w, v); }\n"
  "  oCol = vec4(vCol.rgb, vCol.a * a); }\n";

