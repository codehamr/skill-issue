// =============================================================================
// render/shaders.inc  —  every GLSL source string (GLSL_ENV, WORLD_FS, SKY_FS,
// BLUR/VOLUME/GLARE/HUD). Proof: the shot-* battery + the shimmer rig (CLAUDE.md).
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
// signed offset from this value, and the granular floor field reads it back to decide
// where loose sand exists at all.
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
  /* Value plus its analytic world-space gradient. WORLD_FS uses this for the
     nearest floor aggregate: analytic derivatives remain valid behind the
     footprint gate, unlike dFdx/dFdy inside fragment-divergent control flow. */ \
  "vec3 vnoise2d(vec2 p){\n" \
  "  vec2 i = floor(p), f = fract(p);\n" \
  "  float a = vhash2(i), b = vhash2(i + vec2(1.0, 0.0));\n" \
  "  float c = vhash2(i + vec2(0.0, 1.0)), d = vhash2(i + vec2(1.0, 1.0));\n" \
  "  vec2 u = f * f * (3.0 - 2.0 * f), du = 6.0 * f * (1.0 - f);\n" \
  "  float v = mix(mix(a, b, u.x), mix(c, d, u.x), u.y);\n" \
  "  vec2 g = du * vec2(mix(b - a, d - c, u.y),\n" \
  "                     mix(c - a, d - b, u.x));\n" \
  "  return vec3(v, g);\n" \
  "}\n" \
  /* THE STANDING WATER, as one function with two readers: the lit floor
     (which shades the film) and the geometry prepass (which flags it for the
     reflection pass). One height field, one threshold, so the pixels the
     reflection lands on are exactly the pixels drawn as water. */ \
  "float pool_mask(vec2 P, float level){\n" \
  "  float hgt = vnoise2(P * 0.155) * 0.64 + vnoise2(P * 0.41) * 0.36;\n" \
  "  return smoothstep(-0.045, 0.045, level - hgt);\n" \
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
  "uniform vec4 uBiome;\n" \
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
  "uniform vec3 uHaze, uHazeS, uAmbD, uAmbU, uBounce, uBounceS, uAmbS, uSunH;\n" \
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
  /* THE LOOK: x selects the curve (0 = the ACES fit, 1 = AgX), y is a contrast
     about display mid-grey, z a saturation about luminance, both applied in
     DISPLAY space after the curve so a change of curve and a change of grade
     stay two separate decisions. The harness `look` command drives all of
     them at runtime; the defaults are what ships. */ \
  "uniform vec4 uLook;\n" \
  /* AgX (Sobotka; the minimal fit by Wrensch): a log2 encode over 16.5 stops,
     a sixth-order sigmoid, and the inset/outset matrices that keep a saturated
     highlight desaturating toward white instead of skewing hue — the thing the
     ACES fit does to a sunset (orange to yellow) and to the landmark reds. The
     output is display-encoded already; the sqrt below is skipped for it. */ \
  "vec3 agx_curve(vec3 x){\n" \
  "  vec3 x2 = x * x, x4 = x2 * x2;\n" \
  "  return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x\n" \
  "       + 0.4298 * x2 + 0.1191 * x - 0.00232;\n" \
  "}\n" \
  "vec3 agx(vec3 v){\n" \
  "  const mat3 M = mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,\n" \
  "                      0.0784335999999992, 0.878468636469772, 0.0784336,\n" \
  "                      0.0792237451477643, 0.0791661274605434, 0.879142973793104);\n" \
  "  const mat3 Mi = mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,\n" \
  "                       -0.0980208811401368, 1.15190312990417, -0.0980434501171241,\n" \
  "                       -0.0990297440797205, -0.0989611768448433, 1.15107367264116);\n" \
  "  v = M * v;\n" \
  "  v = clamp(log2(max(v, 1.0e-10)), -12.47393, 4.026069);\n" \
  "  v = (v + 12.47393) * (1.0 / 16.5);\n" \
  "  v = agx_curve(v);\n" \
  "  return clamp(Mi * v, 0.0, 1.0);\n" \
  "}\n" \
  /* BRIGHTNESS IS AN EXPOSURE, NOT A LIFT. uLook.w multiplies the scene here,
     before the curve and before the bloom weight is read off it, so turning it
     down darkens the way stopping a lens down does — the highlights roll back
     into the shoulder — instead of crushing a picture that has already been
     tonemapped and clipped. 1.0 is the shipped exposure. */ \
  "vec3 tonemap(vec3 x){\n" \
  "  x *= EXPOSURE * uLook.w;\n" \
  "  if (uLook.x > 0.5) {\n" \
  "    x = agx(x);\n" \
  "  } else {\n" \
  "    x = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);\n" \
  "    x = sqrt(clamp(x, 0.0, 1.0));\n" \
  "  }\n" \
  "  float lm = dot(x, vec3(0.2126, 0.7152, 0.0722));\n" \
  "  x = mix(vec3(lm), x, uLook.z);\n" \
  "  x = clamp(0.5 + (x - 0.5) * uLook.y, 0.0, 1.0);\n" \
  /* Half an LSB of interleaved-gradient noise. A smooth sky quantised to 8 bits
     shows about eighty hard Mach bands ten pixels apart; dither turns the step
     into grain the eye integrates away, and it costs three instructions. */ \
  "  float dth = fract(52.9829189 * fract(dot(gl_FragCoord.xy,\n" \
  "                                          vec2(0.06711056, 0.00583715))));\n" \
  "  return x + (dth - 0.5) * 0.0039216;\n" \
  "}\n" \
  /* THE BLOOM IS DECIDED BEFORE THE TONEMAP, and it rides in the alpha channel.
     The picture is display-referred by the time the glare chain reads it, so a
     knee on the displayed luminance cannot tell a sunlit wall at 0.8 from a
     sun disc at 26: both clip to about the same white. This is the scene
     luminance (after exposure, before the curve) in stops over the point where
     the curve starts to shoulder (1.2), mapped to [0,1] over three stops — wide
     enough that the sun's aureole counts, so a noon sun blinds the way the
     old display-knee bloom made it, with a real falloff instead of a clipped
     blob. The exposure it measures is the SAME exposure tonemap() applies,
     uLook.w included: a picture stopped down a third must spill a third less,
     or the halo survives the frame it belongs to. The chain
     multiplies the colour by it, so what spills is what was actually bright,
     in its own colour. An RGBA8 target carries it, MSAA resolves it, nothing
     else in the frame reads the alpha. */ \
  "float bloom_w(vec3 c){\n" \
  "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722)) * EXPOSURE * uLook.w;\n" \
  "  return clamp(log2(max(l, 1.0e-4) * (1.0 / 1.2)) * (1.0 / 3.0), 0.0, 1.0);\n" \
  "}\n"

static const char *WORLD_VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "layout(location=1) in vec3 aNrm;\n"
  "layout(location=2) in vec3 aCol;\n"
  "layout(location=3) in vec3 aMat;\n"   // roughness, metalness, ambient occl.
  "uniform mat4 uVP;\n"
  "uniform vec3 uFoliage;\n"
  "uniform mat4 uLightVP;\n"
  "uniform float uSmTexel;\n"            // shadow texel size in world metres
  "out vec3 vNrm; out vec3 vCol; out vec3 vWorld; out vec3 vMat; out vec4 vLit;\n"
  // The shadow lookup is offset ALONG THE NORMAL by a texel and a half rather than
  // biased in depth: a depth bias has to be tuned per slope and leaks light under
  // contact points at grazing angles ("peter-panning"), while a normal offset moves the
  // sample sideways out of the surface's own shadow and needs no slope term at all.
  "void main(){ vNrm=aNrm; vCol=aCol; vMat=aMat; vWorld=aPos;\n"
  "  vLit = uLightVP * vec4(aPos + normalize(aNrm) * uSmTexel * 1.7, 1.0);\n"
  // Fine foliage is excluded from both shadow and depth prepasses. Bending
  // only its tips leaves roots fixed and adds no CPU rebuild or draw call.
  "  if (aMat.x < -1.0 && aPos.y < 0.40 && aPos.y > 0.0) {\n"
  "    float tip = clamp(aPos.y / 0.30, 0.0, 1.0);\n"
  "    float sway = sin(aPos.x * 0.63 + aPos.z * 0.55 + uFoliage.z * 1.4);\n"
  "    vWorld.xz += uFoliage.xy * (sway * tip * tip * 0.018);\n"
  "  }\n"
  "  gl_Position=uVP*vec4(vWorld,1.0); }\n";

// Depth-only pass that fills the shadow map.
static const char *DEPTH_VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "uniform mat4 uVP;\n"
  "void main(){ gl_Position=uVP*vec4(aPos,1.0); }\n";
static const char *DEPTH_FS =
  "#version 330 core\n"
  "void main(){}\n";

// THE GEOMETRY PREPASS: depth plus the world-space normal of what is solid,
// drawn once per frame into single-sample textures BEFORE the lit pass, so the
// screen-space passes (SSAO today) have a picture of the geometry to read
// while the colour is still being decided. It is its own pass and not a second
// attachment on the lit target on purpose: the lit target may be multisampled
// and a screen-space pass wants one depth per pixel, not eight.
static const char *PRE_VS =
  "#version 330 core\n"
  "layout(location=0) in vec3 aPos;\n"
  "layout(location=1) in vec3 aNrm;\n"
  "uniform mat4 uVP;\n"
  "out vec3 vNrm; out vec3 vWorld;\n"
  "void main(){ vNrm = aNrm; vWorld = aPos; gl_Position = uVP * vec4(aPos, 1.0); }\n";
// ...and the alpha is the standing-water mask on the floor (pool_mask, the
// same function the lit floor shades with), so the reflection pass knows
// which pixels are a film of water without a second guess at the noise.
static const char *PRE_FS =
  "#version 330 core\n"
  GLSL_ENV
  "in vec3 vNrm; in vec3 vWorld; out vec4 oNrm;\n"
  "uniform float uPoolLevel;\n"
  "void main(){\n"
  "  float pool = 0.0;\n"
  "  if (uPoolLevel > 0.0 && vNrm.y > 0.95 && vWorld.y < 0.05)\n"
  "    pool = pool_mask(vWorld.xz, uPoolLevel);\n"
  "  oNrm = vec4(normalize(vNrm) * 0.5 + 0.5, pool);\n"
  "}\n";

