// =============================================================================
// world/map.inc  —  arena generator: map_generate, sun_build, mood_build,
// sig_build, wall damage (breaks/breaches/crenellation/rubble).
// Proof: map, sun, mood, mapcheck. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: the sun and the mood are MAP STATE, hashed off the seed and
// never drawn from g_rng; mapcheck must read 0 coplanar same-facing faces
// at N=2000.
// =============================================================================
// ---------------------------------------------------------------------------
// Procedural map — seeded arena generation. AABB solids are the collision set
// AND the render mesh; themes color them.
// ---------------------------------------------------------------------------

#define ARENA_HALF   24.0f
// Raised from 96 with the wall-damage pass: a broken wall is up to four abutting pieces
// plus three rubble blocks where it used to be one box, and all of them are real cover
// that has to be in the collision set. int16 carries the solid index on the wire
// (net_ev_from_live) and the world hash is over the USED entries, so the ceiling itself
// is free — what it costs is 16 lobbies x 32 x 40 bytes of server memory.
#define MAX_SOLIDS   160

// `pen` = a round may cross this box if its budget stretches that far.
typedef struct { v3 min, max, color; int pen; } solid_t;

static solid_t g_solids_all[SRV_LOBBIES][MAX_SOLIDS];
#define g_solids (g_solids_all[g_lobby])
static int     g_num_solids_all[SRV_LOBBIES];
#define g_num_solids (g_num_solids_all[g_lobby])

// Every match start regenerates the arena from the next seed in a deterministic chain
// (harness runs stay reproducible from --seed; the Windows build seeds the chain from
// startup time).
static uint32_t g_map_seed_all[SRV_LOBBIES];
#define g_map_seed (g_map_seed_all[g_lobby])
static int      g_world_dirty_all[SRV_LOBBIES];
#define g_world_dirty (g_world_dirty_all[g_lobby])

static const v3 SPAWN_POS = {{0.0f, 0.0f, 18.0f}};

static void map_add(v3 mn, v3 mx, v3 col) {
  if (g_num_solids >= MAX_SOLIDS) return;
  g_solids[g_num_solids++] = (solid_t){mn, mx, col, 1};
}

// Perimeter = 4 wall bases + 4 accent bands, always solids 0..7.
#define PERIM_SOLIDS 8
// Wall-hugging corridor kept free of cover: guaranteed rotation routes and
// four long sniper sightlines, one per wall.
#define RING_W 4.5f

static int map_box_free(v3 mn, v3 mx, float gap) {
  // Reject boxes near either spawn zone (the map is 180-deg rotationally
  // symmetric, so the mirrored spawn must stay clear too) or overlapping
  // existing solids by less than `gap`.
  for (int s = -1; s <= 1; s += 2) {
    float sz = SPAWN_POS.z * (float)s;
    if (mx.x > SPAWN_POS.x - 4 && mn.x < SPAWN_POS.x + 4 &&
        mx.z > sz - 4 && mn.z < sz + 4) return 0;
  }
  for (int i = PERIM_SOLIDS; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (mx.x > s->min.x - gap && mn.x < s->max.x + gap &&
        mx.z > s->min.z - gap && mn.z < s->max.z + gap &&
        mx.y > s->min.y && mn.y < s->max.y) return 0;
  }
  return 1;
}

// 180-deg rotational symmetry about the arena center: every element lands
// twice, once at (x,z) and once at (-x,-z). Symmetric cover = fair duels.
static int map_sym_free(v3 mn, v3 mx, float gap) {
  return map_box_free(mn, mx, gap) &&
         map_box_free((v3){{-mx.x, mn.y, -mx.z}}, (v3){{-mn.x, mx.y, -mn.z}}, gap);
}

static void map_add_sym(v3 mn, v3 mx, v3 col) {
  map_add(mn, mx, col);
  map_add((v3){{-mx.x, mn.y, -mx.z}}, (v3){{-mn.x, mx.y, -mn.z}}, col);
}

static v3 col_tint(v3 c, float lo, float hi) {
  float t = rng_range(lo, hi);
  return (v3){{c.x * t, c.y * t, c.z * t}};
}


// Curated arena themes — light, high-value surfaces so the dark bot silhouettes always
// pop; muted accents, one saturated landmark colour.
typedef enum { SURF_GRIT = 0, SURF_WET = 1, SURF_SAND = 2 } surf_kind_t;
typedef struct {
  v3 ground, wall, band_ew, band_n, band_s, cover, crate_a, crate_b, mid;
  int surf;
} theme_t;

static const theme_t THEMES[] = {
  // QUARTZ — warm daylight concrete, teal landmark
  {.ground = {{0.60f, 0.585f, 0.55f}}, .wall = {{0.67f, 0.655f, 0.62f}},
   .band_ew = {{0.33f, 0.34f, 0.36f}}, .band_n = {{0.29f, 0.48f, 0.65f}},
   .band_s = {{0.80f, 0.48f, 0.20f}},  .cover = {{0.64f, 0.44f, 0.34f}},
   .crate_a = {{0.63f, 0.56f, 0.45f}}, .crate_b = {{0.53f, 0.51f, 0.48f}},
   .mid = {{0.15f, 0.47f, 0.49f}}, .surf = SURF_GRIT},
  // SLATE — cool overcast grays, signal-orange landmark
  {.ground = {{0.555f, 0.57f, 0.595f}}, .wall = {{0.63f, 0.65f, 0.67f}},
   .band_ew = {{0.30f, 0.32f, 0.34f}}, .band_n = {{0.30f, 0.52f, 0.72f}},
   .band_s = {{0.82f, 0.50f, 0.20f}},  .cover = {{0.44f, 0.50f, 0.60f}},
   .crate_a = {{0.61f, 0.59f, 0.56f}}, .crate_b = {{0.50f, 0.48f, 0.46f}},
   .mid = {{0.83f, 0.42f, 0.15f}}, .surf = SURF_WET},
  // SAND — desert light, deep-blue landmark
  {.ground = {{0.63f, 0.595f, 0.52f}}, .wall = {{0.71f, 0.67f, 0.585f}},
   .band_ew = {{0.36f, 0.34f, 0.30f}}, .band_n = {{0.27f, 0.46f, 0.66f}},
   .band_s = {{0.78f, 0.42f, 0.16f}},  .cover = {{0.62f, 0.40f, 0.30f}},
   .crate_a = {{0.66f, 0.585f, 0.45f}}, .crate_b = {{0.56f, 0.51f, 0.43f}},
   .mid = {{0.21f, 0.36f, 0.58f}}, .surf = SURF_SAND},
};

static theme_t g_theme_all[SRV_LOBBIES];
#define g_theme (g_theme_all[g_lobby])

// ONE wind direction per arena.
static float g_wind_ang_all[SRV_LOBBIES];
#define g_wind_ang (g_wind_ang_all[g_lobby])
static v3 wind_dir(void) {
  return (v3){{cosf(g_wind_ang), 0.0f, sinf(g_wind_ang)}};
}

// ---------------------------------------------------------------------------
// THE SUN — map state, exactly like the wind, and hashed off the seed for the
// same reason: a single draw from g_rng here would re-roll the layout of every
// arena anyone has ever recorded. What varies is the TIME OF DAY, and it is the
// biggest single lever this renderer has: the same arena under a 45-degree sun
// and under an 8-degree one is two places.
//
// ONE STRUCT, AND IT IS THE ONLY AUTHORITY. The direction used to be three
// #defines stringized into the GLSL, with C re-typing them for the shadow
// projection, the shadow-cull foot and the boot screen's sun-side corner — four
// readers of a constant that could only stay in step by hand. Everything below
// is DERIVED from the one drawn direction by sun_build, and both shader programs
// read the result through one uploader (rig_upload), so the sky, the world, the
// shadow map and the showcase cannot disagree about where the light is.
typedef struct {
  v3    dir;                       // TO the light, unit
  v3    col;                       // radiance after atmospheric extinction
  v3    sky_top, sky_hor, sky_gnd;
  float exposure;                  // eye adaptation; see sun_build
  float disc;                      // how hard the visible disc burns; see sun_set
  float cloud;                     // deck coverage, this arena's weather
  float sh_half, sh_depth;         // the shadow ortho box this elevation needs
  v3    sh_centre;                 // ...and where it has to be centred
} sun_t;
static sun_t g_sun_all[SRV_LOBBIES];
#define g_sun (g_sun_all[g_lobby])