// SCREEN-SPACE REFLECTIONS, for the standing water only. A pool is a mirror
// lying on the floor, and until now it mirrored the sky alone: a wall a metre
// behind it, a figure standing in it, the landmark box — none of them were in
// the film. This marches the reflected ray through the prepass depth (24
// steps of a third of a metre, then four halvings onto the hit) and reads the
// finished opaque picture where it lands. Everything else about the water —
// its Fresnel, its diffuse, its sky where no hit lands — is still the lit
// floor's; this pass only replaces the sky in the film where something else
// actually stands. Full resolution, but a pixel that is not water leaves in
// three instructions, so on a dry arena the pass is a clear and a quad.
// The hit is faded toward the screen's edges (what leaves the frame cannot
// be reflected) and by the ray's own length (a long march is a guess).
static const char *SSR_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uDepth;\n"   // unit 0: prepass depth
  "uniform sampler2D uNrm;\n"     // unit 1: prepass normal + pool mask
  "uniform sampler2D uPic;\n"     // unit 3: the opaque picture
  "uniform mat4 uInvVP, uVP;\n"
  "uniform vec3 uEye;\n"
  "uniform vec2 uZ;\n"            // near, far
  "float lin_z(float d){\n"
  "  float z = d * 2.0 - 1.0;\n"
  "  return 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - z * (uZ.y - uZ.x));\n"
  "}\n"
  "vec3 world_at(vec2 uv, float d){\n"
  "  vec4 p = uInvVP * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
  "  return p.xyz / p.w;\n"
  "}\n"
  // Screen position AND view depth of a world point, in one projection.
  "vec3 to_screen(vec3 w){\n"
  "  vec4 c = uVP * vec4(w, 1.0);\n"
  "  return vec3(c.xy / c.w * 0.5 + 0.5, c.w);\n"
  "}\n"
  "void main(){\n"
  "  float pool = texture(uNrm, vUV).a;\n"
  "  if (pool < 0.02) { oCol = vec4(0.0); return; }\n"
  "  float d0 = texture(uDepth, vUV).r;\n"
  "  vec3 P = world_at(vUV, d0);\n"
  "  vec3 V = normalize(uEye - P);\n"
  "  vec3 R = reflect(-V, vec3(0.0, 1.0, 0.0));\n"
  "  if (R.y < 0.02) { oCol = vec4(0.0); return; }\n"
  // A per-pixel start offset (the same interleaved-gradient noise as every
  // other screen-space term) turns the step banding into fine grain.
  "  float ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy,\n"
  "                                          vec2(0.06711056, 0.00583715))));\n"
  "  float step = 0.34;\n"
  "  float t = step * (0.3 + 0.7 * ign);\n"
  "  float t_prev = 0.0;\n"
  "  bool hit = false;\n"
  "  vec3 sp = vec3(0.0);\n"
  "  for (int i = 0; i < 24; i++) {\n"
  "    vec3 w = P + R * t;\n"
  "    sp = to_screen(w);\n"
  "    if (sp.x < 0.0 || sp.x > 1.0 || sp.y < 0.0 || sp.y > 1.0) break;\n"
  "    float zs = lin_z(texture(uDepth, sp.xy).r);\n"
  "    float dz = sp.z - zs;\n"
  "    if (dz > 0.0 && dz < 0.9 + 0.04 * t) { hit = true; break; }\n"
  "    t_prev = t;\n"
  "    t += step * (1.0 + 0.06 * float(i));\n"
  "  }\n"
  "  if (!hit) { oCol = vec4(0.0); return; }\n"
  // Four halvings between the last miss and the hit sharpen the edge.
  "  float lo = t_prev, hi = t;\n"
  "  for (int i = 0; i < 4; i++) {\n"
  "    float mid = 0.5 * (lo + hi);\n"
  "    vec3 sm = to_screen(P + R * mid);\n"
  "    float zs = lin_z(texture(uDepth, sm.xy).r);\n"
  "    if (sm.z - zs > 0.0) { hi = mid; sp = sm; } else lo = mid;\n"
  "  }\n"
  "  vec2 e = abs(sp.xy - 0.5) * 2.0;\n"
  "  float edge = 1.0 - smoothstep(0.75, 1.0, max(e.x, e.y));\n"
  "  float len = 1.0 - smoothstep(10.0, 18.0, hi);\n"
  "  vec3 c = texture(uPic, sp.xy).rgb;\n"
  "  oCol = vec4(c, edge * len);\n"
  "}\n";

// ...and the film's Fresnel decides how much of the hit replaces the sky:
// 0.02 straight down, all of it at grazing — the same 1.33 water the lit
// floor uses. The pool mask and the hit confidence multiply in.
static const char *SSR_APPLY_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uSsr;\n"     // unit 0: the reflection (rgb, confidence)
  "uniform sampler2D uNrm;\n"     // unit 1: prepass normal + pool mask
  "uniform sampler2D uDepth;\n"   // unit 2: prepass depth
  "uniform mat4 uInvVP;\n"
  "uniform vec3 uEye;\n"
  "uniform vec2 uSsrPx;\n"        // one texel of the half-res march
  "void main(){\n"
  // Four bilinear taps a half texel out: a 3x3 tent over the march, which
  // takes the odd/even hit pattern a jittered march leaves on a thin
  // reflected figure and turns it into the soft image a film of water gives.
  "  vec4 r = (texture(uSsr, vUV + uSsrPx * vec2(-0.5, -0.5))\n"
  "          + texture(uSsr, vUV + uSsrPx * vec2( 0.5, -0.5))\n"
  "          + texture(uSsr, vUV + uSsrPx * vec2(-0.5,  0.5))\n"
  "          + texture(uSsr, vUV + uSsrPx * vec2( 0.5,  0.5))) * 0.25;\n"
  "  float pool = texture(uNrm, vUV).a;\n"
  "  if (r.a * pool < 0.005) discard;\n"
  "  vec4 p = uInvVP * vec4(vUV * 2.0 - 1.0, texture(uDepth, vUV).r * 2.0 - 1.0, 1.0);\n"
  "  vec3 V = normalize(uEye - p.xyz / p.w);\n"
  "  float f = 1.0 - max(V.y, 0.0); f *= f; f *= f * (1.0 - max(V.y, 0.0));\n"
  // 1.8x the Fresnel, and the reason is what the mix replaces: the pixel
  // under it already carries the film's own SKY reflection at this same
  // Fresnel, and a hit should stand in for that sky, not sit at half strength
  // on top of it. Display-referred mixing on both sides; the factor is what
  // makes a wall in a puddle read as a wall in a puddle.
  "  float F = clamp((0.02 + 0.98 * f) * 1.8, 0.0, 1.0);\n"
  "  oCol = vec4(r.rgb, F * r.a * pool);\n"
  "}\n";

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
  // 0 = no shadow map to read: either the FBO never came up, or SHADOWS is off.
  // The map and its raw twin stay bound on units 0 and 2 either way — this
  // uniform says the read is not asked for, never that the binding is gone.
  "uniform float uShadow;\n"
  "uniform float uVm;\n"                  // 1 = viewmodel: add the hero rig
  "uniform vec3 uMuzC;\n"                 // muzzle-flash radiance (0 = no shot)
  "uniform vec3 uMuzL;\n"                 // direction from the gun to the flash
  "uniform vec3 uMuzP;\n"                 // ...and where it is, for the WORLD
  "uniform vec3 uFill, uRim;\n"
  // The weapon's OWN frame, published by vm_build once per viewmodel pass.
  "uniform vec3 uMdlO, uMdlX, uMdlZ;\n"   // no uMdlY: nothing reads an up axis
  // THE FLOOR'S OWN TREATMENT, one vec4: loose-grain strength, glitter strength,
  // standing-water level and the wind heading. Floor shading itself stays isotropic;
  // the heading remains useful to the atmosphere and world decoration.
  "uniform vec4 uSurf;\n"
  "uniform int uLampN;\n"
  "uniform vec4 uLampPos[" GLSL_NUM(MAP_LIGHT_MAX) "];\n"
  "uniform vec3 uLampCol[" GLSL_NUM(MAP_LIGHT_MAX) "];\n"
  "uniform sampler2DShadow uLampShadow;\n"
  "uniform vec2 uSmStep;\n"               // one shadow texel in UV
  "uniform sampler2DShadow uSm;\n"
  "uniform sampler2D uSmRaw;\n"           // the same map, no compare: blocker depths
  // x = penumbra texels per unit of light-space depth difference (the sun's
  // angular size over the texel, folded in C); y = the blocker bias in depth
  // units (two texels of world, so a surface is never its own blocker).
  "uniform vec2 uSmPcss;\n"
  // SCREEN-SPACE OCCLUSION, half resolution: r = the obscurance, g = the distance
  // from the eye of the surface it was measured on. uAoP.xy is the texture's size
  // in texels, .z its switch — 0 for the scope picture (a different lens, so a
  // different screen) and for the viewmodel (drawn after its own depth clear, so
  // the buffer under it is the wall behind the rifle).
  "uniform sampler2D uAo;\n"
  "uniform vec4 uAoP;\n"
  "uniform sampler2D uPreDepth;\n"   // unit 3, bound by the dust pass only
  "uniform float uSkyCut;\n"
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
  // THE SUN HAS A SIZE, AND THE SHADOW SHOWS IT. Percentage-closer soft shadows:
  // a blocker search on the raw depth (unit 2, the same texture without the
  // compare) finds how far in front of this surface the caster is, the sun's
  // angular size turns that distance into a penumbra width, and a stable tent
  // of hardware-compared taps at that width filters it. A wall's shadow is a
  // razor at its foot and a hand's width soft twenty metres out, which is what
  // a low sun across an arena actually draws — the old 4x4 grid was three
  // texels wide everywhere.
  // The disc orientation is fixed in light space. Rotating a sparse kernel per
  // screen pixel frays a long straight shadow and makes it crawl under camera
  // motion; a fixed Vogel set trades invisible isotropy for a stable edge.
  // THE CAP IS WHAT KEEPS THE PENUMBRA CLEAN. A 5x5 separable tent gives a
  // straight edge nine monotonic coverage levels instead of exposing the
  // individual points of a sparse disc. Its fixed light-space footprint is
  // important: every neighbouring screen pixel sees the same filter.
  "vec2 vogel(int i, int n, float phi){\n"
  "  float r = sqrt((float(i) + 0.5) / float(n));\n"
  "  float t = 2.3999632 * float(i) + phi;\n"
  "  return r * vec2(cos(t), sin(t));\n"
  "}\n"
  "float sun_shadow(){\n"
  // FULLY LIT, and it is the first line of the function on purpose: with no map
  // to read, twelve compared taps of a texture nothing wrote would paint the
  // arena black. Everything below is a per-pixel term whose result is unreadable
  // here, so none of it runs.
  "  if (uShadow < 0.5) return 1.0;\n"
  "  vec3 p = vLit.xyz / vLit.w;\n"
  "  if (p.z >= 1.0) return 1.0;\n"
  "  p = p * 0.5 + 0.5;\n"
  "  const float phi = 0.731;\n"
  // THE WIDE TEST FIRST. Twelve compared taps over the widest penumbra the
  // filter can draw: all lit or all shadowed means this pixel is further from
  // any edge than the penumbra can reach, and that is the answer — most of
  // the floor, most of every wall. Only a pixel inside that band pays for the
  // blocker search and the second, fitted disc. 12 taps common, 32 on the
  // edge, against the 16 flat taps this replaced.
  "  float s = 0.0;\n"
  "  for (int i = 0; i < 12; i++)\n"
  "    s += texture(uSm, vec3(p.xy + vogel(i, 12, phi) * uSmStep * 12.0, p.z));\n"
  "  if (s < 0.5) return 0.0;\n"
  "  if (s > 11.5) return 1.0;\n"
  // Blocker search: the centre plus eight raw depths over the same width, and
  // THE NEAREST BLOCKER DECIDES, not the average. A floor pixel a hand behind
  // a wall's foot is, in light space, a hand from the LIT floor in front of
  // that wall — the only thing between them is the wall's own lit face. The
  // average blocker there is the wall's top, three metres up, which asks for
  // the widest penumbra and so mixes the lit floor into the umbra: a bright
  // seam along every base. The nearest blocker is the lit face a few
  // centimetres up, which asks for a hard contact — the truth. Far out on
  // the floor every blocker is the top and the two agree.
  "  float bmax = -1.0, bn = 0.0;\n"
  "  { float d = texture(uSmRaw, p.xy).r;\n"
  "    if (d < p.z - uSmPcss.y) { bmax = max(bmax, d); bn += 1.0; } }\n"
  // The search pattern is NOT rotated per pixel: with a rotating pattern the
  // nearest blocker each pixel finds is a different one, the fitted width
  // jumps with it, and the penumbra is noise however many compares the disc
  // below spends (measured 6.9 levels of neighbour difference, unchanged
  // from 24 taps at half the width). A fixed pattern finds the same blocker
  // for neighbouring pixels, so the width varies smoothly across the edge.
  "  for (int i = 0; i < 8; i++) {\n"
  "    float d = texture(uSmRaw, p.xy + vogel(i, 8, 0.0) * uSmStep * 10.0).r;\n"
  "    if (d < p.z - uSmPcss.y) { bmax = max(bmax, d); bn += 1.0; }\n"
  "  }\n"
  "  if (bn < 0.5) return 1.0;\n"
  // A three-texel minimum removes the last staircase at contact. It is still
  // only about 4 cm on the high map. Hardware comparison filtering supplies
  // the sub-texel samples; the explicit tent removes quantised diagonal teeth.
  "  float pen = clamp((p.z - bmax) * uSmPcss.x, 3.0, 5.5);\n"
  "  s = 0.0;\n"
  "  float sw = 0.0;\n"
  "  for (int y = -2; y <= 2; y++) {\n"
  "    for (int x = -2; x <= 2; x++) {\n"
  "      float k = (3.0 - abs(float(x))) * (3.0 - abs(float(y)));\n"
  "      vec2 o = vec2(float(x), float(y)) * (0.5 * pen);\n"
  "      s += texture(uSm, vec3(p.xy + o * uSmStep, p.z)) * k;\n"
  "      sw += k;\n"
  "    }\n"
  "  }\n"
  "  return s / sw;\n"
  "}\n"
  // DEPTH-AWARE UPSAMPLE of the half-res occlusion: the four texels under this
  // pixel, bilinear weights scaled by how close each one's surface is to THIS
  // surface. A plain bilinear read of a half-res AO paints a dark halo of the
  // crate's contact onto the wall three metres behind it; weighting by the
  // distance stored beside the AO keeps the two surfaces' occlusion apart. If
  // no texel agrees (a one-pixel sliver), the nearest one wins rather than
  // averaging strangers.
  "float ao_screen(float d){\n"
  "  vec2 h = gl_FragCoord.xy * 0.5 - 0.5;\n"
  "  vec2 i0 = floor(h), f = h - i0;\n"
  "  ivec2 mx = ivec2(uAoP.xy) - 1;\n"
  "  float acc = 0.0, ws = 0.0, near_ao = 1.0, near_w = -1.0;\n"
  "  for (int j = 0; j < 2; j++)\n"
  "    for (int i = 0; i < 2; i++) {\n"
  "      ivec2 c = clamp(ivec2(i0) + ivec2(i, j), ivec2(0), mx);\n"
  "      vec2 t = texelFetch(uAo, c, 0).rg;\n"
  "      float wb = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);\n"
  "      float w = wb / (1.0 + 80.0 * abs(t.g - d) / max(d, 0.2));\n"
  "      acc += t.r * w; ws += w;\n"
  "      if (wb > near_w) { near_w = wb; near_ao = t.r; }\n"
  "    }\n"
  "  return ws > 1.0e-3 ? acc / ws : near_ao;\n"
  "}\n"
  // The atlas is baked once from the same AABBs that stop a bullet. Guard
  // texels keep the filtered kernel inside each lamp's own projection.
  "float lamp_shadow(int id, vec3 delta, float radius){\n"
  "  if (uShadow < 0.5) return 1.0;\n"
  "  vec2 q = delta.xz / max(-delta.y, 0.01) / " GLSL_NUM(LAMP_SPREAD) " * 0.5 + 0.5;\n"
  "  q = clamp(q, vec2(0.0), vec2(1.0));\n"
  "  vec2 texel = 1.0 / vec2(" GLSL_NUM(LAMP_SH_W) ", " GLSL_NUM(LAMP_SH_H) ");\n"
  "  vec2 uv = (vec2(id % 3, id / 3) * float(" GLSL_NUM(LAMP_SH_TILE) ")\n"
  "          + 1.0 + q * float(" GLSL_NUM(LAMP_SH_SIZE) ")) * texel;\n"
  "  float dist = length(delta);\n"
  "  float depth = (dist - 0.08 - dist * 0.009) / radius;\n"
  "  vec2 px = texel * 0.65;\n"
  "  return (texture(uLampShadow, vec3(uv + vec2(px.x, px.y), depth))\n"
  "        + texture(uLampShadow, vec3(uv + vec2(-px.x, px.y), depth))\n"
  "        + texture(uLampShadow, vec3(uv + vec2(px.x, -px.y), depth))\n"
  "        + texture(uLampShadow, vec3(uv - px, depth))) * 0.25;\n"
  "}\n"
  "void main(){\n"
  // A glow's bloom weight comes from its own brightness: a tracer core and a
  // muzzle star spill, a dust mote (dim by construction) does not.
  // A mote against the SKY is a speck on the lens: the dust pass (and only
  // it, uSkyCut) asks the prepass depth under this pixel and fades the mote
  // out where nothing but sky is behind it. The prepass is single-sample, so
  // a mote on a wall's silhouette edge may flicker by one pixel; the motes
  // are a pixel and a half wide, nobody will see it.
  "  if (vMat.x > 1.5) {\n"
  "    oCol = vec4(tonemap(vCol), bloom_w(vCol)); return;\n"
  "  }\n"
  "  if (uEmis > 0.5) {\n"
  "    float gl = dot(vCol, vec3(0.2126, 0.7152, 0.0722));\n"
  "    float keep = 1.0;\n"
  "    if (uSkyCut > 0.5 &&\n"
  "        texelFetch(uPreDepth, ivec2(gl_FragCoord.xy), 0).r >= 0.99999) keep = 0.15;\n"
  "    oCol = vec4(vCol * keep, smoothstep(0.45, 1.1, gl) * 0.6 * keep);\n"
  "    return;\n"
  "  }\n"
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
  // A negative authored roughness is the zero-width wall-finish tag carried by
  // generated ruins. The BRDF always sees its magnitude.
  "  bool wall_finish = vMat.x < 0.0 && vMat.x > -1.0;\n"
  "  float base_rough = vMat.x < -1.0 ? 0.88 : abs(vMat.x);\n"
  "  float rough, surf_h = 0.5;\n"
  "  vec2 cp2 = abs(n.y) > 0.5 ? vWorld.xz\n"
  "           : (abs(n.x) > 0.5 ? vWorld.zy : vWorld.xy);\n"
  "  if (uDetail > 0.0) {\n"
  // Two smooth, low-frequency bands. A hard centimetre cell hash looked detailed
  // while standing still but became unfilterable noise as soon as the camera moved.
  "  float macro = vnoise2(cp2 * 0.38 + vec2(19.2, -7.4));\n"
  "  float meso = vnoise2(cp2 * 1.35 + vec2(-3.7, 11.8));\n"
  "  surf_h = mix(macro, meso, 0.34);\n"
  "  alb *= 1.0 + (macro - 0.5) * 0.090 * det\n"
  "             + (meso - 0.5) * 0.040 * det;\n"
  "  rough = clamp(base_rough + (meso - 0.5) * 0.085 * det, 0.045, 1.0);\n"
  "  } else rough = clamp(base_rough, 0.045, 1.0);\n"
  // --- QUIET WALL FINISH -------------------------------------------------
  // Ruin boxes are plastered or cast surfaces, including their top faces. The
  // wall tag adds only broad weathering and a dustier cap; construction detail
  // lives in the real 3D fracture modules instead of a repeating shader veneer.
  "  if (wall_finish && uDetail > 0.0) {\n"
  "    float plaster = vnoise2(cp2 * 0.62 + vec2(8.3, 27.1));\n"
  "    float weather = vnoise2(cp2 * 0.24 + vec2(-14.6, 5.9));\n"
  "    float cap = smoothstep(0.72, 0.98, abs(n.y));\n"
  "    alb *= 0.990 + (weather - 0.5) * 0.120 * det\n"
  "                 + (plaster - 0.5) * 0.055 * det + cap * 0.035;\n"
  "    rough = mix(rough, 0.88 + cap * 0.035\n"
  "                      + (plaster - 0.5) * 0.035, 0.42);\n"
  "    surf_h = mix(surf_h, mix(weather, plaster, 0.35), 0.36);\n"
  "  }\n"
  // PROCEDURAL RELIEF FROM THE MATERIAL FIELD ALREADY IN REGISTERS. The broad
  // concrete variation above is not only pigment: aggregate and worn render leave
  // shallow height changes that catch a grazing sun. Screen derivatives turn that
  // same scalar field into a surface gradient without another noise lookup. The
  // perturbation is restricted to upright world faces (sand and water have their own
  // normals below), and det fades it before the projected pattern becomes sub-pixel.
  "  if (uDetail > 0.0 && abs(n.y) < 0.82 && det > 0.04 && base_rough > 0.14) {\n"
  "    vec3 dpdx = dFdx(vWorld), dpdy = dFdy(vWorld);\n"
  "    vec3 r1 = cross(dpdy, n), r2 = cross(n, dpdx);\n"
  "    float hd = dot(dpdx, r1);\n"
  "    if (abs(hd) > 1.0e-8) {\n"
  "      vec3 sg = sign(hd) * (dFdx(surf_h) * r1 + dFdy(surf_h) * r2);\n"
  "      float relief = wall_finish ? 0.035 : 0.070;\n"
  "      n = normalize(abs(hd) * n - sg * (relief * det));\n"
  "    }\n"
  "  }\n"
  // ...and the metalness is its MAGNITUDE: the sign is the tool direction. See
  // mat_lathe. Nothing outside gun_build writes a negative, so the world is unchanged.
  "  float metal = abs(vMat.y), ao = vMat.z;\n"
  // TWO OCCLUSIONS, ONE READER EACH. `ao` is the authored ramp a vertex carries
  // (a crate's foot, a figure's crease) and it goes on partly applying to the
  // sun — a crease is a little darker in a beam. `aos` is what the screen
  // measured this frame — the metre of floor under a wall, the ground under a
  // boot, the corner two walls make — and it is INDIRECT light only: the sun
  // does not care that a wall stands a metre away, and the shadow map already
  // says when it does. So the screen term joins the vertex term in `aoi` for
  // the ambient, the environment, the sheen and the bounce, and stays out of
  // `sh`. The branch is uniform-valued per draw.
  "  float aos = 1.0;\n"
  "  if (uAoP.z > 0.5 && uVm < 0.5) aos = ao_screen(d);\n"
  "  float aoi = ao * aos;\n"
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
  "  if (uDetail > 0.0 && !wall_finish && abs(n.y) < 0.5) {\n"  // vertical faces: panel seams
  "    float sc = abs(n.x) > 0.5 ? vWorld.z : vWorld.x;\n"
  "    float sf = abs(fract(sc / 3.0) - 0.5) * 3.0;\n"
  "    float sm = 1.0 - smoothstep(0.02, 0.05, sf);\n"
  "    alb *= 1.0 - 0.075 * det * sm;\n"
  "    rough = mix(rough, 1.0, 0.5 * det * sm);\n"
  "  }\n"
  // --- THE FLOOR ----------------------------------------------------------
  // Half the screen is this surface, so it needs outdoor variation without a
  // seam grid that would turn the arena into a tiled hall.
  "  float wet = 0.0, glint = 0.0;\n"
  "  if (n.y > 0.95 && vWorld.y < 0.05) {\n"
  "    vec2 P = vWorld.xz;\n"
  // ONE sqrt, not two: sqrt is monotone and correctly rounded, so the max of the
  // lengths is the sqrt of the max of the squared lengths, bit for bit.
  "    vec2 dpx = dFdx(P), dpy = dFdy(P);\n"
  "    float fp = sqrt(max(dot(dpx, dpx), dot(dpy, dpy)));\n"
  // GATED ON THE THEME. surf_params returns uSurf.x == 0 for GRIT and WET, so only
  // loose SAND pays for its broad crust field. The field has no privileged
  // direction and no repeating crest profile.
  "    if (uSurf.x > 0.0) {\n"
  // --- LOOSE CRUST --------------------------------------------------------
  // Two deliberately incommensurate, isotropic bands form eroded patches rather
  // than lines: metre-scale colour underneath hand-scale compaction. Both remain
  // resolved into the middle distance, so their contrast fades by distance only.
  "      float loose = clamp((vMat.x - " GLSL_NUM(WORLD_GROUND_R) ") * 8.0 + 0.75,\n"
  "                          0.0, 1.0);\n"
  "      float crust0 = vnoise2(P * 0.47 + vec2(8.1, -3.7));\n"
  "      float crust1 = vnoise2(P * 2.35 - vec2(1.9, 6.4));\n"
  "      float crust = crust0 * 0.68 + crust1 * 0.32;\n"
  "      float crust_vis = (1.0 - smoothstep(0.18, 0.52, fp)) * loose * det;\n"
  "      alb *= 1.0 + (crust - 0.5) * 0.14 * crust_vis;\n"
  "      rough = clamp(rough + (0.54 - crust) * 0.13 * crust_vis\n"
  "                    + 0.045 * uSurf.x, 0.045, 1.0);\n"
  "    }\n"
  // --- AGGREGATE ----------------------------------------------------------
  // Every dry floor gets two size bands of mineral aggregate. Each band disappears
  // before its wavelength falls below a few pixels, avoiding the crawling grain a
  // fixed high-frequency texture produces. The fine band also supplies a shallow
  // height field; geometric specular AA below catches its remaining normal variance.
  "    float mvis = 1.0 - smoothstep(0.035, 0.16, fp);\n"
  "    float fvis = 1.0 - smoothstep(0.006, 0.038, fp);\n"
  "    float grit_m = 0.5;\n"
  "    vec3 grit_fd = vec3(0.5, 0.0, 0.0);\n"
  "    if (mvis > 0.004) grit_m = vnoise2(P * 6.7 + vec2(2.6, 11.2));\n"
  "    if (fvis > 0.004) grit_fd = vnoise2d(P * 27.0 - vec2(7.4, 3.1));\n"
  "    float grit = (grit_m - 0.5) * 0.105 * mvis\n"
  "               + (grit_fd.x - 0.5) * 0.095 * fvis;\n"
  "    alb *= 1.0 + grit * det;\n"
  "    rough = clamp(rough + grit * 0.62 * det, 0.045, 1.0);\n"
  "    float relief_vis = fvis * det;\n"
  "    if (relief_vis > 0.004) {\n"
  "      float ra = mix(0.00065, 0.00135, clamp(uSurf.x, 0.0, 1.0));\n"
  "      vec2 gg = grit_fd.yz * 27.0;\n"
  "      n = normalize(n - vec3(gg.x, 0.0, gg.y) * ra * relief_vis);\n"
  "    }\n"
  // Sparse pebbles are cell-contained (their centres stay away from every boundary),
  // so adjacent cells cannot clip a pebble in half. A second hash is evaluated only in
  // occupied cells and gives the chip an independent angle, aspect and material. The
  // edge is widened by the actual pixel footprint, then the whole signal fades out
  // before the pebble becomes sub-pixel. No temporal phase or camera-dependent input.
  "    float chip_vis = 1.0 - smoothstep(0.004, 0.030, fp);\n"
  "    if (chip_vis > 0.004 && uSurf.z < 0.01) {\n"
  "      const float chip_cs = 0.105;\n"
  "      vec2 chip_id = floor(P / chip_cs);\n"
  "      vec3 chip_r = vhash3(chip_id + vec2(19.7, 43.1));\n"
  "      if (chip_r.z > 0.83) {\n"
  "        vec3 chip_s = vhash3(chip_id - vec2(37.2, 13.9));\n"
  "        vec2 chip_p = fract(P / chip_cs) - mix(vec2(0.30), vec2(0.70), chip_r.xy);\n"
  "        float chip_ang = chip_s.x * 6.2831853;\n"
  "        vec2 chip_ax = vec2(cos(chip_ang), sin(chip_ang));\n"
  "        vec2 chip_q = vec2(dot(chip_p, chip_ax),\n"
  "                           dot(chip_p, vec2(-chip_ax.y, chip_ax.x)));\n"
  "        float chip_aspect = mix(0.76, 1.28, chip_s.y);\n"
  "        chip_q *= vec2(chip_aspect, 1.0 / chip_aspect);\n"
  "        float chip_rad = mix(0.075, 0.175, chip_s.z);\n"
  "        float chip_aa = clamp(fp / chip_cs, 0.012, 0.10);\n"
  "        float chip_dist = mix(length(chip_q),\n"
  "                              (abs(chip_q.x) + abs(chip_q.y)) * 0.76, 0.22);\n"
  "        float chip = (1.0 - smoothstep(chip_rad - chip_aa,\n"
  "                                      chip_rad + chip_aa, chip_dist))\n"
  "                   * chip_vis * det;\n"
  "        float chip_tone = mix(0.78, 1.10, chip_s.x);\n"
  "        alb *= mix(1.0, chip_tone, chip * 0.38);\n"
  "        rough = mix(rough, mix(0.62, 0.88, chip_s.y), chip * 0.42);\n"
  "        vec3 chip_n = normalize(n + vec3((chip_s.x - 0.5) * 0.20, 0.0,\n"
  "                                          (chip_s.y - 0.5) * 0.20));\n"
  "        n = normalize(mix(n, chip_n, chip * 0.34));\n"
  "      }\n"
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
  "      float pool = pool_mask(P, uSurf.z);\n"
  // THE FLOOR BETWEEN THE POOLS IS DAMP, and it did not read as anything: a
  // 0.86-rough concrete with a few dark pools in it is a dry floor with
  // stains. Wet stone is a broad glossy reflection of the sky at grazing
  // angles — the far half of the floor takes the horizon's brightness —
  // and a darker albedo close up. Patchy, from a broad noise, so the
  // sheen has drying edges rather than a uniform varnish; the water level
  // scales it, so DRAINED is dampest only where the pools were.
  "      float dmpn = vnoise2(P * 0.07 + vec2(7.3, 2.1)) * 0.6\n"
  "                 + vnoise2(P * 0.23 - vec2(1.7, 5.9)) * 0.4;\n"
  "      float dampf = (0.55 + 0.45 * smoothstep(0.30, 0.72, dmpn))\n"
  "                  * clamp(uSurf.z * 2.6, 0.0, 1.0);\n"
  "      rough = mix(rough, 0.28, dampf * (1.0 - pool));\n"
  "      alb *= mix(1.0, 0.70, dampf);\n"
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
  // Snow accumulates on upward surfaces; vertical concrete retains its
  // colder underlying palette. Low-frequency variation preserves quiet areas.
  "  if (uDetail > 0.5 && uBiome.y > 0.5) {\n"
  "    float cover = smoothstep(0.45, 0.92, n.y);\n"
  "    float powder = vnoise2(vWorld.xz * 0.72) * 0.06;\n"
  "    alb = mix(alb, vec3(0.76, 0.84, 0.88) + powder, cover * 0.82);\n"
  "    rough = mix(rough, 0.88, cover);\n"
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
  "  if (base_rough < 0.14 && metal < 0.5) {\n"
  "    F0 = vec3(0.004, 0.010, 0.006);\n"
  "    f90 = alb * 8.0;\n"
  "    diff = vec3(0.0);\n"
  "  }\n"
  "  float NoV = max(dot(n, V), 1e-4);\n"
  // Diffuse irradiance: the same sky, smoothed to a hemisphere lobe. Sun-facing AO is
  // only partly applied — a crease is dark in ambient light, not in a direct beam.
  // ...AND THE SKY IS NOT THE SAME ALL ROUND. The quarter of the sky the sun
  // is in is where the haze is bright (haze_rad's forward lobe), so a face
  // turned toward the sun's azimuth stands in more skylight than one turned
  // away — even in a wall's shadow. Without this the two shadowed faces of a
  // box are one value and the box is a flat cut-out; with it the shadow side
  // has a warm and a cool face, which is what makes it a box. uAmbS is the
  // sun colour through the dust, folded in C; the horizontal normal picks it.
  "  vec3 amb = mix(uAmbD, uAmbU, n.y * 0.5 + 0.5)\n"
  "           + uAmbS * max(dot(n, uSunH), 0.0);\n"
  "  vec3 irr = amb * aoi;\n"
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
  // ...AND AN EIGHTH OF THE SCREEN TERM REACHES THE SUN. Not physics — the shadow
  // map already says where the sun does not land — but the map's 1.2 cm texel
  // and its normal offset lose exactly the last centimetres of a contact, and
  // this is the cheapest way to hand them back: a crate's foot in full sun
  // shows a darker seam, a boot on the ground stands rather than floats.
  "  float sh = 0.0;\n"
  "  bool fig = uDetail < 0.5 && uVm < 0.5;\n"
  "  if (dot(n, SUN_DIR) > 0.0 || glint > 0.0 || wet > 0.0 || fig)\n"
  "    sh = mix(0.45, 1.0, ao) * mix(1.0, aos, 0.12) * sun_shadow();\n"
  // ...AND THE REFLECTION IS OCCLUDED HARDER THAN THE DIFFUSE. Lagarde's
  // specular-occlusion fit: a smooth surface seen at grazing angle reflects the
  // narrow cone of sky it can actually see, and in a corner that cone is
  // blocked before the diffuse hemisphere is. One pow, only when the screen
  // measured anything.
  "  float so = 1.0;\n"
  "  if (aos < 0.999)\n"
  "    so = clamp(pow(NoV + aos, exp2(-16.0 * rough - 1.0)) - 1.0 + aos, 0.0, 1.0);\n"
  "  vec3 env = env_gloss(reflect(-V, n), rough) * mix(1.0, ao, 0.75) * so;\n"
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
  "    env = mix(env, wenv * mix(1.0, aoi, 0.75) * mix(0.45, 1.0, sh), wet);\n"
  "  }\n"

  // Grazing-angle sheen.
  "  vec3 sheen = diff * rough * rough * pow(1.0 - NoV, 4.0) * 0.85 * amb;\n"
  // A SKY RIM ON THE FIGURES ONLY (the scene draw: no world detail, not the
  // viewmodel). Independent of the albedo, because the cloth is dark and a
  // rim that is diff-scaled is no rim: a body against a wall in its own colour
  // needs its edge lit by the sky behind it to read as a body. Kept small —
  // the silhouettes are the art direction and this may only draw their edge.
  // ...and the SUN'S rim when it stands behind the figure: cloth and hair
  // scatter light through their edge, which is why a backlit soldier is a
  // dark shape with a burning outline and not a black hole. Shadowed like
  // any other sun term (sh is evaluated for every figure pixel).
  "  if (fig) {\n"
  "    float rf = 1.0 - NoV; rf *= rf; rf *= rf;\n"
  "    sheen += amb * rf * 0.45 * (1.0 - metal);\n"
  "    float back = max(dot(-V, SUN_DIR), 0.0);\n"
  "    sheen += SUN_COL * sh * (rf * (1.0 - NoV)) * back * back * 0.30 * (1.0 - metal);\n"
  "  }\n"
  // Bounce off the floor: down-facing surfaces near the ground pick up warm light
  // reflected off it.
  "  float bnc = 0.0;\n"
  "  if (n.y < 0.0) bnc = -n.y * exp(-max(vWorld.y, 0.0) * 0.85);\n"
  // ...AND SIDEWAYS, which is what the down-facing version was missing.
  "  float bncs = (1.0 - abs(n.y)) * exp(-max(vWorld.y, 0.0) * 2.2);\n"
  // ...AND THE SUN OFF THE FLOOR. uBounce is the sky's ground colour, which is
  // what a shadowed floor gives back; a SUNLIT floor gives back the sun times
  // its own albedo, and half of a wall's hemisphere is floor. Without it the
  // face of a box turned from the sun was the darkest thing in the frame at
  // noon, and a figure's back a cut-out. Fades with height like the rest —
  // a cap 4 m up sees less floor than a boot does.
  "  vec3 bounce = diff * ((bnc + bncs * 0.55) * uBounce\n"
  "              + (1.0 - abs(n.y)) * exp(-max(vWorld.y, 0.0) * 0.30) * uBounceS)\n"
  "              * mix(1.0, aos, 0.6);\n"
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
  "           + sheen * mix(0.55, 1.0, aoi) + bounce;\n"
  // Six bounded spotlights, with smooth cone and distance falloff. The
  // viewmodel has a separate projection, so its local lamp sample uses the eye.
  "  for (int i = 0; i < uLampN; i++) {\n"
  "    vec3 lp = uVm > 0.5 ? uEye : vWorld;\n"
  "    vec3 delta = lp - uLampPos[i].xyz;\n"
  "    float d2 = dot(delta, delta), radius = uLampPos[i].w;\n"
  "    if (d2 >= radius * radius || delta.y >= -0.04) continue;\n"
  "    float ld = sqrt(max(d2, 0.001));\n"
  "    vec3 L = -delta / ld;\n"
  "    float cone = smoothstep(0.42, 0.69, L.y);\n"
  "    if (cone <= 0.001) continue;\n"
  "    float fall = 1.0 - smoothstep(radius * 0.50, radius, ld);\n"
  "    float shadow = lamp_shadow(i, delta, radius);\n"
  "    vec3 rad = uLampCol[i] * (fall * cone * shadow / (1.0 + d2 * 0.045));\n"
  "    col += lit(n, V, L, diff, F0, f90, rough, rad);\n"
  "  }\n"
  // The glitter rides in AS A LIGHT, after the shadow term and scaled by it: a
  // spark in a wall's shadow is a spark with no sun on it, and adding this
  // before `sh` was the difference between mica and fairy lights.
  "  if (glint > 0.0) col += SUN_COL * glint * sh * 0.44;\n"
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
  "  oCol = vec4(tonemap(col), bloom_w(col));\n"
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
  "uniform float uSkyTime;\n"
  // A hemisphere projection has no longitude seam or polar singularity. Each
  // cell owns at most one star; its footprint softens subpixel points without
  // camera-space noise or a full-screen search over neighboring cells.
  "vec3 night_stars(vec3 d){\n"
  "  vec2 p = d.xz / (1.0 + max(d.y, 0.0)) * 190.0;\n"
  "  vec3 rnd = vhash3(floor(p));\n"
  "  vec2 q = fract(p) - mix(vec2(0.20), vec2(0.80), rnd.xy);\n"
  "  float footprint = max(length(fwidth(p)), 0.015);\n"
  "  float radius = 0.035 + rnd.x * rnd.x * 0.09;\n"
  "  float aa = footprint * 0.55;\n"
  "  float point = 1.0 - smoothstep(max(radius - aa, 0.0), radius + aa, length(q));\n"
  "  point *= min(1.0, radius / max(aa, 0.001));\n"
  "  float bright = smoothstep(0.984, 1.0, rnd.z);\n"
  "  bright *= bright * (0.28 + rnd.y * 1.05);\n"
  "  vec3 tint = mix(vec3(0.59, 0.73, 1.0), vec3(1.0, 0.85, 0.64), rnd.x * rnd.x);\n"
  "  return tint * (point * bright * smoothstep(0.035, 0.24, d.y));\n"
  "}\n"
  // Two thin curtains on the northern hemisphere. Low-frequency bends and
  // vertical striations share one slowly moving field; the lower hem and upper
  // falloff both fade smoothly. The bounded analytic bands need no raymarch.
  "vec3 night_aurora(vec3 d){\n"
  "  float x = atan(d.x, d.z), y = asin(clamp(d.y, 0.0, 1.0));\n"
  "  float tm = uSkyTime * 0.035;\n"
  "  float window = smoothstep(-1.45, -0.85, x) * (1.0 - smoothstep(0.82, 1.50, x));\n"
  "  vec3 glow = vec3(0.0);\n"
  "  for (int i = 0; i < 2; ++i) {\n"
  "    float layer = float(i), xx = x + layer * 0.48;\n"
  "    float bend = 0.29 + layer * 0.19 + sin(xx * 2.3 + tm) * 0.075\n"
  "               + sin(xx * 5.8 - tm * 0.6) * 0.023;\n"
  "    float h = y - bend;\n"
  "    float thread = vnoise2(vec2(xx * 43.0 + tm * 0.6, layer * 8.1 + tm * 0.12));\n"
  "    float billow = vnoise2(vec2(xx * 5.5 - tm * 0.25, layer * 3.7 + tm * 0.1));\n"
  "    h += (thread - 0.5) * 0.018;\n"
  "    float curtain = smoothstep(-0.050, 0.024, h) * exp(-max(h, 0.0) * 13.0);\n"
  "    curtain *= (0.58 + thread * thread * 0.42) * smoothstep(0.10, 0.78, billow);\n"
  "    vec3 tint = mix(vec3(0.024, 0.17, 0.105), vec3(0.07, 0.065, 0.17),\n"
  "                    smoothstep(0.07, 0.26, h) * 0.75);\n"
  "    glow += tint * curtain * (1.0 - layer * 0.62);\n"
  "  }\n"
  "  return glow * window * smoothstep(0.09, 0.22, d.y);\n"
  "}\n"
  // A finite lunar disc keeps the daylight sun's glare out of night skies.
  // Mare texture and a directional terminator give it a shaded surface.
  "vec3 night_moon(vec3 d){\n"
  "  float alignment = dot(d, SUN_DIR);\n"
  "  if (alignment < 0.9996) return vec3(0.0);\n"
  "  vec3 right = normalize(cross(SUN_DIR, vec3(0.0, 1.0, 0.0)));\n"
  "  vec3 up = cross(right, SUN_DIR);\n"
  "  vec2 q = vec2(dot(d, right), dot(d, up)) / 0.008;\n"
  "  float r = length(q), aa = max(fwidth(r), 0.01);\n"
  "  float disc = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, r);\n"
  "  vec3 normal = vec3(q, sqrt(max(1.0 - dot(q, q), 0.0)));\n"
  "  float phase = max(dot(normal, normalize(vec3(-0.48, 0.12, 0.86))), 0.0);\n"
  "  float mare = vnoise2(q * 3.8 + vec2(4.3, 7.9)) * 0.7\n"
  "             + vnoise2(q * 8.1 + vec2(1.2, 9.1)) * 0.3;\n"
  "  return vec3(0.70, 0.80, 0.95) * disc * (0.10 + phase * 0.90) * (0.70 + mare * 0.30);\n"
  "}\n"
  // The sun's heading, normalized once on the CPU: it is a uniform, so doing it
  // per pixel is a rsqrt for a value that cannot change within the draw.
  "uniform vec2 uSunXZ;\n"
  // BANDS, NOT BLOBS. A deck sampled on an isotropic grid is cotton wool; what
  // a sky actually shows is the wind, drawn out along it. The lookup runs in
  // the WIND'S frame and is squashed 4:1 across it, so the streaks lie along
  // the same heading that drags the drifts on the lee of every wall — one wind
  // shared by the cloud deck and ground deposits.
  "float deck(vec2 p){\n"
  "  vec2 q0 = vec2(dot(p, uWindD), dot(p, vec2(-uWindD.y, uWindD.x)));\n"
  "  q0 = vec2(q0.x * 0.52, q0.y * 1.28);\n"
  "  float w = vnoise2(q0 * 0.55) - 0.5;\n"
  "  vec2 q = q0 + vec2(w, -w * 0.6) * 1.15;\n"
  "  return vnoise2(q) * 0.62 + vnoise2(q * 2.31) * 0.38;\n"
  "}\n"
  // THE LAND BEYOND THE WALL. The arena used to end at its perimeter with
  // nothing behind it but gradient: a box on a plane in a void. This is a
  // ridge line a few kilometres out — a height field of the HEADING, so it is
  // periodic by construction (the lookup runs on the unit circle, never on an
  // angle that wraps) — in two layers, the far one paler than the near one,
  // both nearly the colour of the haze they stand in. It hides the sun disc
  // when the sun is behind it, which the aureole then sits over, and that is
  // the whole reason a sunset looks like a place. uRidge is theme state
  // (ridge_params): dunes, hills or mesas.
  "uniform vec4 uRidge;\n"
  "float ridge_h(vec2 hd, float k, float seed){\n"
  "  float n = vnoise2(hd * k + seed) * 0.62 + vnoise2(hd * k * 2.7 + seed * 1.7) * 0.38;\n"
  "  float r = 1.0 - abs(n * 2.0 - 1.0);\n"   // ridged: sharp crests
  "  return mix(n, r * r, uRidge.z);\n"
  "}\n"
  "void main(){\n"
  "  vec3 dir = normalize(uFwd + uRight * (vNdc.x * uTan.x) + uUp * (vNdc.y * uTan.y));\n"
  "  vec3 c = sky_env(dir, 1.0 - uBiome.x);\n"
  "  float up = dir.y;\n"
  "  if (uBiome.x > 0.01 && up > 0.0) {\n"
  "    c += uBiome.x * (night_stars(dir) + night_moon(dir));\n"
  "    if (uBiome.w > 0.01) c += night_aurora(dir) * uBiome.w;\n"
  "  }\n"
  "  if (up < 0.12 && uRidge.y > 0.0) {\n"
  "    vec2 hd = normalize(dir.xz);\n"
  "    float e = max(fwidth(up), 1.0e-5);\n"
  // The haze this land stands in: the sky at the horizon in this heading,
  // disc off (the disc is what the ridge hides). Two layers: the far one is
  // the haze barely darkened, the near one darker and lower.
  "    vec3 hz = sky_env(vec3(hd.x, 0.02, hd.y), 0.0);\n"
  "    float hf = uRidge.x + uRidge.y * 1.35 * ridge_h(hd, 2.1, 3.7);\n"
  "    float mf = smoothstep(hf + e, hf - e, up);\n"
  "    c = mix(c, hz * (1.0 - uRidge.w * 0.45), mf);\n"
  "    float hn = uRidge.x * 0.5 + uRidge.y * ridge_h(hd, 3.3, 11.9);\n"
  "    float mn = smoothstep(hn + e, hn - e, up);\n"
  "    c = mix(c, hz * (1.0 - uRidge.w), mn);\n"
  "  }\n"
  // 0.012, not 0.03: at a low sun the interesting deck is exactly the part
  // lying ON the horizon, and the old cut-in threw that band away and started
  // the clouds a hand's width above the one place they had to be.
  "  if (up > 0.012) {\n"
  "    float t = min(1.0 / max(up, 0.05), 16.0);\n"
  "    vec2 p = dir.xz * t * 0.86 + uWindD * (uSkyTime * 0.004);\n"
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
  // ...AND THE RIM IS A RIM, NOT A RING. At 2.6 the edge term outshone the
  // body it belonged to whenever the sun stood behind the deck, so every
  // band toward a low sun drew as a bright loop around a dark hole — the
  // "doughnuts" of the sunset frames. Half of that energy now goes into the
  // body as forward scatter (a thin cloud in front of the sun glows through),
  // the rest stays on the edge, and a band reads as a lit band.
  "      float s2 = max(dot(dir, SUN_DIR), 0.0);\n"
  "      float fwd = pow(s2, 6.0);\n"
  "      cl += SUN_COL * fwd * (dens * (1.0 - dens) * 1.1 + dens * 0.55);\n"
  "      c = mix(c, cl, dens * 0.86);\n"
  "    }\n"
  "  }\n"
  "  oCol = vec4(tonemap(c), bloom_w(c));\n"
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