// The tallest thing that can cast across the arena, for the shadow box fit.
#define SUN_CAST_H     4.6f
// Below this the extinction model runs out and the shadows stop landing on the
// arena at all: at 6 degrees a 4.6 m wall throws 44 m of shadow, which is most
// of the floor. It is a floor on the DRAW, not a clamp inside the model.
#define SUN_ELEV_MIN   6.5f
#define SUN_ELEV_MAX   54.0f

// Rayleigh + aerosol optical depth at unit air mass, and the radiance above it
// all. NOT invented: the three taus are the ones that reproduce the light rig
// this game shipped with — (2.90, 2.56, 2.04) at 38 degrees — from a white
// 3.60 source, so a mid-afternoon arena is bit-for-bit the look that was tuned
// and every other elevation follows from the same physics rather than from a
// second set of hand-picked colours.
static v3 sun_extinct(float sin_elev, float elev_deg) {
  // Kasten-Young air mass. A plain 1/sin blows up at the horizon — which is
  // where half the interesting light is — and this is the standard fit that
  // does not.
  float am = 1.0f / (sin_elev + 0.15f * powf(elev_deg + 3.885f, -1.253f));
  return (v3){{3.60f * expf(-0.132f * am), 3.60f * expf(-0.210f * am),
               3.60f * expf(-0.356f * am)}};
}

static void sun_set(float elev_deg, float azi_rad);

static void sun_build(uint32_t seed) {
  // Hashed off the seed, never drawn from the map stream — see wind_dir().
  uint32_t h = seed * 2654435761u + 0x5C1Fu;
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
  float u = (float)(h >> 8) * (1.0f / 16777216.0f);
  uint32_t h2 = h * 2246822519u + 1u;
  h2 ^= h2 >> 13; h2 *= 3266489917u; h2 ^= h2 >> 16;
  float az = (float)(h2 >> 8) * (1.0f / 16777216.0f) * (2.0f * F_PI);
  // u^1.5 leans the draw toward the low sun WITHOUT pinning it there: about
  // two arenas in five come up under 20 degrees, i.e. golden hour, and the
  // rest spread over the afternoon. A uniform draw gave a flat afternoon most
  // of the time, which is the one look this whole pass exists to escape.
  float ed = SUN_ELEV_MIN + (SUN_ELEV_MAX - SUN_ELEV_MIN) * u * sqrtf(u);
  sun_set(ed, az);
  // The weather is its own draw off the same hash: a clear arena and a
  // banded one are different places under the same sun, and coupling the two
  // would mean every sunset came with the same sky.
  uint32_t h3 = h * 3266489917u + 0x9E37u;
  h3 ^= h3 >> 15; h3 *= 2246822519u; h3 ^= h3 >> 13;
  g_sun.cloud = 0.13f + 0.26f * ((float)(h3 >> 8) * (1.0f / 16777216.0f));
}

// The whole rig from one angle pair. Split out of sun_build because the harness
// `sun` command drives it too: a look that can only be reached by hunting seeds
// cannot be reviewed.
static void sun_set(float elev_deg, float azi_rad) {
  sun_t *S = &g_sun;
  float e = elev_deg * (F_PI / 180.0f);
  float se = sinf(e), ce = cosf(e);
  S->dir = (v3){{ce * sinf(azi_rad), se, -ce * cosf(azi_rad)}};
  S->col = sun_extinct(se, elev_deg);
  // DUSK IS A RAMP ON THE ELEVATION, not a second mode. Everything below
  // crosses over between 3 and 29 degrees, so no seed lands in a gap.
  float dusk = 1.0f - f_smoothstep(0.05f, 0.49f, se);
  S->sky_top = v3_lerp((v3){{0.14f, 0.27f, 0.49f}},
                       (v3){{0.052f, 0.070f, 0.180f}}, dusk);
  // The horizon AWAY from the sun. The fire on the other side is the aureole
  // and the haze band, both of which already carry the reddened sun colour —
  // painting it in here as well is how a sunset ends up orange all the way
  // round, which no sunset is.
  S->sky_hor = v3_lerp((v3){{0.88f, 0.96f, 1.11f}},
                       (v3){{0.255f, 0.205f, 0.265f}}, dusk);
  S->sky_gnd = v3_lerp((v3){{0.36f, 0.33f, 0.29f}},
                       (v3){{0.085f, 0.072f, 0.082f}}, dusk);
  // EYE ADAPTATION, AND ONLY PARTLY. A low sun delivers a third of the light,
  // and a frame that simply goes a third as bright is a frame the player
  // cannot fight in. The 0.55 exponent gives back most of the stop but not
  // all of it, so dusk still READS as dusk — measured, sunlit ground lands at
  // 157 against 187 at midday — while the silhouettes the whole art direction
  // depends on keep their separation.
  float lum = 0.2126f * S->col.x + 0.7152f * S->col.y + 0.0722f * S->col.z;
  S->exposure = 0.40f * powf(2.556f / fmaxf(lum, 0.02f), 0.55f);
  // THE SHADOW BOX FOLLOWS THE SUN DOWN. 31 was the tight fit for a fixed
  // 38-degree light and is nowhere near enough at 8: a wall smears 1/tan(elev)
  // of its own height across the floor, 5.9 m there and 33 m here. The box is
  // also SHIFTED half that smear downwind of the light, because a shadow only
  // ever falls one way — centring it would pay for an equal margin on the side
  // no shadow can reach, i.e. half the resolution for nothing.
  // THE DISC MAY NOT BURN THE SAME AT EVERY HOUR. 26 is right for a midday
  // sun — it saturates every channel and reads as a hole in the sky, which is
  // what looking at the sun does. At 8 degrees the same 26 saturates just as
  // hard, so the one frame the whole sunset is built around ended with a WHITE
  // pinprick sitting in an orange sky: the extinction that reddened everything
  // else never reached the source itself. Measured against the tonemap at the
  // matching exposure, 0.8 puts the low disc at roughly (221, 180, 125) — an
  // orange orb — and the ramp is over by 30 degrees, where a sun should blind.
  S->disc = 0.8f + 25.2f * f_smoothstep(0.13f, 0.58f, se);
  S->cloud = 0.26f;   // sun_build re-draws this per map; this is the default
  float tan_e = se / sqrtf(fmaxf(1.0f - se * se, 1.0e-4f));
  float smear = fminf(SUN_CAST_H / fmaxf(tan_e, 0.10f), 34.0f);
  S->sh_half = ARENA_HALF * 1.06f + smear * 0.5f;
  S->sh_depth = 2.2f * S->sh_half + 40.0f;
  v3 dh = v3_norm((v3){{S->dir.x, 0.0f, S->dir.z}});
  S->sh_centre = (v3){{-dh.x * smear * 0.5f, 1.6f, -dh.z * smear * 0.5f}};
}