// SCREEN-SPACE AMBIENT OBSCURANCE, McGuire's estimator (SAO, 2012), at half
// resolution over the prepass. Twelve samples on a fixed golden-angle spiral whose
// radius is a WORLD radius projected — so a crate's foot is occluded the same
// amount from across the arena as from a metre away. A per-pixel rotation made
// the half-resolution result look like square grain in corners and crawl during
// camera motion; the blur has a stable pattern to integrate now. Positions come
// back through the inverse view-projection
// rather than through a view-space reconstruction: the normals are stored in
// WORLD space, the estimator is a dot product and a distance, and one matrix
// per sample at a quarter of the pixels is cheaper than getting a second frame
// of reference right.
// Each sample's weight is (r^2 - |v|^2)^3 * max(0, v.n - bias) / (|v|^2 + eps):
// the cube is a smooth cutoff at the radius (no ring where samples stop
// counting), the dot is "how much of my hemisphere does this point cover", the
// division is the falloff. The bias grows with view depth because the depth
// buffer's own quantization does: without it a flat wall twenty metres off
// shades itself.
// .g carries the surface's distance from the eye so the blur and the world
// pass's upsample can tell which surface an occlusion value belongs to. The
// sky writes 1.0 at a distance nothing else reaches.
static const char *SSAO_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uDepth;\n"
  "uniform sampler2D uNrm;\n"
  "uniform mat4 uInvVP;\n"
  "uniform vec3 uEye;\n"
  "uniform vec4 uScr;\n"   // xy = this target's size, zw = 1 / size
  "uniform vec4 uZ;\n"     // x near, y far, z = texels per metre at 1 m, w = radius (m)
  "uniform float uAoI;\n"  // intensity
  "float lin_z(float d){\n"
  "  float z = d * 2.0 - 1.0;\n"
  "  return 2.0 * uZ.x * uZ.y / (uZ.y + uZ.x - z * (uZ.y - uZ.x));\n"
  "}\n"
  "vec3 world_at(vec2 uv, float d){\n"
  "  vec4 p = uInvVP * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
  "  return p.xyz / p.w;\n"
  "}\n"
  "void main(){\n"
  "  float d0 = texture(uDepth, vUV).r;\n"
  "  if (d0 >= 0.99999) { oCol = vec4(1.0, 1.0e4, 0.0, 1.0); return; }\n"
  "  vec3 P = world_at(vUV, d0);\n"
  "  float z0 = lin_z(d0);\n"
  "  vec3 n = normalize(texture(uNrm, vUV).rgb * 2.0 - 1.0);\n"
  "  float rpx = clamp(uZ.z * uZ.w / z0, 1.5, 80.0);\n"
  "  float rr = uZ.w * uZ.w;\n"
  "  float bias = 0.015 + 0.004 * z0;\n"
  "  float occ = 0.0;\n"
  "  for (int i = 0; i < 12; i++) {\n"
  "    float a = (float(i) + 0.5) * (1.0 / 12.0);\n"
  "    float ang = float(i) * 2.3999632 + 0.731;\n"
  "    vec2 suv = vUV + vec2(cos(ang), sin(ang)) * (sqrt(a) * rpx) * uScr.zw;\n"
  "    float d = texture(uDepth, suv).r;\n"
  "    if (d >= 0.99999) continue;\n"
  "    vec3 v = world_at(suv, d) - P;\n"
  "    float vv = dot(v, v);\n"
  "    float f = max(rr - vv, 0.0);\n"
  "    occ += f * f * f * max(0.0, dot(v, n) - bias) / (vv + 0.02);\n"
  "  }\n"
  "  float ao = max(0.0, 1.0 - occ * uAoI * (5.0 / 12.0) / (rr * rr * rr));\n"
  "  oCol = vec4(ao, length(P - uEye), 0.0, 1.0);\n"
  "}\n";

// One axis of the depth-aware blur over the AO target (run twice, x then y).
// Nine taps, Gaussian-ish; a tap only counts if it was measured on a surface
// at about this one's distance, so a crate's contact shadow does not bleed
// onto the wall behind it and the sky (distance 1e4) never averages into
// anything. The width is what removes the sampler's per-pixel rotation
// pattern: at half resolution nine taps span a hand's width of screen.
static const char *AOBLUR_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uTex; uniform vec2 uPix;\n"
  "void main(){\n"
  "  vec2 c = texture(uTex, vUV).rg;\n"
  "  float acc = c.r, ws = 1.0;\n"
  "  for (int i = 1; i <= 4; i++) {\n"
  "    float k = i == 1 ? 0.88 : (i == 2 ? 0.62 : (i == 3 ? 0.35 : 0.15));\n"
  "    vec2 a = texture(uTex, vUV + uPix * float(i)).rg;\n"
  "    vec2 b = texture(uTex, vUV - uPix * float(i)).rg;\n"
  "    float wa = k / (1.0 + 120.0 * abs(a.g - c.g) / c.g);\n"
  "    float wb = k / (1.0 + 120.0 * abs(b.g - c.g) / c.g);\n"
  "    acc += a.r * wa + b.r * wb; ws += wa + wb;\n"
  "  }\n"
  "  oCol = vec4(acc / ws, c.g, 0.0, 1.0);\n"
  "}\n";