// THE MOOD IS MAP STATE, the sun's and the wind's sibling: three latents in
// [0,1] that the generator and the scatter pass read so their features move
// TOGETHER (a ruined arena has more damaged walls AND more rubble AND more
// brick spill — one number, many readers, or the correlations that make a
// place read as one place cannot exist). Hashed off the seed, never drawn
// from g_rng, for the wind's reason: a draw here would re-roll every
// recorded layout — although the mood DOES feed probabilities inside the
// generator's own g_rng consumption, which is why introducing it was the
// one deliberate seed break of the 2026-08-25 layout window.
//
// DISCRETE PRESETS ON TOP, JITTER BELOW: three named moods per theme, each a
// latent triple plus one LOUD LEVER (the construction that names the mood
// from a single frame). A free latent draw gives every seed the same beige
// average; presets are what make a seed a nameable place while every seed
// stays one game. The latents jitter ±0.15 off two more hash lanes;
// ELIGIBILITY decisions (signatures) read the DISCRETE preset, pre-jitter,
// so a jittered latent crossing a threshold can never flip a signature on.
typedef struct { float ruin, growth, strew; } mood_t;
typedef struct { const char *name; mood_t m; } mood_def_t;
// Indexed [surf][preset] — surf is the theme's own enum (GRIT/WET/SAND).
static const mood_def_t MOOD_DEF[3][3] = {
  {{"QUARRY",     {0.70f, 0.10f, 0.70f}},   // heavy damage, talus, brick spill
   {"SWEPT",      {0.15f, 0.20f, 0.25f}},   // near-empty: the minimalist mood
   {"STEPPE",     {0.25f, 0.65f, 0.35f}}},  // straw tuft bands along the walls
  {{"DOWNPOUR",   {0.35f, 0.00f, 0.45f}},   // max water + waterline bands
   {"DRAINED",    {0.50f, 0.00f, 0.65f}},   // scree fans, water pulled low
   {"MOSSTONE",   {0.30f, 0.15f, 0.50f}}},  // moss on cobble and wall base
  {{"BURIED",     {0.55f, 0.10f, 0.35f}},   // oversized drifts, half-sunk reads
   {"STONEFIELD", {0.25f, 0.15f, 0.80f}},   // ventifact clusters + pavement
   {"THORNS",     {0.20f, 0.75f, 0.40f}}},  // grass + low cactus; the tree
};
static mood_t g_mood = {0.5f, 0.5f, 0.5f};
static int    g_mood_preset;   // discrete row, 0..2 within the theme
static mood_t g_mood_forced;
static int    g_mood_force;          // 1 = latents forced (preset stays)
static int    g_mood_force_preset = -1;   // >= 0 = preset index forced
// Needs the THEME, so it runs after the theme draw — the preset table is
// per-theme and the theme is deliberately still the first g_rng draw (the
// same seed keeps its theme across the layout break).
static void mood_build(uint32_t seed, int surf) {
  uint32_t h = seed * 2654435761u + 0x300Du;
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
  int preset = (int)(h % 3u);
  if (g_mood_force_preset >= 0) preset = g_mood_force_preset;
  g_mood_preset = preset;
  mood_t m = MOOD_DEF[surf][preset].m;
  uint32_t h2 = h * 2246822519u + 1u;
  h2 ^= h2 >> 13; h2 *= 3266489917u; h2 ^= h2 >> 16;
  uint32_t h3 = h2 * 2654435761u + 0x9E37u;
  h3 ^= h3 >> 15; h3 *= 2246822519u; h3 ^= h3 >> 13;
  float j1 = ((float)(h2 >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.30f;
  float j2 = ((float)(h3 >> 8) * (1.0f / 16777216.0f) - 0.5f) * 0.30f;
  m.ruin   = f_clamp(m.ruin + j1, 0.0f, 1.0f);
  m.growth = f_clamp(m.growth + j2, 0.0f, 1.0f);
  m.strew  = f_clamp(m.strew + (j1 - j2) * 0.5f, 0.0f, 1.0f);
  if (g_mood_force_preset >= 0) m = MOOD_DEF[surf][preset].m;  // exact, reviewable
  g_mood = g_mood_force ? g_mood_forced : m;
}

// --- the layout -> dressing data channel -----------------------------------
// Everything the generator decides that a LATER pass consumes: the mood
// preset, the signature, the damage edges the brick/stain dressing attaches
// to, and where the special solids sit. Re-deriving any of this from
// g_solids is guesswork; the struct is the contract. Canonical-half data —
// the dressing passes mint the 180-degree twins themselves.
typedef enum {
  SIG_NONE = 0,
  SIG_TREE,      // a twinned pair of dead trees: solid trunks, casting crowns
  SIG_COLLAPSE,  // one long wall fully down to a jumpable stub + brick field
  SIG_BOULDERS,  // a twinned pair of ~1.1 m boulders: solids, chamfered render
  SIG_FALLEN,    // the monolith toppled: a lying slab + shattered-tip rubble
  SIG_SPLIT      // the split wall: two depth-offset columns, an advertised
                 // oblique sight corridor (see its gates at the build site)
} mapsig_t;      // NOT sig_t: signal.h owns that name
// [[maybe_unused]] for upd_status_line's reason: the only reader is the
// harness `map` print, which is Linux-only.
[[maybe_unused]] static const char *SIG_NAME[] = {
  "none", "tree", "collapse", "boulders", "fallen", "split"};
typedef enum { ME_FOOT = 0, ME_SILL, ME_TOP } medge_kind_t;
typedef struct { v3 a, b; uint8_t kind, ax; } medge_t;
#define META_EDGE_MAX 48
typedef struct {
  uint8_t sig;
  int16_t bould[2];     // solid indices of the boulder pair, else -1
  int16_t tree[2];      // solid indices of the two trunks, else -1
  int     nedge;
  medge_t edge[META_EDGE_MAX];
} map_meta_t;
static map_meta_t g_meta;

static void meta_edge(v3 a, v3 b, int kind, int ax) {
  if (g_meta.nedge >= META_EDGE_MAX) return;   // dressing degrades, never UB
  g_meta.edge[g_meta.nedge++] = (medge_t){a, b, (uint8_t)kind, (uint8_t)ax};
}

// THE SIGNATURE IS ONE SLOT, hash-gated off the seed with ordered rolls —
// first hit wins, later features skip. Rarity is spent on silhouettes: a
// signature must be nameable from the play camera, so two on one map would
// compete and neither would read. Eligibility reads the DISCRETE preset.
static void sig_build(uint32_t seed, int surf, int preset) {
  memset(&g_meta, 0, sizeof g_meta);
  g_meta.bould[0] = g_meta.bould[1] = -1;
  g_meta.tree[0] = g_meta.tree[1] = -1;
  uint32_t h = seed * 3266489917u + 0x51Cu;
  h ^= h >> 15; h *= 2654435761u; h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
  float u[5];
  for (int i = 0; i < 5; i++) {
    u[i] = (float)(h >> 8) * (1.0f / 16777216.0f);
    h = h * 2654435761u + 1u; h ^= h >> 15; h *= 3266489917u; h ^= h >> 13;
  }
  int ruinous = (surf == SURF_GRIT && preset == 0) ||   // QUARRY
                (surf == SURF_WET && preset == 1) ||    // DRAINED
                (surf == SURF_SAND && preset == 0);     // BURIED
  if ((surf != SURF_WET && preset == 2) && u[0] < 0.20f) g_meta.sig = SIG_TREE;
  else if (ruinous && u[1] < 1.0f / 6.0f) g_meta.sig = SIG_COLLAPSE;
  else if (((surf == SURF_SAND && preset == 1) ||
            (surf == SURF_GRIT && preset == 0)) && u[2] < 0.22f)
    // 0.22, not 1/6: measured over 500 seeds the slot competition pushed the
    // pair under the ~1/30 namable floor — rarity below that is wasted.
    g_meta.sig = SIG_BOULDERS;
  else if (u[3] < 0.125f) g_meta.sig = SIG_FALLEN;
  else if (ruinous && u[4] < 0.125f) g_meta.sig = SIG_SPLIT;
  else g_meta.sig = SIG_NONE;
}

// THE ONE COPY of the lean/eye-band rule, for every horizontal edge a player
// can shoot over or duck under. Leaning drops the eye 91 mm (1.620 -> 1.529,
// the `lean` proof), so an edge inside the OPEN interval (1.48, 1.78) flips
// a sightline open/shut on the lean alone with nothing on the wall saying
// which way — the one thing leaning is for, inverted. Escapes are the two
// curated stations: 1.82 (stand-and-shoot) where the wall has the room and
// the coin says so, 1.44 (crouch-only) otherwise.
static float map_eyeband(float y, float ymax, uint32_t coin) {
  if (y > 1.48f && y < 1.78f)
    return (ymax >= 1.82f && coin) ? 1.82f : 1.44f;
  return y;
}

// --- A LONG WALL IS NOT ONE BOX --------------------------------------------
// At twenty metres the only thing that reads about a wall is its top line against the
// sky, and eight identical horizontal lines is what makes a procedural arena look
// authored by a for-loop.
//
// THE DAMAGE IS REAL GEOMETRY, NOT A DECAL: `g_solids` is the collision set AND the
// render mesh, so a chip that exists only in the picture is cover that is not there and
// a death nobody can explain. Every piece below is a solid.
//
// The pieces ABUT EXACTLY rather than overlapping: two collinear boxes that overlap
// share BOTH long side planes and `mapcheck` counts that as the z-fighting it is, while
// at zero overlap every pair falls out of its area test and one of the two coincident
// faces is always culled.
//
// HOW MANY walls are damaged is the mood's call now — P = 0.15 + 0.55*ruin —
// because "how ruined is this place" is exactly what the ruin latent IS. At the
// QUARRY end most walls carry a story; on a SWEPT map almost none do, and both
// read as places rather than as settings of a slider.
static void wall_piece(v3 mn, v3 mx, int ax, float a, float b,
                       float y0, float y1, v3 col) {
  v3 p0 = mn, p1 = mx;
  if (ax) { p0.x = a; p1.x = b; } else { p0.z = a; p1.z = b; }
  p0.y = y0; p1.y = y1;
  map_add_sym(p0, p1, col);
}

// Rubble at the foot of a break.
static void wall_rubble(v3 mn, v3 mx, int ax, float at, float dir, int count,
                        float hcap, v3 col) {
  float H = ARENA_HALF;
  float cz = ax ? (mn.z + mx.z) * 0.5f : (mn.x + mx.x) * 0.5f;
  for (int k = 0; k < count; k++) {
    // THIS COLLISION HAS NO STEP-UP. move_axis is a per-axis AABB sweep and nothing in
    // the file auto-climbs: a 0.25 m block is not a kerb you walk over, it is a wall
    // you have to JUMP, and a scatter of them in open ground is the clutter that gets
    // removed from competitive maps for exactly that reason.
    float rw = rng_range(0.22f, 0.34f), rd = rng_range(0.22f, 0.30f);
    // Capped by whatever it fell OFF. A rubble block taller than the stub it sits under
    // is not a collapse, it is a second wall — and on the low height class (a 1.0 m
    // wall breaks to a 0.4 m stub) an uncapped 0.62 would be exactly that.
    float rh = rng_range(0.34f, 0.58f);
    if (rh > hcap) rh = hcap;
    if (rh < 0.18f) continue;
    float side = rng_range(-0.06f, 0.06f);
    // Slots along the run, spaced wider than two blocks can ever be. dir 0 is the
    // breach, whose spill sits in front of the hole rather than beyond an end, so its
    // blocks step sideways from the centre instead. ...AND IT SITS ON THE WALL'S OWN
    // LINE, immediately at the break, rather than being thrown clear of it.
    float slot = dir != 0.0f ? dir * (0.30f + rw + 0.34f * (float)k)
                             : ((float)k - 0.5f * (float)(count - 1)) * 0.86f;
    v3 p0, p1;
    if (ax) {
      p0 = (v3){{at + slot - rw, 0.0f, cz + side - rd}};
      p1 = (v3){{at + slot + rw, rh,   cz + side + rd}};
    } else {
      p0 = (v3){{cz + side - rd, 0.0f, at + slot - rw}};
      p1 = (v3){{cz + side + rd, rh,   at + slot + rw}};
    }
    // The same corridor bound the step crates get: the RING_W lane is documented
    // cover-free, and a knee-high block a bullet stops is still cover.
    if (p0.x < -(H - RING_W) || p0.z < -(H - RING_W) ||
        p1.x > H - RING_W || p1.z > H - RING_W) continue;
    if (!map_sym_free(p0, p1, 0.012f)) continue;
    map_add_sym(p0, p1, col_tint(col, 0.84f, 0.96f));
  }
}

// A point on a wall's centre line, for the meta edges the dressing pass reads.
static v3 wall_pt(v3 mn, v3 mx, int ax, float t, float y) {
  float cz = ax ? (mn.z + mx.z) * 0.5f : (mn.x + mx.x) * 0.5f;
  return ax ? (v3){{t, y, cz}} : (v3){{cz, y, t}};
}

static void map_add_wall_sym(v3 mn, v3 mx, v3 col, int ax) {
  float lo = ax ? mn.x : mn.z, hi = ax ? mx.x : mx.z;
  float len = hi - lo, h = mx.y;
  uint32_t roll = rng_u32();
  // A short wall has no room for a story, and a LOW wall has no room for a hole
  // with material above and below it.
  int kind = (len >= 3.2f &&
              (float)(roll & 1023u) * (1.0f / 1024.0f) <
                  0.15f + 0.55f * g_mood.ruin)
             ? (int)((roll >> 10) % 3u) : -1;
  if (kind == 1 && h < 1.8f) kind = 2;
  switch (kind) {
  case 0: {  // BROKEN END: the last stretch has come down to a climbable stub
    float bl = rng_range(0.9f, 1.6f);
    // A FRACTION of the wall, never an absolute height.
    float hs = h * rng_range(0.34f, 0.52f);
    // NOT `far`. windef.h defines `far` and `near` as macros, so a local of
    // either name compiles clean on gcc and fails only the Windows build.
    int hi_end = (int)((roll >> 13) & 1u);
    float cut = hi_end ? hi - bl : lo + bl;
    if (hi_end) {
      wall_piece(mn, mx, ax, lo, cut, 0.0f, h, col);
      wall_piece(mn, mx, ax, cut, hi, 0.0f, hs, col);
      wall_rubble(mn, mx, ax, hi, 1.0f, 1, hs * 0.82f, col);
      meta_edge(wall_pt(mn, mx, ax, cut, hs), wall_pt(mn, mx, ax, hi, hs),
                ME_TOP, ax);
      meta_edge(wall_pt(mn, mx, ax, cut, 0.0f),
                wall_pt(mn, mx, ax, cut + 0.9f, 0.0f), ME_FOOT, ax);
    } else {
      wall_piece(mn, mx, ax, lo, cut, 0.0f, hs, col);
      wall_piece(mn, mx, ax, cut, hi, 0.0f, h, col);
      wall_rubble(mn, mx, ax, lo, -1.0f, 1, hs * 0.82f, col);
      meta_edge(wall_pt(mn, mx, ax, lo, hs), wall_pt(mn, mx, ax, cut, hs),
                ME_TOP, ax);
      meta_edge(wall_pt(mn, mx, ax, cut - 0.9f, 0.0f),
                wall_pt(mn, mx, ax, cut, 0.0f), ME_FOOT, ax);
    }
  } break;
  case 1: {  // BREACH: material above and below, nothing between. A real hole.
    float bw = rng_range(0.85f, 1.45f);
    float mid = rng_range(lo + bw * 0.6f + 0.5f, hi - bw * 0.6f - 0.5f);
    float a = mid - bw * 0.5f, b = mid + bw * 0.5f;
    // The SILL is a ledge you can mount and the LINTEL leaves a port you can fire
    // through standing or crouched.
    float y0 = rng_range(0.72f, 0.94f);
    float y1 = y0 + rng_range(0.62f, 0.86f);
    if (y1 > h - 0.28f) y1 = h - 0.28f;
    if (y1 - y0 < 0.35f) { y0 = h * 0.34f; y1 = h * 0.72f; }
    wall_piece(mn, mx, ax, lo, a, 0.0f, h, col);
    // THE HOLE IS RAGGED, and it is ragged in the collision set as well as in the
    // picture.
    int cols = 2 + (int)((roll >> 14) & 1u);
    for (int c = 0; c < cols; c++) {
      float ca = a + (b - a) * (float)c / (float)cols;
      float cb = a + (b - a) * (float)(c + 1) / (float)cols;
      float jy0 = y0 * rng_range(0.80f, 1.14f);
      float jy1 = y1 + (h - y1) * rng_range(-0.42f, 0.34f);
      if (jy1 > h - 0.14f) jy1 = h - 0.14f;
      if (jy1 - jy0 < 0.30f) jy1 = jy0 + 0.30f;
      if (jy1 > h - 0.14f) { jy1 = h - 0.14f; jy0 = jy1 - 0.30f; }
      // THE LINTEL MAY NOT LAND ON THE EYE — map_eyeband is the one copy of
      // the rule every horizontal edge in this generator now goes through.
      jy1 = map_eyeband(jy1, h - 0.14f, (roll >> 17) & 1u);
      if (jy1 - jy0 < 0.30f) jy0 = jy1 - 0.30f;
      if (jy0 < 0.34f) jy0 = 0.34f;
      if (jy1 - jy0 < 0.30f) jy1 = jy0 + 0.30f;
      wall_piece(mn, mx, ax, ca, cb, 0.0f, jy0, col);
      wall_piece(mn, mx, ax, ca, cb, jy1, h, col);
      if (jy0 >= 0.50f)  // a sill worth a brick is a ledge, not a kerb
        meta_edge(wall_pt(mn, mx, ax, ca, jy0), wall_pt(mn, mx, ax, cb, jy0),
                  ME_SILL, ax);
    }
    wall_piece(mn, mx, ax, b, hi, 0.0f, h, col);
    wall_rubble(mn, mx, ax, mid, 0.0f, 1, y0 * 0.72f, col);
    meta_edge(wall_pt(mn, mx, ax, a, 0.0f), wall_pt(mn, mx, ax, b, 0.0f),
              ME_FOOT, ax);
  } break;
  case 2: {  // CRENELLATION: the top comes down by QUANTIZED courses.
    // Continuous noise on a wall top reads as melted; masonry gives way a
    // course at a time, so the drops are exactly -0.15 and -0.35 — and at
    // most three distinct heights per wall (the eye-band remap uses ONE coin
    // per wall so it cannot mint a fourth). Damage biases toward the ENDS,
    // where a real wall fails first.
    float segt[18], segh[17];
    int ns = 0;
    float t = lo;
    uint32_t coin = (roll >> 13) & 1u;
    while (t < hi - 0.4f && ns < 16) {
      segt[ns] = t;
      t += rng_range(0.45f, 0.95f);
      if (t > hi - 0.35f) t = hi;
      float mid = (segt[ns] + t) * 0.5f;
      float dend = fminf(mid - lo, hi - mid);
      float endb = 1.0f + 0.9f * (1.0f - fminf(dend, 2.0f) * 0.5f);
      float pd = (0.22f + 0.36f * g_mood.ruin) * endb;
      float ch = h;
      if (rng_range(0.0f, 1.0f) < pd)
        ch = map_eyeband(rng_range(0.0f, 1.0f) < 0.40f ? h - 0.15f : h - 0.45f,
                         h, coin);
      segh[ns++] = ch;
    }
    segt[ns] = hi;
    if (ns == 0) { map_add_sym(mn, mx, col); break; }
    float dmin = h, dat = lo;
    int i0 = 0;
    for (int i = 1; i <= ns; i++) {
      if (i == ns || segh[i] != segh[i0]) {
        wall_piece(mn, mx, ax, segt[i0], segt[i], 0.0f, segh[i0], col);
        if (segh[i0] < h - 0.01f) {
          meta_edge(wall_pt(mn, mx, ax, segt[i0], segh[i0]),
                    wall_pt(mn, mx, ax, segt[i], segh[i0]), ME_TOP, ax);
          if (segh[i0] < dmin) { dmin = segh[i0]; dat = (segt[i0] + segt[i]) * 0.5f; }
        }
        i0 = i;
      }
    }
    if (dmin < h - 0.01f) {
      wall_rubble(mn, mx, ax, dat, 0.0f, 1, dmin * 0.55f, col);
      meta_edge(wall_pt(mn, mx, ax, dat - 0.5f, 0.0f),
                wall_pt(mn, mx, ax, dat + 0.5f, 0.0f), ME_FOOT, ax);
    }
  } break;
  default: {
    // ERODED CORNER — an intact wall may still lose the top of ONE end: a
    // stepped diagonal in silhouette, the highest-value damage feature per
    // solid spent, because the top line against the sky is all that reads at
    // twenty metres. Never both ends: a wall missing all its corners is the
    // collapsed-wall SIGNATURE's job, and corners read as structure.
    uint32_t er = (roll >> 12) & 1023u;
    if (h >= 1.4f &&
        (float)er * (1.0f / 1024.0f) < 0.10f + 0.28f * g_mood.ruin) {
      int hi_end = (int)((roll >> 22) & 1u);
      uint32_t coin = (roll >> 23) & 1u;
      float e = rng_range(0.30f, 0.70f);
      float w1 = rng_range(0.28f, 0.50f), w2 = rng_range(0.25f, 0.45f);
      float h1 = map_eyeband(h - e * 0.45f, h, coin);
      float h2 = map_eyeband(h - e, h, coin);
      if (h2 > h1) h2 = h1 - 0.15f;   // the steps must descend
      float c1 = hi_end ? hi - w1 - w2 : lo + w1 + w2;
      float c2 = hi_end ? hi - w2 : lo + w2;
      if (hi_end) {
        wall_piece(mn, mx, ax, lo, c1, 0.0f, h, col);
        wall_piece(mn, mx, ax, c1, c2, 0.0f, h1, col);
        wall_piece(mn, mx, ax, c2, hi, 0.0f, h2, col);
        meta_edge(wall_pt(mn, mx, ax, c1, h1), wall_pt(mn, mx, ax, c2, h1),
                  ME_TOP, ax);
        meta_edge(wall_pt(mn, mx, ax, c2, h2), wall_pt(mn, mx, ax, hi, h2),
                  ME_TOP, ax);
      } else {
        wall_piece(mn, mx, ax, lo, c2, 0.0f, h2, col);
        wall_piece(mn, mx, ax, c2, c1, 0.0f, h1, col);
        wall_piece(mn, mx, ax, c1, hi, 0.0f, h, col);
        meta_edge(wall_pt(mn, mx, ax, lo, h2), wall_pt(mn, mx, ax, c2, h2),
                  ME_TOP, ax);
        meta_edge(wall_pt(mn, mx, ax, c2, h1), wall_pt(mn, mx, ax, c1, h1),
                  ME_TOP, ax);
      }
    } else map_add_sym(mn, mx, col);
  } break;
  }
}

// THE AIR OVER THIS ARENA, per theme — the tint of the dust it carries and how
// much of it there is. ONE pair of numbers feeds BOTH halves of the atmosphere:
// the haze band the sky pass draws along the horizon and the aerial perspective
// the world pass fades distant geometry into. They are the same medium seen in
// two geometries, so they may not be tuned apart — a horizon that is dusty while
// the far wall in front of it is clear is the seam this arrangement exists to
// make impossible.
//
// The tint is a multiplier on the dust's own scattered radiance, so 1.0 is
// "grey dust" and the departures are what give each theme its light:
//   QUARTZ  dry warm air over concrete   — barely tinted, thin
//   SLATE   cool damp mist off wet stone — blue, and the thickest of the three
//   SAND    ochre desert dust            — warm, and it owns the horizon
static v3 atmo_tint(void) {
  switch (g_theme.surf) {
  case SURF_SAND: return (v3){{1.20f, 0.99f, 0.72f}};
  case SURF_WET:  return (v3){{0.84f, 0.93f, 1.10f}};
  default:        return (v3){{1.06f, 1.00f, 0.90f}};
  }
}
static float atmo_haze(void) {
  switch (g_theme.surf) {
  case SURF_SAND: return 0.46f;
  case SURF_WET:  return 0.50f;
  default:        return 0.33f;
  }
}

// THE HORIZON BEYOND THE WALL, per theme: x = the ridge line's base elevation
// (sin of the angle above the horizon), y = its amplitude, z = its shape
// (0 = rounded dunes, 1 = ridged rock), w = how dark the near silhouette is
// against the haze (0 = the haze itself). Drawn by SKY_FS only — like the
// cloud deck it lives in the sky pass and nowhere else, so no wall and no
// reflection ever sees it; it is scenery, not environment.
//   SAND  low rounded dunes, warm and nearly lost in the dust
//   SLATE soft hills under the mist, barely there
//   QUARTZ a mesa line: flat tops, sharp shoulders, the darkest of the three
static v3 ridge_params(float *dark) {
  switch (g_theme.surf) {
  // The elevations are what clears a 2.75 m perimeter wall seen from eye
  // height at 24 m (about 3.2 degrees): below that the land is behind the
  // wall from everywhere on the floor and exists only for the overview.
  case SURF_SAND: *dark = 0.26f; return (v3){{0.032f, 0.062f, 0.15f}};
  case SURF_WET:  *dark = 0.20f; return (v3){{0.028f, 0.064f, 0.35f}};
  default:        *dark = 0.30f; return (v3){{0.034f, 0.095f, 0.85f}};
  }
}

// Ripple strength / glitter strength / standing-water level, per treatment.
static v3 surf_params(void) {
  switch (g_theme.surf) {
  case SURF_SAND: return (v3){{1.00f, 1.00f, 0.00f}};
  // A wet floor has no ripples (water flattens loose material) and no glitter
  // (the film buries the crystal faces that would catch the sun) — the two are
  // off because the surface is telling a different story, not to save the ALU.
  // The WATER LEVEL is the SLATE moods' loudest scalar: DOWNPOUR floods it,
  // DRAINED pulls it low so the scree fans own the floor, MOSSTONE keeps the
  // old middle. uSurf.z already carries it; nothing else changes.
  case SURF_WET:
    return (v3){{0.00f, 0.00f, g_mood_preset == 0 ? 0.38f
                              : g_mood_preset == 1 ? 0.18f : 0.30f}};
  // Dry concrete gets NO ripples.
  default:        return (v3){{0.00f, 0.22f, 0.00f}};
  }
}

// One human's gun-RNG stream for a given map seed.
static uint64_t rng_gun_seed(uint32_t seed, int h) {
  // Human 0's term vanishes: stream [0] is bit-identical to the historical
  // g_rng_gun, which is what keeps every spray artefact valid.
  return (uint64_t)(seed ^ 0x6A75u) * 2654435761u + 1 +
         (uint64_t)h * 0x9E3779B97F4A7C15u;
}

static void map_gen_shell(void) {
  const float H = ARENA_HALF, WT = 0.6f, WH = 3.2f, BH = 2.75f;
  v3 wc = g_theme.wall;
  map_add((v3){{-H - WT, 0, -H - WT}}, (v3){{H + WT, BH, -H}}, wc);
  map_add((v3){{-H - WT, 0, H}},       (v3){{H + WT, BH, H + WT}}, wc);
  map_add((v3){{-H - WT, 0, -H}},      (v3){{-H, BH, H}}, wc);
  map_add((v3){{H, 0, -H}},            (v3){{H + WT, BH, H}}, wc);
  // Compass bands atop the walls: north cool, south warm, east/west dark —
  // instant orientation in a rotationally symmetric arena.
  map_add((v3){{-H - WT, BH, -H - WT}}, (v3){{H + WT, WH, -H}}, g_theme.band_n);
  map_add((v3){{-H - WT, BH, H}},       (v3){{H + WT, WH, H + WT}}, g_theme.band_s);
  map_add((v3){{-H - WT, BH, -H}},      (v3){{-H, WH, H}}, g_theme.band_ew);
  map_add((v3){{H, BH, -H}},            (v3){{H + WT, WH, H}}, g_theme.band_ew);
  // Everything added SO FAR is the arena shell, and a round may never cross it: it is
  // the edge of the world rather than cover, and the shell happens to be the same 0.60
  // m thick as a cover wall, so thickness alone cannot tell them apart.
  for (int i = 0; i < g_num_solids; i++) g_solids[i].pen = 0;
}

static void map_gen_landmark(void) {
  // Mid landmark, self-symmetric: the map's one saturated color anchor.
  if (g_meta.sig == SIG_FALLEN) {
    // THE FALLEN MONOLITH — the signature that replaces the mid roll: the
    // anchor toppled into a lying slab (0.9 m: mountable cover, a completely
    // different mid fight) with shattered-tip blocks at both ends. Centred,
    // so the slab is self-symmetric; the tip blocks land as symmetric pairs.
    int fax = (int)(rng_u32() & 1u);
    float fl = rng_range(2.6f, 3.2f), fw = rng_range(1.0f, 1.4f);
    v3 smn = fax ? (v3){{-fl, 0, -fw}} : (v3){{-fw, 0, -fl}};
    v3 smx = fax ? (v3){{fl, 0.9f, fw}} : (v3){{fw, 0.9f, fl}};
    map_add(smn, smx, g_theme.mid);
    // The shattered tip: blocks placed SEQUENTIALLY along the fall line, each
    // past the last — two independent draws overlapped about one seed in
    // three hundred and shared a plane when they did, which is exactly the
    // defect class mapcheck exists to catch.
    float tip = fl + rng_range(0.10f, 0.30f);
    for (int k = 0; k < 2; k++) {
      float bw = rng_range(0.30f, 0.48f), bh = rng_range(0.30f, 0.52f);
      float off = tip + bw;
      tip = off + bw + rng_range(0.08f, 0.24f);
      float side = rng_range(-0.5f, 0.5f);
      v3 bmn = fax ? (v3){{off - bw, 0, side - bw}} : (v3){{side - bw, 0, off - bw}};
      v3 bmx = fax ? (v3){{off + bw, bh, side + bw}} : (v3){{side + bw, bh, off + bw}};
      map_add_sym(bmn, bmx, col_tint(g_theme.mid, 0.80f, 0.92f));
    }
    // The shatter line gets the brick/stone dressing.
    meta_edge(fax ? (v3){{fl, 0, -fw}} : (v3){{-fw, 0, fl}},
              fax ? (v3){{fl, 0, fw}} : (v3){{fw, 0, fl}}, ME_FOOT, !fax);
  } else {
    float gx0 = 0.0f, gz0 = 0.0f;   // the standing landmark's base corner
    switch (rng_u32() % 4) {
    case 0: {  // monolith: full block, splits mid into two lanes
      float mx_ = rng_range(2.0f, 3.2f), mz = rng_range(2.0f, 3.2f);
      float mh = rng_range(2.5f, 3.0f);
      map_add((v3){{-mx_, 0, -mz}}, (v3){{mx_, mh, mz}}, g_theme.mid);
      gx0 = mx_; gz0 = mz;
    } break;
    case 1: {  // plaza: mountable low platform, pillar marks the center
      float pw = rng_range(3.4f, 4.4f);
      map_add((v3){{-pw, 0, -pw}}, (v3){{pw, 0.55f, pw}},
              col_tint(g_theme.crate_b, 0.97f, 1.03f));
      map_add((v3){{-0.7f, 0.55f, -0.7f}}, (v3){{0.7f, 3.0f, 0.7f}}, g_theme.mid);
      gx0 = pw; gz0 = pw;
    } break;
    case 2: {  // gate: two parallel walls, a fought-over corridor through mid
      float gx = rng_range(2.6f, 3.6f), gl = rng_range(3.5f, 5.0f);
      float gh = rng_range(2.1f, 2.5f);
      map_add_sym((v3){{gx - 0.35f, 0, -gl}}, (v3){{gx + 0.35f, gh, gl}}, g_theme.mid);
      meta_edge((v3){{gx + 0.35f, 0, -gl * 0.5f}}, (v3){{gx + 0.35f, 0, gl * 0.5f}},
                ME_FOOT, 0);
    } break;
    default: {  // four pillars: porous mid, peek duels
      float d = rng_range(2.6f, 3.8f), r = rng_range(0.45f, 0.6f);
      float h = rng_range(2.4f, 2.8f);
      map_add_sym((v3){{d - r, 0, d - r}},  (v3){{d + r, h, d + r}},  g_theme.mid);
      map_add_sym((v3){{d - r, 0, -d - r}}, (v3){{d + r, h, -d + r}}, g_theme.mid);
    } break;
    }
    // THE LANDMARK STOPS FLOATING (the grounding rule): the two big-mass
    // forms take an eroded plinth corner — 1-2 shrinking steps off ONE base
    // corner, in the WALL family's tint so the accent face itself stays
    // pristine: an old thing maintained, not a new thing dropped in. The
    // steps abut the landmark's own planes exactly, and the twin pair lands
    // on the opposite corner, which is what 180-degree fairness means here.
    if (gx0 > 0.0f) {
      float s1 = rng_range(0.45f, 0.70f), s2 = rng_range(0.35f, 0.55f);
      v3 pc = col_tint(g_theme.wall, 0.86f, 0.94f);
      map_add_sym((v3){{gx0, 0, gz0 - s1}}, (v3){{gx0 + s1 * 0.8f, 0.34f, gz0}}, pc);
      map_add_sym((v3){{gx0, 0, gz0 - s1 - s2}},
                  (v3){{gx0 + s2 * 0.55f, 0.16f, gz0 - s1}}, pc);
      meta_edge((v3){{gx0, 0, -gz0 * 0.6f}}, (v3){{gx0, 0, gz0 * 0.6f}},
                ME_FOOT, 0);
    }
  }
}

static void map_gen_walls(void) {
  // Re-read, not threaded: ARENA_HALF is a compile-time constant, so this
  // local matches map_gen_shell's own copy — bit-identical by construction.
  const float H = ARENA_HALF;
  // Long tactical walls. Heights are curated, never eye-level: LOW you shoot
  // over standing, HIGH blocks fully — no awkward in-between clutter.
  int nwalls = 3 + (int)(rng_u32() % 2);
  int sig_wall_done = 0;
  for (int i = 0; i < nwalls; i++) {
    for (int t = 0; t < 24; t++) {
      float hl = rng_range(1.8f, 3.4f);
      float h = (rng_u32() & 1) ? rng_range(1.95f, 2.25f) : rng_range(0.95f, 1.10f);
      // The split wall needs material above the corridor, so its host is
      // always the HIGH height class.
      if (i == 0 && g_meta.sig == SIG_SPLIT && h < 1.95f)
        h = rng_range(1.95f, 2.25f);
      float cx = rng_range(-(H - RING_W - 1.5f), H - RING_W - 1.5f);
      float cz = rng_range(-(H - RING_W - 1.5f), H - RING_W - 1.5f);
      if (cx * cx + cz * cz < 64.0f) continue;  // leave mid to the landmark
      int ax = rng_u32() & 1;
      v3 mn = {{cx - (ax ? hl : 0.3f), 0, cz - (ax ? 0.3f : hl)}};
      v3 mx = {{cx + (ax ? hl : 0.3f), h, cz + (ax ? 0.3f : hl)}};
      if (mn.x < -(H - RING_W) || mn.z < -(H - RING_W) ||
          mx.x > H - RING_W || mx.z > H - RING_W) continue;
      if (!map_sym_free(mn, mx, 1.4f)) continue;
      v3 wcol = col_tint(g_theme.cover, 0.94f, 1.06f);
      float wlo = ax ? mn.x : mn.z, whi = ax ? mx.x : mx.z;
      if (i == 0 && g_meta.sig == SIG_COLLAPSE) {
        // THE COLLAPSED WALL — one long wall fully down to a 0.4-0.6 m stub:
        // still a solid, still jumpable, and the brick field along its line
        // is where the ruin identity gets its loudest sentence.
        float hs = rng_range(0.40f, 0.60f);
        wall_piece(mn, mx, ax, wlo, whi, 0.0f, hs, wcol);
        wall_rubble(mn, mx, ax, (wlo + whi) * 0.5f, 0.0f, 4, hs * 0.9f, wcol);
        meta_edge(wall_pt(mn, mx, ax, wlo, hs), wall_pt(mn, mx, ax, whi, hs),
                  ME_TOP, ax);
        meta_edge(wall_pt(mn, mx, ax, wlo, 0.0f),
                  wall_pt(mn, mx, ax, whi, 0.0f), ME_FOOT, ax);
        sig_wall_done = 1;
      } else if (i == 0 && g_meta.sig == SIG_SPLIT && h >= 1.95f) {
        // THE SPLIT WALL — the advertised wallbang station. Two columns,
        // each 60 % of the wall's depth, SHIFTED OUT of the wall line in
        // opposite directions and overlapping 0.10-0.18 along the run: from
        // the perpendicular the wall reads broken and doubled (and "broken
        // wall" already means "shootable" here — the breach taught it),
        // while the 0.12 m depth gap at the joint is an oblique sight
        // corridor no body can pass (PLAYER_RADIUS 0.35). Both columns are
        // ordinary pen=1 solids; no shared planes by construction. Its
        // gates (wallbang sweep, bot census, picture review) ran at the
        // 2026-08-25 layout window — the pre-agreed fallback is to cut it.
        float m = (wlo + whi) * 0.5f + rng_range(-0.6f, 0.6f);
        float ov = rng_range(0.10f, 0.18f) * 0.5f;
        float dmid = ax ? (mn.z + mx.z) * 0.5f : (mn.x + mx.x) * 0.5f;
        float dsh = 0.12f;   // each column's outward shift; gap = 2*dsh - 0.12
        // The columns poke 0.12 m proud of the wall line, and the RING_W
        // corridor is documented cover-free: a host that close to the ring
        // stays an ordinary damaged wall instead.
        if (fabsf(dmid) + 0.44f > H - RING_W) {
          g_meta.sig = SIG_NONE;   // the print may not claim what is not built
          map_add_wall_sym(mn, mx, wcol, ax);
          break;
        }
        v3 amn = mn, amx = mx, bmn = mn, bmx = mx;
        if (ax) {
          amx.x = m + ov; bmn.x = m - ov;
          amn.z = dmid - 0.30f - dsh; amx.z = dmid + 0.30f - dsh - 0.24f;
          bmn.z = dmid - 0.30f + dsh + 0.24f; bmx.z = dmid + 0.30f + dsh;
        } else {
          amx.z = m + ov; bmn.z = m - ov;
          amn.x = dmid - 0.30f - dsh; amx.x = dmid + 0.30f - dsh - 0.24f;
          bmn.x = dmid - 0.30f + dsh + 0.24f; bmx.x = dmid + 0.30f + dsh;
        }
        map_add_sym(amn, amx, wcol);
        map_add_sym(bmn, bmx, col_tint(g_theme.cover, 0.90f, 1.00f));
        // Advertisement on BOTH faces: spill lines at the joint, both sides.
        meta_edge(wall_pt(mn, mx, ax, m - 0.8f, 0.0f),
                  wall_pt(mn, mx, ax, m + 0.8f, 0.0f), ME_FOOT, ax);
        sig_wall_done = 1;
      } else {
        map_add_wall_sym(mn, mx, wcol, ax);
      }
      break;
    }
  }
  if ((g_meta.sig == SIG_COLLAPSE || g_meta.sig == SIG_SPLIT) && !sig_wall_done)
    g_meta.sig = SIG_NONE;   // placement starved out: no phantom signature

  // THE DEAD TREES — a signature PAIR, because a solid must be symmetric to
  // be fair cover and a single off-centre trunk is neither: two dead trees
  // facing each other across the arena, which reads as composition rather
  // than as accident. The trunk is the SOLID (0.16 m of path — every round
  // crosses it, only bodies stop); the crown is cast-only world geometry
  // built by mesh_build off g_meta, wind-swept like everything else here.
  if (g_meta.sig == SIG_TREE) {
    for (int t = 0; t < 24; t++) {
      float r = rng_range(11.0f, H - RING_W - 1.2f);
      float a = rng_range(0.0f, 2.0f * F_PI);
      float tx = cosf(a) * r, tz = sinf(a) * r;
      // Sun-side preferred: golden-hour seeds should throw the crown's REAL
      // shadow across the floor, and the sun is a fixed world direction.
      if (t < 12 && tx * g_sun.dir.x + tz * g_sun.dir.z < 0.0f) continue;
      float th_ = rng_range(2.2f, 3.0f);
      v3 tmn = {{tx - 0.08f, 0, tz - 0.08f}}, tmx = {{tx + 0.08f, th_, tz + 0.08f}};
      if (!map_sym_free(tmn, tmx, 2.2f)) continue;
      g_meta.tree[0] = (int16_t)g_num_solids;
      g_meta.tree[1] = (int16_t)(g_num_solids + 1);
      map_add_sym(tmn, tmx, (v3){{0.30f, 0.26f, 0.21f}});
      break;
    }
    if (g_meta.tree[0] < 0) g_meta.sig = SIG_NONE;   // no room: no signature
  }

  // THE BOULDER PAIR — two ~1.1 m twinned boulders, ordinary solids with a
  // chamfered RENDER (mesh_build reads the indices): real cover that did not
  // come off a truck.
  if (g_meta.sig == SIG_BOULDERS) {
    for (int t = 0; t < 24; t++) {
      float hx = rng_range(0.50f, 0.62f), hz = rng_range(0.46f, 0.60f);
      float bh = rng_range(1.00f, 1.20f);
      float cx = rng_range(-(H - RING_W - 1.2f), H - RING_W - 1.2f);
      float cz = rng_range(-(H - RING_W - 1.2f), H - RING_W - 1.2f);
      if (cx * cx + cz * cz < 52.0f) continue;
      v3 bmn = {{cx - hx, 0, cz - hz}}, bmx = {{cx + hx, bh, cz + hz}};
      if (!map_sym_free(bmn, bmx, 1.8f)) continue;
      g_meta.bould[0] = (int16_t)g_num_solids;
      g_meta.bould[1] = (int16_t)(g_num_solids + 1);
      // Granite grey, NOT the cover family's rust: orange means man-made
      // cover in this arena's grammar, and a rock may not borrow it.
      { v3 gr = v3_lerp(g_theme.wall, (v3){{0.42f, 0.41f, 0.40f}}, 0.55f);
        map_add_sym(bmn, bmx, col_tint(gr, 0.94f, 1.04f)); }
      break;
    }
    if (g_meta.bould[0] < 0) g_meta.sig = SIG_NONE;
  }
}

static void map_gen_crates(void) {
  // Re-read, not threaded: ARENA_HALF is a compile-time constant, so this
  // local matches every sibling map_gen_* copy — bit-identical by construction.
  const float H = ARENA_HALF;
  // Crate clusters: anchor crate + optional jump-step + optional top box.
  int nclust = 4 + (int)(rng_u32() % 2);
  for (int i = 0; i < nclust && g_num_solids < MAX_SOLIDS - 8; i++) {
    for (int t = 0; t < 24; t++) {
      float hx = rng_range(0.7f, 1.15f), hz = rng_range(0.7f, 1.15f);
      float h = rng_range(1.0f, 1.3f);
      float cx = rng_range(-(H - RING_W - 1.2f), H - RING_W - 1.2f);
      float cz = rng_range(-(H - RING_W - 1.2f), H - RING_W - 1.2f);
      if (cx * cx + cz * cz < 42.0f) continue;
      v3 mn = {{cx - hx, 0, cz - hz}}, mx = {{cx + hx, h, cz + hz}};
      if (!map_sym_free(mn, mx, 2.2f)) continue;  // room incl. attachments
      map_add_sym(mn, mx, col_tint(g_theme.crate_a, 0.93f, 1.07f));
      uint32_t roll = rng_u32();
      if (roll & 1) {  // step crate: jump-height route onto the anchor
        float sh = rng_range(0.5f, 0.62f), sw = rng_range(0.45f, 0.6f);
        float sd = rng_range(0.4f, 0.55f);
        v3 smn, smx;  // overlaps the anchor 2 cm: no coplanar faces
        switch ((roll >> 1) & 3) {
        case 0:  smn = (v3){{mx.x - 0.02f, 0, cz - sw}};      smx = (v3){{mx.x + 2 * sd, sh, cz + sw}}; break;
        case 1:  smn = (v3){{mn.x - 2 * sd, 0, cz - sw}};     smx = (v3){{mn.x + 0.02f, sh, cz + sw}}; break;
        case 2:  smn = (v3){{cx - sw, 0, mx.z - 0.02f}};      smx = (v3){{cx + sw, sh, mx.z + 2 * sd}}; break;
        default: smn = (v3){{cx - sw, 0, mn.z - 2 * sd}};     smx = (v3){{cx + sw, sh, mn.z + 0.02f}}; break;
        }
        // The step extends up to 2*sd past the anchor with no bounds check of its own,
        // and the ANCHOR's placement bound only covers the anchor: ~1 in 12 seeds put a
        // step crate up to a metre inside the RING_W corridor that is documented cover-
        // free (rotation routes, the four long sightlines, and every rig that stages
        // there BECAUSE it is cover-free).
        if (smn.x >= -(H - RING_W) && smn.z >= -(H - RING_W) &&
            smx.x <= H - RING_W && smx.z <= H - RING_W)
          map_add_sym(smn, smx, col_tint(g_theme.crate_b, 0.93f, 1.07f));
      }
      if (((roll >> 3) & 63) < 28) {  // top box: silhouette variety
        float tx = hx * rng_range(0.5f, 0.7f), tz = hz * rng_range(0.5f, 0.7f);
        float th = rng_range(0.4f, 0.55f);
        // Inset by 3 cm, not flush: at the extremes of this range the top box's
        // side face lands on the anchor's, and two same-facing coplanar faces
        // z-fight at every distance. `mapcheck` counts exactly that.
        float ox = rng_range(-(hx - tx - 0.03f), hx - tx - 0.03f);
        float oz = rng_range(-(hz - tz - 0.03f), hz - tz - 0.03f);
        map_add_sym((v3){{cx + ox - tx, h - 0.02f, cz + oz - tz}},
                    (v3){{cx + ox + tx, h + th, cz + oz + tz}},
                    col_tint(g_theme.crate_b, 0.93f, 1.07f));
      }
      break;
    }
  }
}

static void map_generate(uint32_t seed) {
  g_rng = (uint64_t)seed * 2654435761u + 1;
  for (int h = 0; h < (int)(sizeof g_rng_gun / sizeof g_rng_gun[0]); h++)
    g_rng_gun[h] = rng_gun_seed(seed, h);
  g_rng_bot = (uint64_t)(seed ^ 0xB07u) * 2654435761u + 1;
  g_num_solids = 0;
  sun_build(seed);   // hashed off the seed, like the wind; see sun_build
  g_theme = THEMES[rng_u32() % (sizeof THEMES / sizeof THEMES[0])];
  // Mood then signature, both seed-hashed, both AFTER the theme (their
  // tables are per-theme) and BEFORE any geometry (their numbers feed the
  // generator's own probabilities).
  mood_build(seed, g_theme.surf);
  sig_build(seed, g_theme.surf, g_mood_preset);
  {  // see wind_dir(): hashed off the seed, never drawn from the map stream
    uint32_t wh = seed * 2246822519u;
    wh ^= wh >> 13; wh *= 3266489917u; wh ^= wh >> 16;
    g_wind_ang = (float)(wh >> 8) * (1.0f / 16777216.0f) * (2.0f * F_PI);
  }

  map_gen_shell();
  map_gen_landmark();
  map_gen_walls();
  map_gen_crates();
}