// SHADOWED VOLUMETRIC SUNLIGHT at quarter resolution. Each pixel reconstructs
// its camera ray from the prepass depth and integrates the arena's height-profiled
// dust only as far as the first opaque surface. Every step asks the SAME sun shadow
// map as WORLD_FS, so walls and figures carve light shafts out of the medium instead
// of a radial screen blur guessing where an occluder might be. Twelve single-tap
// comparisons at quarter resolution are three quarters of one shadow lookup per
// full-resolution pixel; the existing Kawase pair filters the deterministic spatial
// jitter and softens the beam edges before the composite.
static const char *VOLUME_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uDepth;\n"
  "uniform sampler2DShadow uShadow;\n"
  "uniform mat4 uInvVP, uLightVP;\n"
  "uniform vec3 uEye, uSunDir, uSunCol, uAtmoTint;\n"
  // x = medium strength, y = shadow-map gate, z = exposure, w = haze density.
  "uniform vec4 uVol;\n"
  "vec3 world_at(float d){\n"
  "  vec4 p = uInvVP * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);\n"
  "  return p.xyz / p.w;\n"
  "}\n"
  "float shadow_at(vec3 p){\n"
  "  if (uVol.y < 0.5) return 1.0;\n"
  "  vec4 q = uLightVP * vec4(p, 1.0);\n"
  "  vec3 s = q.xyz / q.w * 0.5 + 0.5;\n"
  "  if (s.z <= 0.0 || s.z >= 1.0) return 1.0;\n"
  // A small receiver bias is enough for volume samples: unlike a surface they
  // cannot self-shadow, and the bilinear comparison already supplies a soft edge.
  "  return texture(uShadow, vec3(s.xy, s.z - 0.00020));\n"
  "}\n"
  "void main(){\n"
  "  if (uVol.x <= 0.0) { oCol = vec4(0.0); return; }\n"
  "  float z = texture(uDepth, vUV).r;\n"
  "  vec3 ray = world_at(z >= 0.99999 ? 1.0 : z) - uEye;\n"
  "  float raw_len = length(ray);\n"
  "  float ray_len = min(raw_len, 72.0);\n"
  "  if (ray_len < 0.18) { oCol = vec4(0.0); return; }\n"
  "  vec3 rd = ray / raw_len;\n"
  // Schlick's rational fit to a forward-scattering dust phase function. It has
  // the strong sun-facing peak that makes shafts read, without a sqrt per sample.
  "  float mu = clamp(dot(rd, uSunDir), -1.0, 1.0);\n"
  "  float k = 0.792, den = 1.0 - k * mu;\n"
  "  float phase = 0.0293337 / max(den * den, 1.0e-4);\n"
  // Stable screen-space jitter breaks twelve hard depth bands; the following
  // quarter-resolution blur integrates it, while deterministic captures stay exact.
  "  vec3 h = fract(vec3(gl_FragCoord.xy, gl_FragCoord.x + 19.1) * 0.1031);\n"
  "  h += dot(h, h.yzx + 33.33);\n"
  "  float jitter = fract((h.x + h.y) * h.z);\n"
  "  float step_len = ray_len * (1.0 / 12.0);\n"
  "  float optical = 0.0, lit_depth = 0.0;\n"
  "  for (int i = 0; i < 12; i++) {\n"
  "    float t = (float(i) + 0.25 + jitter * 0.50) * step_len;\n"
  "    vec3 p = uEye + rd * t;\n"
  // Second-order reciprocal approximation to exp(-height / 9 m): monotone,
  // close over the arena's 0..5 m range and no transcendental inside the loop.
  "    float yh = max(p.y, 0.0) * 0.1111;\n"
  "    float density = 1.0 / (1.0 + yh * (1.0 + 0.50 * yh));\n"
  "    optical += density * step_len;\n"
  "    float trans = 1.0 / (1.0 + optical * uVol.w * 0.020);\n"
  "    lit_depth += shadow_at(p) * density * trans * step_len;\n"
  "  }\n"
  // The result joins a display-referred composite, so apply the same exposure and
  // a compact shoulder here. sqrt is the renderer's gamma-2 display encoding.
  "  vec3 c = uSunCol * uAtmoTint\n"
  "         * (lit_depth * uVol.w * phase * uVol.x * 0.0065 * uVol.z);\n"
  "  c = sqrt(clamp(c / (1.0 + c), 0.0, 1.0));\n"
  "  oCol = vec4(c, 1.0);\n"
  "}\n";

// THE BLOOM CHAIN — progressive, the Call of Duty: Advanced Warfare
// construction (Jimenez 2014): a 13-tap downsample from each level to the
// next (four levels from a quarter of the frame down to a thirty-second),
// then a 9-tap tent upsample added back up the chain. One wide Gaussian-ish
// halo built from four octaves, so a sun disc gets a tight core AND a wide
// glow, and a tracer a soft streak rather than a square smudge. The two
// Kawase taps this replaced were one scale and pulsed on small bright things.
// The source is scene.rgb * scene.a — the bloom weight the lit passes wrote
// (bloom_w) — so what spills is decided by scene luminance, not by display
// luminance.
// The first level takes the KARIS AVERAGE: each of the four 2x2 quadrants is
// weighted by 1/(1+luma), so one blazing pixel (a specular pinprick, a tracer
// end) cannot own the whole level and flicker as the camera moves.
static const char *BLOOM_DOWN_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uTex; uniform vec2 uPix;\n"   // uPix = one SOURCE texel
  "uniform float uFirst;\n"                        // 1 = apply alpha weight + Karis
  "vec3 T(vec2 o){\n"
  "  vec4 s = texture(uTex, vUV + o * uPix);\n"
  "  return uFirst > 0.5 ? s.rgb * s.a : s.rgb;\n"
  "}\n"
  "float kw(vec3 c){ return 1.0 / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722))); }\n"
  "void main(){\n"
  "  vec3 a = T(vec2(-2.0, 2.0)), b = T(vec2(0.0, 2.0)), c = T(vec2(2.0, 2.0));\n"
  "  vec3 d = T(vec2(-2.0, 0.0)), e = T(vec2(0.0, 0.0)), f = T(vec2(2.0, 0.0));\n"
  "  vec3 g = T(vec2(-2.0, -2.0)), h = T(vec2(0.0, -2.0)), i = T(vec2(2.0, -2.0));\n"
  "  vec3 j = T(vec2(-1.0, 1.0)), k = T(vec2(1.0, 1.0));\n"
  "  vec3 l = T(vec2(-1.0, -1.0)), m = T(vec2(1.0, -1.0));\n"
  "  vec3 o;\n"
  "  if (uFirst > 0.5) {\n"
  "    vec3 g0 = (a + b + d + e) * 0.25, g1 = (b + c + e + f) * 0.25;\n"
  "    vec3 g2 = (d + e + g + h) * 0.25, g3 = (e + f + h + i) * 0.25;\n"
  "    vec3 g4 = (j + k + l + m) * 0.25;\n"
  "    float w0 = kw(g0), w1 = kw(g1), w2 = kw(g2), w3 = kw(g3), w4 = kw(g4);\n"
  "    o = (g4 * w4 * 0.5 + (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3) * 0.125)\n"
  "      / (w4 * 0.5 + (w0 + w1 + w2 + w3) * 0.125);\n"
  "  } else {\n"
  "    o = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625\n"
  "      + (j + k + l + m) * 0.125;\n"
  "  }\n"
  "  oCol = vec4(o, 1.0);\n"
  "}\n";
static const char *BLOOM_UP_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uTex; uniform vec2 uPix;\n"   // uPix = one SOURCE texel
  "vec3 T(vec2 o){ return texture(uTex, vUV + o * uPix).rgb; }\n"
  "void main(){\n"
  "  vec3 o = T(vec2(0.0)) * 4.0\n"
  "         + (T(vec2(-1.0, 0.0)) + T(vec2(1.0, 0.0)) + T(vec2(0.0, -1.0)) + T(vec2(0.0, 1.0))) * 2.0\n"
  "         + T(vec2(-1.0, -1.0)) + T(vec2(1.0, -1.0)) + T(vec2(-1.0, 1.0)) + T(vec2(1.0, 1.0));\n"
  "  oCol = vec4(o * (1.0 / 16.0), 1.0);\n"
  "}\n";

// GLARE — the light shafts and the composite. The shafts are a march toward
// the sun through the bright pass at quarter resolution, softened by the two
// dual-Kawase taps the frosted menu backdrop already owned; the bloom is the
// chain above, and the composite adds both to the scene.
//
// THE OCCLUSION IS FREE AND EXACT. A shaft is a march toward the sun through the
// bright-pass, so a wall between this pixel and the sun contributes nothing and
// the shaft simply stops at its edge — no shadow lookup, no geometry, no list of
// occluders that can disagree with the one the depth pass drew.
static const char *GLARE_FS =
  "#version 330 core\n"
  "in vec2 vUV; out vec4 oCol;\n"
  "uniform sampler2D uTex;\n"
  "uniform sampler2D uScene;\n" // the finished picture (unit 1), composite only
  "uniform sampler2D uBloom;\n" // the bloom chain's finest level (unit 2)
  "uniform vec3 uSunUV;\n"   // xy = the sun's place on screen, z = shaft strength
  "uniform vec2 uKnee;\n"    // luminance knee: what counts as bright (shafts)
  "uniform vec3 uGain;\n"    // x bloom, y shafts, z composite
  "uniform vec4 uGrade;\n"   // vignette, grain, grain seed, aspect
  "uniform float uMode;\n"   // 0 = build the glare, 1 = add it back
  "uniform vec3 uAA;\n"      // x = 1: FXAA the scene read (MSAA off); yz = one scene texel
  "const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);\n"
  // FXAA (Lottes, the nine-tap console form) on the finished picture when
  // the multisample target is off: the flat facets and long straight
  // silhouettes of this world are exactly what crawls without it. Runs
  // inside the composite so the frame still costs one full-screen pass.
  "vec3 fxaa(vec2 uv){\n"
  "  vec2 px = uAA.yz;\n"
  "  vec3 nw = texture(uScene, uv + vec2(-1.0, -1.0) * px).rgb;\n"
  "  vec3 ne = texture(uScene, uv + vec2( 1.0, -1.0) * px).rgb;\n"
  "  vec3 sw = texture(uScene, uv + vec2(-1.0,  1.0) * px).rgb;\n"
  "  vec3 se = texture(uScene, uv + vec2( 1.0,  1.0) * px).rgb;\n"
  "  vec3 m  = texture(uScene, uv).rgb;\n"
  "  float lnw = dot(nw, LUMA), lne = dot(ne, LUMA), lsw = dot(sw, LUMA);\n"
  "  float lse = dot(se, LUMA), lm = dot(m, LUMA);\n"
  "  float lmin = min(lm, min(min(lnw, lne), min(lsw, lse)));\n"
  "  float lmax = max(lm, max(max(lnw, lne), max(lsw, lse)));\n"
  "  if (lmax - lmin < max(0.03, lmax * 0.125)) return m;\n"
  "  vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));\n"
  "  float red = max((lnw + lne + lsw + lse) * 0.03125, 0.0078125);\n"
  "  float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);\n"
  "  dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * px;\n"
  "  vec3 a = 0.5 * (texture(uScene, uv + dir * (1.0 / 3.0 - 0.5)).rgb\n"
  "                + texture(uScene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n"
  "  vec3 b = a * 0.5 + 0.25 * (texture(uScene, uv - dir * 0.5).rgb\n"
  "                           + texture(uScene, uv + dir * 0.5).rgb);\n"
  "  float lb = dot(b, LUMA);\n"
  "  return (lb < lmin || lb > lmax) ? a : b;\n"
  "}\n"
  "void main(){\n"
  // --- THE COMPOSITE, AND IT IS ALSO THE GRADE ---------------------------
  // The finished picture lives in a texture now (the scene target), so this is
  // a plain full-screen pass that READS it and writes the present buffer: the
  // glare adds, the vignette and the grain's dark half multiply, and none of
  // it has to be smuggled through a blend equation. The arithmetic is exactly
  // the old ONE/SRC_ALPHA composite's — base * mul + add — so a frame with the
  // screen-space terms off is the frame this pass used to blend.
  // Mode 2: the harness's `dbgview ao` — the occlusion buffer as the picture.
  "  if (uMode > 3.5) { oCol = vec4(vec3(texture(uScene, vUV).a), 1.0); return; }\n"
  "  if (uMode > 2.5) { oCol = vec4(texture(uScene, vUV).rgb * 2.0, 1.0); return; }\n"
  "  if (uMode > 1.5) { oCol = vec4(vec3(texture(uScene, vUV).r), 1.0); return; }\n"
  "  if (uMode > 0.5) {\n"
  "    vec2 q = (vUV - 0.5) * vec2(uGrade.w, 1.0);\n"
  "    float r2 = dot(q, q) * 1.30;\n"
  // Interleaved-gradient noise: three instructions, and unlike a value-noise
  // hash it has no visible cell structure at one sample per pixel. The seed is
  // driven by the SIM TICK and not by the rendered-frame counter — grain that
  // moves with the render clock would make every harness screenshot depend on
  // how many frames preceded it, which is the same trap the menu caret's lack
  // of a blink documents.
  // Hash noise, not interleaved-gradient: IGN's diagonal lattice is exactly
  // the structure a grain must not have, and at 2x it read as a screen door
  // over every flat face. Three fract-multiplies, no cell structure.
  "    vec3 gq = fract(vec3(gl_FragCoord.xy + uGrade.z, gl_FragCoord.x) * 0.1031);\n"
  "    gq += dot(gq, gq.yzx + 33.33);\n"
  "    float g = fract((gq.x + gq.y) * gq.z);\n"
  "    float gn = (g - 0.5) * uGrade.y;\n"
  // The vignette starts outside the middle third and never takes more than a
  // fifth: it was r2^2 at 0.34 and took 40-60% off the corners, which read as
  // a lens hood on every mood and was most of why the frames looked grey.
  "    float vig = 1.0 - uGrade.x * smoothstep(0.35, 1.6, r2);\n"
  // The edges go cool as well as dark. A vignette that only darkens reads as a
  // lens fault; one that also loses a little warmth reads as the falloff of a
  // real lens, and it costs an add. FILM GRAIN OFF zeroes uGrade.x, and this
  // switch turns the cool edge off with it: the tint is the vignette's other
  // half, and a corner that is blue but not dark is that same lens fault. One
  // step, so the shipped look multiplies by exactly one.
  "    float vigk = step(1e-6, uGrade.x);\n"
  // GLOW OFF zeroes both gains and the two glare targets hold last frame's
  // picture — an unreadable result, so the fetches are not made.
  "    vec3 add = (uGain.z > 0.0 ? texture(uTex, vUV).rgb * uGain.z : vec3(0.0))\n"
  "             + (uGain.x > 0.0 ? texture(uBloom, vUV).rgb * uGain.x : vec3(0.0))\n"
  "             + max(gn, 0.0) * 0.42\n"
  "             + vec3(-0.008, -0.002, 0.010) * r2 * vigk;\n"
  "    float mul = clamp(vig * (1.0 + min(gn, 0.0)), 0.0, 1.0);\n"
  "    vec3 base = uAA.x > 0.5 ? fxaa(vUV) : texture(uScene, vUV).rgb;\n"
  "    oCol = vec4(base * mul + add, 1.0);\n"
  "    return;\n"
  "  }\n"
  // A LUMINANCE KNEE, NOT A SUBTRACTION. Subtracting a threshold from each
  // channel shifts the HUE of everything that glows — a warm 0.9 highlight comes
  // out orange-red — and the one thing this pass must not do is recolour the
  // sunset it is there to sell. Scaling by a smoothstep on the luminance keeps
  // the colour and only decides how much of it spills.
  // The bright pass is the scene's own bloom weight (alpha, see bloom_w) —
  // the display knee it replaced could not tell a sunlit wall from the sun.
  "  vec4 s0 = texture(uTex, vUV);\n"
  "  vec3 core = s0.rgb * s0.a;\n"
  "  vec3 acc = vec3(0.0);\n"
  "  if (uSunUV.z > 0.0) {\n"
  "    vec2 d = (uSunUV.xy - vUV) * (0.85 / 10.0);\n"
  "    vec2 uv = vUV;\n"
  "    float w = 1.0;\n"
  "    for (int i = 0; i < 10; i++) {\n"
  "      vec4 s = texture(uTex, uv);\n"
  "      acc += s.rgb * s.a * w;\n"
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
