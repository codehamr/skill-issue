// =============================================================================
// world/scatter.inc  —  scat_plan, gbake_stamp/gbake_smear, ground_bake.
// Proof: scat, mapcheck. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: scat_plan runs in mesh_build BEFORE ground_bake, so a stamp can
// mark the floor under a planned object; randomness is a per-candidate hash
// (scat_h), never a stream, so adding a family cannot re-roll another.
// =============================================================================
// ===========================================================================
// THE SCATTER PLAN — placement decided as DATA before any geometry exists.
// ===========================================================================
// PLAN -> STAMP -> EMIT. mesh_build runs the plan BEFORE the ground bake so a
// later pass can stamp what was planned into the floor (a stain under a rock
// needs the rock's position before the ground verts are written), and the
// emitters in mesh_decor read the same array. The budget fills top-down by
// silhouette value — rocks are planned before stones before pebbles before
// tufts, so when SCAT_MAX runs out what is dropped is a pebble, never a rock.
//
// DETERMINISM IS PER-CANDIDATE HASHING, NOT A STREAM. Every candidate's
// randomness is a pure function of (map seed, family, cell identity, k):
// adding a family or reordering passes can never re-roll another family,
// which is the fragility a serial rng stream has. uint32 mixing only — the
// fhash2 lesson: a float-scaled cast to int32 is UB and -ffast-math is
// entitled to eat it.
//
// THE SYMMETRY CONTRACT (statistical, not geometric). The plan works on the
// CANONICAL half (z > 0, or x > 0 on the z = 0 strip); the emitter mints both
// 180-degree twins from each site, so twins always match in count, family and
// size — fairness by construction. Wind cues cannot survive that rotation
// geometrically (a lee side maps onto the twin's windward side), so they are
// split: AXIS cues (elongation, band alignment) rotate exactly; SIDE cues are
// re-derived per twin — free-field items build their geometry against
// dec_rot(wind) so the rotated copy leans down the TRUE wind, and
// face-attached items emit the twin on the twin solid's own SAME-NORMAL face
// (scat_reflect + rotation), which is its true lee. The freedom test runs on
// BOTH twins' real positions at plan time; if either fails, the pair drops —
// so the fairness invariant is structural, never flaky.
typedef struct {
  v3       pos;    // canonical-half emit position
  float    s;      // the family's size scalar (also the footprint margin)
  uint32_t h;      // the candidate's own hash: emit derives detail from it
  int16_t  host;   // attached face's solid, or -1 = free-field (twin = -pos)
  uint8_t  fam, var;  // var: face axis for attached sites, variant bits else
} scat_t;
enum { SCAT_ROCK, SCAT_STONE, SCAT_PEBBLE, SCAT_TUFT, SCAT_PAVE, SCAT_SHRUB,
       SCAT_CACTUS, SCAT_TUMBLE, SCAT_BRICK, SCAT_FAM_COUNT };
// [[maybe_unused]] for upd_status_line's reason: its only reader is the
// harness `scat` command, which is Linux-only — on MinGW that is
// -Wunused-variable, i.e. a broken release build.
[[maybe_unused]] static const char *SCAT_FAM_NAME[SCAT_FAM_COUNT] = {
  "rock", "stone", "pebble", "tuft", "pave", "shrub", "cactus", "tumble",
  "brick"};
#define SCAT_MAX 768
static scat_t g_scat[SCAT_MAX];
static int    g_scat_n;
static int    g_scat_fam[SCAT_FAM_COUNT];
static float  g_scat_ms;    // plan CPU time; the harness `scat` prints it
static float  g_scat_top;   // tallest proud decor point; `scat` prints it —
                            // the two-class cover rule (< 0.30 m) as a number

static uint32_t scat_h(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  uint32_t h = g_map_seed * 2654435761u ^ 0xA3C59AC3u;
  h ^= a * 2246822519u; h = (h ^ (h >> 15)) * 2654435761u;
  h ^= b * 3266489917u; h = (h ^ (h >> 13)) * 2246822519u;
  h ^= c * 2654435761u; h = (h ^ (h >> 16)) * 3266489917u;
  h ^= d * 2246822519u; h ^= h >> 15; h *= 2654435761u; h ^= h >> 13;
  return h;
}
static float scat01(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  return (float)(scat_h(a, b, c, d) >> 8) * (1.0f / 16777216.0f);
}

// The one copy of "does this solid dress its surroundings": tall enough to be
// a wall (rubble dresses nothing), actually tall (not a thin slab), and
// standing on the ground (a stacked top box would collect debris in mid-air).
static int scat_host_ok(const solid_t *s) {
  return s->max.y >= 0.42f && s->max.y - s->min.y >= 0.42f &&
         s->min.y <= 0.05f;
}

// 2D distance from a point to a solid's footprint, 0 inside.
static float scat_box_d(const solid_t *s, float x, float z) {
  float dx = fmaxf(fmaxf(s->min.x - x, x - s->max.x), 0.0f);
  float dz = fmaxf(fmaxf(s->min.z - z, z - s->max.z), 0.0f);
  return sqrtf(dx * dx + dz * dz);
}

// Hard exclusions: outside the arena, inside a spawn box, or overlapping ANY
// low solid's footprint (rubble included — a rock half inside a rubble block
// is the coplanar defect in decor form). Symmetric by construction for
// free-field sites, because the solid set and the spawn boxes are.
static int scat_free(float x, float z, float r) {
  float H = ARENA_HALF;
  if (fabsf(x) > H - 0.30f - r || fabsf(z) > H - 0.30f - r) return 0;
  for (int s = -1; s <= 1; s += 2)
    if (fabsf(x - SPAWN_POS.x) < 4.0f + r &&
        fabsf(z - SPAWN_POS.z * (float)s) < 4.0f + r)
      return 0;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *so = &g_solids[i];
    if (so->min.y > 0.40f) continue;   // a floating box shadows no floor
    if (scat_box_d(so, x, z) < r + 0.05f) return 0;
  }
  return 1;
}

// Nearest two QUALIFYING walls: distance to the nearest (the wall mask), the
// nearest one's index (its colour seeds the stone tint), and whether the point
// sits in a concave corner — two walls close on differing axes, where debris
// drifts and the wall/floor line is hardest to hide.
static float scat_wall(float x, float z, int *idx, int *corner) {
  float d1 = 1e9f, d2 = 1e9f;
  v3 n1 = {{0, 0, 0}}, n2 = {{0, 0, 0}};
  int best = -1;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (!scat_host_ok(s)) continue;
    float d = scat_box_d(s, x, z);
    float cx = f_clamp(x, s->min.x, s->max.x);
    float cz = f_clamp(z, s->min.z, s->max.z);
    v3 n = {{x - cx, 0.0f, z - cz}};
    float l = sqrtf(n.x * n.x + n.z * n.z);
    if (l > 1e-4f) n = v3_scale(n, 1.0f / l);
    if (d < d1) { d2 = d1; n2 = n1; d1 = d; n1 = n; best = i; }
    else if (d < d2) { d2 = d; n2 = n; }
  }
  if (idx) *idx = best;
  if (corner)
    *corner = d1 < 1.0f && d2 < 1.0f &&
              fabsf(n1.x * n2.x + n1.z * n2.z) < 0.70f;
  return d1;
}

// The symmetric lee mask: "near a wall ALONG the wind axis, either side" —
// max(ws(p), ws(-p)) is trivially the same for both twins, so it may sit in
// the accept product; which side of its wall a site's geometry favours is
// decided per twin at emit, never here.
static float scat_lee_sym(float x, float z, v3 w) {
  float best = 0.0f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (!scat_host_ok(s)) continue;
    for (int sg = -1; sg <= 1; sg += 2) {
      float t = wind_shadow(s, x, z, w, (float)sg);
      if (t >= 0.0f) {
        float a = expf(-t / 3.0f);
        if (a > best) best = a;
      }
    }
  }
  return best;
}

// A face site's twin: reflect through the host's centre along the face axis,
// then the emitter's 180-degree rotation lands it on the twin solid's
// same-normal face — its true lee. See the symmetry contract above.
static v3 scat_reflect(v3 p, const solid_t *s, int axis) {
  if (axis == 0) p.x = s->min.x + s->max.x - p.x;
  else           p.z = s->min.z + s->max.z - p.z;
  return p;
}

static void scat_push(v3 pos, float s, uint32_t h, int host, int fam,
                      int var) {
  if (g_scat_n >= SCAT_MAX) return;
  g_scat[g_scat_n++] = (scat_t){pos, s, h, (int16_t)host, (uint8_t)fam,
                                (uint8_t)var};
  g_scat_fam[fam]++;
}

// The plan pass. Density is a PRODUCT of physical factors — wall distance,
// corners, the wind slab, 7 m patch noise — so the placement explains itself;
// uniform scatter is banned. Children cluster on parents (golden-angle
// offsets, sizes falling by generation) because detail attracts detail.
static void scat_plan_rocks(void) {
  float H = ARENA_HALF;
  float strew = g_mood.strew, growth = g_mood.growth;
  int grass = g_theme.surf != SURF_WET;
  // ROCK — jittered grid, cell 3.0 m, canonical half. The theme's bedrock:
  // each accepted rock immediately seeds its children (stones), a pebble ring
  // and a facilitation tuft or two, all keyed off the rock's own cell so no
  // ordering can re-roll them.
  for (int iz = 0; iz < 8; iz++)
    for (int ix = -8; ix < 8; ix++) {
      uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
      float x = ((float)ix + 0.075f + 0.85f * scat01(SCAT_ROCK, cx, cz, 1)) * 3.0f;
      float z = ((float)iz + 0.075f + 0.85f * scat01(SCAT_ROCK, cx, cz, 2)) * 3.0f;
      if (z < 0.45f) continue;
      if (x * x + z * z < 6.5f * 6.5f) continue;            // the mid reserve
      if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue; // the wall ring
      int wi, corner;
      float wd = scat_wall(x, z, &wi, &corner);
      // A full cluster in the open middle is the most detailed thing this
      // pass makes, parked exactly where the fair-silhouette rule wants calm
      // — so the open floor gets a 4 % tail, not a 32 % base. The middle
      // belongs to pebbles.
      float wallM = 0.04f + 0.96f * f_smoothstep(3.8f, 0.6f, wd);
      float noiseM = f_smoothstep(0.30f, 0.62f,
                                  fnoise2(x * 0.143f + 17.0f, z * 0.143f - 9.0f));
      float cornerM = corner ? 2.2f : 1.0f;
      float viab = (0.22f + 0.42f * strew) * wallM * noiseM * cornerM;
      if (scat01(SCAT_ROCK, cx, cz, 0) > viab) continue;
      uint32_t h = scat_h(SCAT_ROCK, cx, cz, 3);
      uint64_t st = (uint64_t)h * 2654435761u + 1;
      float ry = rng_rangef(&st, 0.09f, 0.185f);
      if (!scat_free(x, z, ry * 1.7f)) continue;
      scat_push((v3){{x, 0.0f, z}}, ry, h, -1, SCAT_ROCK, 0);
      // children: 2-5 stones at golden-angle offsets, one pebble ring, and —
      // on grass themes — the tuft or two that collects beside a stone.
      int nc = 2 + (int)(scat_h(SCAT_STONE, cx, cz, 90) % 4u);
      for (int k = 0; k < nc; k++) {
        uint64_t cs = (uint64_t)scat_h(SCAT_STONE, cx, cz, (uint32_t)k) *
                      2654435761u + 1;
        float ang = (float)k * 2.39996f + rng_rangef(&cs, -0.5f, 0.5f);
        float u = rng_rangef(&cs, 0.0f, 1.0f);
        float dist = ry * (2.2f + 3.4f * u * u);
        float sx = x + cosf(ang) * dist, sz = z + sinf(ang) * dist;
        float sr = ry * rng_rangef(&cs, 0.35f, 0.60f);
        if (sz < 0.30f || !scat_free(sx, sz, sr * 1.6f)) continue;
        if (sx * sx + sz * sz < 6.5f * 6.5f) continue;
        scat_push((v3){{sx, 0.0f, sz}}, sr,
                  scat_h(SCAT_STONE, cx, cz, (uint32_t)k + 8), -1,
                  SCAT_STONE, 0);
      }
      int np = 4 + (int)(scat_h(SCAT_PEBBLE, cx, cz, 91) % 5u);
      for (int k = 0; k < np; k++) {
        uint64_t cs = (uint64_t)scat_h(SCAT_PEBBLE, cx, cz, (uint32_t)k) *
                      2654435761u + 1;
        float ang = rng_rangef(&cs, 0.0f, 6.283f);
        float dist = ry * rng_rangef(&cs, 3.2f, 5.6f);
        float sx = x + cosf(ang) * dist, sz = z + sinf(ang) * dist;
        if (sz < 0.15f || !scat_free(sx, sz, 0.05f)) continue;
        if (sx * sx + sz * sz < 5.5f * 5.5f) continue;
        scat_push((v3){{sx, 0.0f, sz}}, rng_rangef(&cs, 0.012f, 0.028f),
                  scat_h(SCAT_PEBBLE, cx, cz, (uint32_t)k + 16), -1,
                  SCAT_PEBBLE, 0);
      }
      if (grass && scat01(SCAT_TUFT, cx, cz, 92) < 0.20f + 0.65f * growth) {
        int nt = 1 + (int)(scat_h(SCAT_TUFT, cx, cz, 93) % 2u);
        for (int k = 0; k < nt; k++) {
          uint64_t cs = (uint64_t)scat_h(SCAT_TUFT, cx, cz, (uint32_t)k + 24) *
                        2654435761u + 1;
          float ang = rng_rangef(&cs, 0.0f, 6.283f);
          // Tight against the rock: at 4 radii the tuft read as orphaned.
          float dist = ry * rng_rangef(&cs, 1.8f, 3.2f);
          float sx = x + cosf(ang) * dist, sz = z + sinf(ang) * dist;
          if (sz < 0.15f || !scat_free(sx, sz, 0.09f)) continue;
          if (sx * sx + sz * sz < 6.5f * 6.5f) continue;
          // 1-in-8 draws the green variant, and only HERE — beside a rock is
          // where the moisture logic puts it (var bit 1).
          int green = (scat_h(SCAT_TUFT, cx, cz, (uint32_t)k + 32) & 7u) == 0;
          scat_push((v3){{sx, 0.0f, sz}}, rng_rangef(&cs, 0.7f, 1.15f),
                    scat_h(SCAT_TUFT, cx, cz, (uint32_t)k + 40), -1,
                    SCAT_TUFT, green ? 2 : 0);
        }
      }
    }

  // STONE, field tier — the wall-hugging clutter the old clump walk placed,
  // now mask-driven: smoothstep(1.5, 0.2, d) IS the recipe, near-zero in the
  // open middle.
  for (int iz = 0; iz < 11; iz++)
    for (int ix = -11; ix < 11; ix++) {
      uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
      float x = ((float)ix + 0.075f + 0.85f * scat01(SCAT_STONE, cx, cz, 101)) * 2.2f;
      float z = ((float)iz + 0.075f + 0.85f * scat01(SCAT_STONE, cx, cz, 102)) * 2.2f;
      if (z < 0.30f) continue;
      if (x * x + z * z < 6.5f * 6.5f) continue;
      if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue;
      int wi, corner;
      float wd = scat_wall(x, z, &wi, &corner);
      float wallM = f_smoothstep(1.5f, 0.2f, wd);
      float noiseM = 0.35f + 0.65f * fnoise2(x * 0.62f, z * 0.62f);
      float cornerM = corner ? 2.6f : 1.0f;
      // DRAINED promotes the wall-foot stones to scree fans — THE identity of
      // that mood; QUARRY's talus takes a smaller lift on top of its strew.
      float scree = (g_theme.surf == SURF_WET && g_mood_preset == 1) ? 1.9f
                  : (g_theme.surf == SURF_GRIT && g_mood_preset == 0) ? 1.4f
                                                                      : 1.0f;
      float viab = (0.16f + 0.34f * strew) * wallM * noiseM * cornerM * scree;
      if (scat01(SCAT_STONE, cx, cz, 100) > viab) continue;
      uint32_t h = scat_h(SCAT_STONE, cx, cz, 103);
      uint64_t st = (uint64_t)h * 2654435761u + 1;
      float r = rng_rangef(&st, 0.035f, 0.095f);
      if (!scat_free(x, z, r * 1.7f)) continue;
      scat_push((v3){{x, 0.0f, z}}, r, h, -1, SCAT_STONE, 0);
    }
}

static void scat_plan_ground(void) {
  // Re-read, not threaded: wind_dir() and g_mood are map state set once in
  // map_generate's frame and unmutated here; ARENA_HALF is a compile-time
  // constant — all three bit-identical by construction.
  v3 w = wind_dir();
  float H = ARENA_HALF;
  float strew = g_mood.strew, growth = g_mood.growth;
  int grass = g_theme.surf != SURF_WET;
  // PEBBLE, field tier — the fines that survive as mesh. Sparse everywhere
  // walls are near; allowed closer to the middle than anything else (they
  // obstruct nothing and the landmark should not stand on swept concrete).
  for (int iz = 0; iz < 22; iz++)
    for (int ix = -22; ix < 22; ix++) {
      uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
      float x = ((float)ix + 0.075f + 0.85f * scat01(SCAT_PEBBLE, cx, cz, 111)) * 1.1f;
      float z = ((float)iz + 0.075f + 0.85f * scat01(SCAT_PEBBLE, cx, cz, 112)) * 1.1f;
      if (z < 0.15f) continue;
      if (x * x + z * z < 5.5f * 5.5f) continue;
      float ring = fmaxf(fabsf(x), fabsf(z));
      int wi, corner;
      float wd = scat_wall(x, z, &wi, &corner);
      if (ring > H - RING_W && wd > 0.9f) continue;  // ring: wall base only
      float wallM = 0.28f + 0.72f * f_smoothstep(2.0f, 0.3f, wd);
      float noiseM = 0.30f + 0.70f * fnoise2(x * 0.62f + 5.0f, z * 0.62f - 3.0f);
      float viab = (0.10f + 0.26f * strew) * wallM * noiseM *
                   (corner ? 2.0f : 1.0f);
      if (scat01(SCAT_PEBBLE, cx, cz, 110) > viab) continue;
      uint32_t h = scat_h(SCAT_PEBBLE, cx, cz, 113);
      uint64_t st = (uint64_t)h * 2654435761u + 1;
      float r = rng_rangef(&st, 0.012f, 0.028f);
      if (!scat_free(x, z, r + 0.02f)) continue;
      scat_push((v3){{x, 0.0f, z}}, r, h, -1, SCAT_PEBBLE, 0);
    }

  // TUFT, band tier — tiger-bush logic: grass collects along wall FEET, and
  // preferentially on each wall's LEE side. Acceptance is per canonical FACE
  // (a face and its twin share a normal, so they share a lee — the pair draws
  // one roll), which is what keeps the twins fair while each face's own
  // density follows its own lee.
  if (grass)
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *s = &g_solids[i];
      if (!scat_host_ok(s)) continue;
      // Not the shell: its twin is the OPPOSITE wall, which breaks the
      // scat_reflect pairing (see the drift walk's note) — every shell pair
      // was silently dropped by the both-twins-free test anyway, so this is
      // the same outcome without the wasted planning.
      if (s->pen == 0) continue;
      for (int f = 0; f < 4; f++) {
        v3 o = {{f == 0 ? 1.0f : f == 1 ? -1.0f : 0.0f, 0.0f,
                 f == 2 ? 1.0f : f == 3 ? -1.0f : 0.0f}};
        float base = f == 0 ? s->max.x : f == 1 ? s->min.x
                   : f == 2 ? s->max.z : s->min.z;
        float lo = (f < 2) ? s->min.z : s->min.x;
        float hi = (f < 2) ? s->max.z : s->max.x;
        if (hi - lo < 0.7f) continue;
        // the canonical face of the pair
        float cmx = (f < 2) ? base : (lo + hi) * 0.5f;
        float cmz = (f < 2) ? (lo + hi) * 0.5f : base;
        if (cmz < -0.001f || (fabsf(cmz) <= 0.001f && cmx <= 0.0f)) continue;
        float lee01 = f_clamp(-(o.x * w.x + o.z * w.z), 0.0f, 1.0f);
        // High enough that a lee face actually reads as a BAND: at the first
        // rate a 5 m face held ~2 tufts and the review could not find the
        // bands at all.
        float rate = growth * (0.16f + 0.92f * lee01);
        int k = 0;
        for (float t = lo + 0.35f; t < hi - 0.35f; t += 0.85f, k++) {
          if (scat01(SCAT_TUFT, (uint32_t)i, (uint32_t)f, (uint32_t)k) > rate)
            continue;
          uint32_t h = scat_h(SCAT_TUFT, (uint32_t)i, (uint32_t)f,
                              (uint32_t)k + 200);
          uint64_t st = (uint64_t)h * 2654435761u + 1;
          float off = rng_rangef(&st, 0.14f, 0.36f);
          float tt = t + rng_rangef(&st, -0.25f, 0.25f);
          v3 p = (f < 2) ? (v3){{base + o.x * off, 0.0f, tt}}
                         : (v3){{tt, 0.0f, base + o.z * off}};
          int axis = f < 2 ? 0 : 1;
          v3 q = scat_reflect(p, s, axis);
          // both twins' REAL positions must be free, or the pair drops
          if (!scat_free(p.x, p.z, 0.05f) || !scat_free(-q.x, -q.z, 0.05f))
            continue;
          scat_push(p, rng_rangef(&st, 0.7f, 1.15f), h, i, SCAT_TUFT, axis);
        }
      }
    }

  // TUFT, field tier — the patchy fill between the bands: grass collects in
  // NOISE PATCHES near any cover, never in the open middle, and it is what
  // makes a growth mood read as growth from twenty metres rather than only
  // in a crop. Band + facilitation tufts alone were 11-39 sites against the
  // 40-90 the budget prices; this pass is the difference.
  if (grass)
    for (int iz = 0; iz < 18; iz++)
      for (int ix = -18; ix < 18; ix++) {
        uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
        float x = ((float)ix + 0.075f + 0.85f * scat01(SCAT_TUFT, cx, cz, 301)) * 1.3f;
        float z = ((float)iz + 0.075f + 0.85f * scat01(SCAT_TUFT, cx, cz, 302)) * 1.3f;
        if (z < 0.2f) continue;
        if (x * x + z * z < 6.5f * 6.5f) continue;
        if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue;
        int wi2, c2;
        float wd = scat_wall(x, z, &wi2, &c2);
        float patch = f_smoothstep(0.36f, 0.60f,
                                   fnoise2(x * 0.19f - 23.0f, z * 0.19f + 41.0f));
        // Grass reaches FURTHER off the walls than mineral clutter does —
        // tiger-bush bands are metres wide — so the growth moods can be read
        // from the play camera; the mid reserve above still keeps the true
        // middle calm, and the patch noise keeps the reach from being a wash.
        float viab = growth * (0.05f + 0.50f * f_smoothstep(5.5f, 0.8f, wd)) *
                     patch;
        if (scat01(SCAT_TUFT, cx, cz, 300) > viab) continue;
        uint32_t h = scat_h(SCAT_TUFT, cx, cz, 303);
        uint64_t st = (uint64_t)h * 2654435761u + 1;
        if (!scat_free(x, z, 0.09f)) continue;
        scat_push((v3){{x, 0.0f, z}}, rng_rangef(&st, 0.65f, 1.10f), h, -1,
                  SCAT_TUFT, 0);
      }

  // PAVE — desert pavement / scree sheet: one or two 2.8-4 m patches of flat
  // embedded stones, the STONEFIELD and DRAINED moods' open-floor identity.
  // The one family that WANTS the open field, which is fair because nothing
  // in it stands over two centimetres proud.
  int pave_on = (g_theme.surf == SURF_SAND && g_mood_preset == 1) ||
                (g_theme.surf == SURF_WET && g_mood_preset == 1);
  if (pave_on) {
    int npatch = 0;
    for (int iz = 0; iz < 3 && npatch < 2; iz++)
      for (int ix = -3; ix < 3 && npatch < 2; ix++) {
        uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
        float x = ((float)ix + 0.15f + 0.7f * scat01(SCAT_PAVE, cx, cz, 1)) * 8.0f;
        float z = ((float)iz + 0.15f + 0.7f * scat01(SCAT_PAVE, cx, cz, 2)) * 8.0f;
        if (z < 1.8f) continue;
        if (x * x + z * z < 8.0f * 8.0f) continue;
        if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W - 1.2f) continue;
        int wi2, c2;
        if (scat_wall(x, z, &wi2, &c2) < 2.0f) continue;   // open-field family
        if (scat01(SCAT_PAVE, cx, cz, 0) > 0.85f) continue;
        uint32_t h = scat_h(SCAT_PAVE, cx, cz, 3);
        uint64_t st = (uint64_t)h * 2654435761u + 1;
        float pr = rng_rangef(&st, 1.4f, 2.0f);
        if (!scat_free(x, z, 0.3f)) continue;
        scat_push((v3){{x, 0.0f, z}}, pr, h, -1, SCAT_PAVE, 0);
        npatch++;
      }
  }
}

static void scat_plan_flora(void) {
  // Re-read, not threaded: same as scat_plan_ground's copy above — wind_dir()
  // and g_mood are map state set once in map_generate's frame, ARENA_HALF a
  // compile-time constant — bit-identical by construction.
  v3 w = wind_dir();
  float H = ARENA_HALF;
  float growth = g_mood.growth;
  int grass = g_theme.surf != SURF_WET;
  int moss = g_theme.surf == SURF_WET && g_mood_preset == 2;
  // SHRUB — openwork sticks at wall bases and beside rocks, never the open
  // field; the growth latent's woody half. On dry themes the upper tail gets
  // sparse olive leaves. MOSSTONE reuses exactly the same skeleton as low
  // fern fans: one construction, coherent ecology, no foliage draw call.
  if (grass || moss)
    for (int iz = 0; iz < 9; iz++)
      for (int ix = -9; ix < 9; ix++) {
        uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
        float x = ((float)ix + 0.075f + 0.85f * scat01(SCAT_SHRUB, cx, cz, 1)) * 2.6f;
        float z = ((float)iz + 0.075f + 0.85f * scat01(SCAT_SHRUB, cx, cz, 2)) * 2.6f;
        if (z < 0.3f) continue;
        if (x * x + z * z < 6.5f * 6.5f) continue;
        if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue;
        int wi2, c2;
        float wd = scat_wall(x, z, &wi2, &c2);
        float vigour = moss ? 0.48f : growth;
        float viab = vigour * (0.06f + 0.55f * f_smoothstep(1.7f, 0.3f, wd)) *
                     (c2 ? 1.8f : 1.0f);
        if (scat01(SCAT_SHRUB, cx, cz, 0) > viab) continue;
        uint32_t h = scat_h(SCAT_SHRUB, cx, cz, 3);
        uint64_t st = (uint64_t)h * 2654435761u + 1;
        float r = rng_rangef(&st, 0.14f, 0.22f);
        if (!scat_free(x, z, r * 0.6f)) continue;
        int leafy = moss || (growth > 0.45f && (h & 3u) != 0u);
        scat_push((v3){{x, 0.0f, z}}, r, h, -1, SCAT_SHRUB,
                  (leafy ? 1 : 0) | (c2 ? 2 : 0) | (moss ? 4 : 0));
      }

  // SHELTERED CORNER GROVES — a jittered field can miss the exact place the
  // eye expects life: the pocket just beyond a wall end. Sample one candidate
  // corner per canonical cover solid and build a tiny facilitation cluster
  // there (bush + grass). It is deterministic per solid, paired by 180-degree
  // symmetry, non-colliding, and capped at four canonical sites so visual
  // richness cannot turn into geometry snow when the generator adds walls.
  if (grass || moss) {
    int groves = 0;
    float vigour = moss ? 0.62f : growth;
    for (int i = 0; i < g_num_solids && groves < 4; i++) {
      const solid_t *s = &g_solids[i];
      if (!scat_host_ok(s) || s->pen == 0) continue;
      float cx0 = (s->min.x + s->max.x) * 0.5f;
      float cz0 = (s->min.z + s->max.z) * 0.5f;
      if (cz0 < -0.001f || (fabsf(cz0) <= 0.001f && cx0 <= 0.0f)) continue;
      uint32_t h = scat_h(SCAT_SHRUB, (uint32_t)i, 0xC071u, 0);
      if (scat01(SCAT_SHRUB, (uint32_t)i, 0xC071u, 1) >
          0.16f + 0.62f * vigour)
        continue;
      uint64_t st = (uint64_t)h * 2654435761u + 1;
      float r = rng_rangef(&st, 0.17f, 0.215f);
      // Diagonal from the AABB corner clears both faces at once, so the
      // plant reads tucked into the endpoint without intersecting collision.
      int xp = (h >> 3) & 1u, zp = (h >> 7) & 1u;
      float off = 0.13f + r * 0.65f;
      v3 p = {{xp ? s->max.x + off : s->min.x - off, 0.0f,
               zp ? s->max.z + off : s->min.z - off}};
      // Keep the stored site canonical; its negative is the fair twin.
      if (p.z < -0.001f || (fabsf(p.z) <= 0.001f && p.x < 0.0f))
        p = v3_scale(p, -1.0f);
      if (p.x * p.x + p.z * p.z < 6.5f * 6.5f) continue;
      if (!scat_free(p.x, p.z, r * 0.60f) ||
          !scat_free(-p.x, -p.z, r * 0.60f))
        continue;
      scat_push(p, r, h, -1, SCAT_SHRUB, 1 | 2 | (moss ? 4 : 0));
      groves++;
      if (grass) {
        // Two small satellites make a patch rather than a placed prop. Their
        // golden-angle offsets are keyed to the grove and therefore cannot
        // re-roll the parent or any other scatter family.
        for (int k = 0; k < 2; k++) {
          float a = (float)k * 2.39996f + rng_rangef(&st, -0.35f, 0.35f);
          float d = rng_rangef(&st, 0.18f, 0.32f);
          v3 q = v3_add(p, (v3){{cosf(a) * d, 0.0f, sinf(a) * d}});
          if (!scat_free(q.x, q.z, 0.05f) ||
              !scat_free(-q.x, -q.z, 0.05f))
            continue;
          scat_push(q, rng_rangef(&st, 0.72f, 1.02f),
                    scat_h(SCAT_TUFT, (uint32_t)i, 0xC071u,
                           (uint32_t)k), -1, SCAT_TUFT, 2);
        }
      }
    }
  }

  // CACTUS — SAND, THORNS only: the one green mass in the desert, rare by
  // identity. Low pear fans and squat barrels; no saguaro, ever — a humanoid
  // silhouette at 25 m is the exact false positive the cover rule exists for.
  if (g_theme.surf == SURF_SAND && g_mood_preset == 2)
    for (int iz = 0; iz < 6; iz++)
      for (int ix = -6; ix < 6; ix++) {
        uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
        float x = ((float)ix + 0.1f + 0.8f * scat01(SCAT_CACTUS, cx, cz, 1)) * 4.0f;
        float z = ((float)iz + 0.1f + 0.8f * scat01(SCAT_CACTUS, cx, cz, 2)) * 4.0f;
        if (z < 0.4f) continue;
        if (x * x + z * z < 7.0f * 7.0f) continue;
        if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue;
        if (scat01(SCAT_CACTUS, cx, cz, 0) > 0.17f) continue;
        uint32_t h = scat_h(SCAT_CACTUS, cx, cz, 3);
        if (!scat_free(x, z, 0.16f)) continue;
        scat_push((v3){{x, 0.0f, z}}, 0.13f, h, -1, SCAT_CACTUS,
                  (int)(h & 1u));
      }

  // TUMBLEWEED — 0-2, resting where a tumbleweed actually stops: a concave
  // corner on the lee side. THORNS, STEPPE and SWEPT only (a dry-wind story).
  int tumble_on = (g_theme.surf == SURF_SAND && g_mood_preset == 2) ||
                  (g_theme.surf == SURF_GRIT && g_mood_preset != 0);
  if (tumble_on) {
    int nt = 0;
    for (int iz = 0; iz < 8 && nt < 2; iz++)
      for (int ix = -8; ix < 8 && nt < 2; ix++) {
        uint32_t cx = (uint32_t)(ix + 64), cz = (uint32_t)iz;
        float x = ((float)ix + 0.1f + 0.8f * scat01(SCAT_TUMBLE, cx, cz, 1)) * 3.0f;
        float z = ((float)iz + 0.1f + 0.8f * scat01(SCAT_TUMBLE, cx, cz, 2)) * 3.0f;
        if (z < 0.4f) continue;
        if (x * x + z * z < 6.5f * 6.5f) continue;
        if (fmaxf(fabsf(x), fabsf(z)) > H - RING_W) continue;
        int wi2, c2;
        float wd = scat_wall(x, z, &wi2, &c2);
        float viab = (c2 ? 0.55f : 0.06f) * f_smoothstep(1.2f, 0.3f, wd) *
                     (0.4f + 0.6f * scat_lee_sym(x, z, w));
        if (scat01(SCAT_TUMBLE, cx, cz, 0) > viab) continue;
        uint32_t h = scat_h(SCAT_TUMBLE, cx, cz, 3);
        uint64_t st = (uint64_t)h * 2654435761u + 1;
        float r = rng_rangef(&st, 0.13f, 0.165f);
        if (!scat_free(x, z, r)) continue;
        scat_push((v3){{x, 0.0f, z}}, r, h, -1, SCAT_TUMBLE, 0);
        nt++;
      }
  }
}

static void scat_plan_bricks(void) {
  // BRICK — the damage dressing: spill at every recorded FOOT line, rotated
  // edge bricks on the sills and lowered tops. Bricks attach to the meta
  // edges the generator published; count rides the ruin latent, because a
  // brick field is what "ruined" literally means here.
  for (int e = 0; e < g_meta.nedge; e++) {
    const medge_t *ed = &g_meta.edge[e];
    int nb = ed->kind == ME_FOOT
                 ? 3 + (int)(scat_h(SCAT_BRICK, (uint32_t)e, 90, 0) %
                             (uint32_t)(3.0f + 5.0f * g_mood.ruin))
             : ed->kind == ME_FRACTURE
                 ? 1 + (int)(scat_h(SCAT_BRICK, (uint32_t)e, 92, 0) % 2u)
                 : 2 + (int)(scat_h(SCAT_BRICK, (uint32_t)e, 91, 0) % 3u);
    for (int k = 0; k < nb; k++) {
      uint32_t h = scat_h(SCAT_BRICK, (uint32_t)e, (uint32_t)k, 7);
      uint64_t st = (uint64_t)h * 2654435761u + 1;
      float u = rng_rangef(&st, 0.05f, 0.95f);
      v3 p = v3_lerp(ed->a, ed->b, u);
      if (ed->kind == ME_FOOT) {
        // the spill spreads off the wall's line, never past a rubble's throw
        float side = rng_rangef(&st, -0.55f, 0.55f);
        if (ed->ax) p.z += side; else p.x += side;
        p.y = 0.0f;
        if (!scat_free(p.x, p.z, 0.10f)) continue;
      } else if (ed->kind == ME_FRACTURE) {
        // The metadata line lies on the wall centre plane. Put each tooth on
        // one actual face of the 60 cm wall so it protrudes into light rather
        // than disappearing inside the host solid.
        float face = (h & 1u) ? 0.315f : -0.315f;
        if (ed->ax) p.z += face; else p.x += face;
      }
      if (g_scat_n >= SCAT_MAX) break;
      scat_push(p, rng_rangef(&st, 0.85f, 1.10f), h, -1, SCAT_BRICK,
                (int)ed->kind | ((int)ed->ax << 4));
    }
  }
}

static void scat_plan(void) {
  clock_t t0 = clock();
  g_scat_n = 0;
  memset(g_scat_fam, 0, sizeof g_scat_fam);
  scat_plan_rocks();
  scat_plan_ground();
  scat_plan_flora();
  scat_plan_bricks();
  g_scat_ms = (float)(clock() - t0) * (1000.0f / (float)CLOCKS_PER_SEC);
}

#define DECOR_MAX_VERTS 49152
static float g_mesh[(MAX_SOLIDS * 36 + GROUND_VERTS + DECOR_MAX_VERTS) * VERT_FLOATS];
static int   g_mesh_floats;
static long  g_mesh_drops;

// The raw pusher. mesh_push below is the batch-state form every solid uses; the
// floor needs its three channels PER VERTEX, which is the whole point of baking
// them, so it writes through here instead of through the batch state.
static void mesh_vert(v3 p, v3 n, v3 c, float rough, float metal, float ao) {
  if (g_mesh_floats + VERT_FLOATS >
      (int)(sizeof g_mesh / sizeof g_mesh[0])) {
    // Unlike scene_push this used to write blind, because the array was sized
    // exactly for a full solid list and could not overflow. It can now.
    if (!g_mesh_drops++)
      fprintf(stderr, "mesh: world vertex budget exhausted — geometry dropped\n");
    return;
  }
  float *v = g_mesh + g_mesh_floats;
  v[0] = p.x; v[1] = p.y; v[2] = p.z;
  v[3] = n.x; v[4] = n.y; v[5] = n.z;
  v[6] = c.x; v[7] = c.y; v[8] = c.z;
  v[9] = rough; v[10] = metal; v[11] = ao;
  g_mesh_floats += VERT_FLOATS;
}

static void mesh_push(v3 p, v3 n, v3 c) {
  mesh_vert(p, n, c, g_mat_rough, g_mat_metal, ao_at(p.y));
}

static void mesh_quad(v3 a, v3 b, v3 c, v3 d, v3 n, v3 col) {
  mesh_push(a, n, col); mesh_push(b, n, col); mesh_push(c, n, col);
  mesh_push(a, n, col); mesh_push(c, n, col); mesh_push(d, n, col);
}

// One baked corner of the floor grid. Everything the bake decides lands in the three
// channels a vertex already carries, so the fragment shader pays nothing:
//   albedo    drift lightens (loose dry sand scatters more), traffic darkens (packed
//             ground is darker ground)
//   roughness loose sand is rougher than a packed lane
//   AO        only broad prop/damage stamps; narrow contact belongs to stable SSAO
//
// AND IT IS LOW-PASSED BEFORE IT IS EMITTED. Three values per vertex on a 1.5 m grid,
// linearly interpolated across two triangles, show their own lattice as soon as
// adjacent vertices disagree by more than the eye's threshold: the quad diagonal
// becomes a visible crease. One 3x3 pass removes exactly the vertex-to-vertex
// component the interpolation cannot hide, and costs nothing at runtime.
// The tint is a VECTOR now, not a value: the SLATE moods' waterline and moss
// bands are hue shifts, and a scalar multiplier cannot cool a band or green
// a foot line without also just darkening it.
typedef struct { v3 tint; float rough, ao; } gvert_t;
static gvert_t g_gbake[(GROUND_CELLS + 1) * (GROUND_CELLS + 1)];

// One stamp into the bake: multiply the cell's tint toward `to` and darken
// its AO, falling off over r from p. PAINT CONTINUES WHERE MESH BUDGET
// STOPS — the fines under a rock, the dust at a damage foot and the shadow a
// decor rock cannot cast are all cheaper as pigment than as vertices.
static void gbake_stamp(v3 p, float r, v3 to, float amt, float ao_amt) {
  float H = ARENA_HALF;
  int N = GROUND_CELLS + 1;
  int x0 = (int)((p.x - r + H) / GROUND_STEP), x1 = (int)((p.x + r + H) / GROUND_STEP) + 1;
  int z0 = (int)((p.z - r + H) / GROUND_STEP), z1 = (int)((p.z + r + H) / GROUND_STEP) + 1;
  if (x0 < 0) x0 = 0;
  if (z0 < 0) z0 = 0;
  if (x1 >= N) x1 = N - 1;
  if (z1 >= N) z1 = N - 1;
  for (int iz = z0; iz <= z1; iz++)
    for (int ix = x0; ix <= x1; ix++) {
      float x = -H + (float)ix * GROUND_STEP, z = -H + (float)iz * GROUND_STEP;
      float d = sqrtf((x - p.x) * (x - p.x) + (z - p.z) * (z - p.z));
      float k = f_smoothstep(r, r * 0.25f, d) * amt;
      if (k <= 0.0f) continue;
      gvert_t *g = &g_gbake[iz * N + ix];
      g->tint = v3_lerp(g->tint, (v3){{g->tint.x * to.x, g->tint.y * to.y,
                                       g->tint.z * to.z}}, k);
      g->ao *= 1.0f - ao_amt * k;
    }
}

// The same stamp smeared along a SEGMENT — the sun-shadow smears and the
// damage-foot dust lines. The same slab idea wind_shadow and shadow_foot
// already use, in its third suit.
static void gbake_smear(v3 a, v3 b, float r, v3 to, float amt, float ao_amt) {
  v3 d = v3_sub(b, a);
  float l = sqrtf(d.x * d.x + d.z * d.z);
  int n = (int)(l / (GROUND_STEP * 0.55f)) + 1;
  for (int i = 0; i <= n; i++) {
    float u = (float)i / (float)n;
    // the smear thins and fades toward its far end, the way a shadow does
    gbake_stamp(v3_lerp(a, b, u), r * (1.0f - 0.4f * u), to,
                amt * (1.0f - 0.65f * u), ao_amt * (1.0f - 0.65f * u));
  }
}

static void ground_bake(void) {
  float H = ARENA_HALF;
  int N = GROUND_CELLS + 1;
  for (int iz = 0; iz < N; iz++)
    for (int ix = 0; ix < N; ix++) {
      float x = -H + (float)ix * GROUND_STEP, z = -H + (float)iz * GROUND_STEP;
      gsurf_t g = ground_sample(x, z);
      float t = 1.0f + 0.33f * g.drift - 0.11f * g.compact;
      t *= 0.965f + 0.07f * fnoise2(x * 0.11f, z * 0.11f);
      float r = WORLD_GROUND_R + 0.13f * g.drift - 0.08f * g.compact;
      v3 tv = {{t, t, t}};
      g_gbake[iz * N + ix] = (gvert_t){tv, r, 1.0f};
    }
  // THE PLAN STAMPS THE BAKE — placement is data and ran before this pass.
  // Every stamped site marks BOTH twins (the emit rule, in pigment).
  float elev = asinf(f_clamp(g_sun.dir.y, -1.0f, 1.0f));
  float smear_len = f_clamp(0.30f / tanf(fmaxf(elev, 0.06f)), 0.0f, 2.5f);
  float smear_amt = 0.55f * (1.0f - f_smoothstep(0.30f, 0.75f, elev));
  v3 sdir = v3_norm((v3){{-g_sun.dir.x, 0.0f, -g_sun.dir.z}});
  v3 dust = {{0.86f, 0.85f, 0.84f}};
  for (int n = 0; n < g_scat_n; n++) {
    const scat_t *sc = &g_scat[n];
    v3 p1 = sc->pos;
    v3 p2 = sc->host >= 0
                ? v3_scale(scat_reflect(p1, &g_solids[sc->host], sc->var & 1), -1.0f)
                : v3_scale(p1, -1.0f);
    v3 pp[2] = {p1, p2};
    for (int t = 0; t < 2; t++) {
      float s = sc->s;
      switch (sc->fam) {
      case SCAT_ROCK:
        gbake_stamp(pp[t], s * 4.0f, dust, 0.7f, 0.30f);
        if (smear_amt > 0.02f)
          gbake_smear(pp[t], v3_add(pp[t], v3_scale(sdir, smear_len * s * 6.0f)),
                      s * 1.8f, dust, smear_amt, 0.22f);
        break;
      case SCAT_STONE:
        gbake_stamp(pp[t], s * 3.2f, dust, 0.5f, 0.20f);
        if (smear_amt > 0.02f)
          gbake_smear(pp[t], v3_add(pp[t], v3_scale(sdir, smear_len * s * 4.0f)),
                      s * 1.5f, dust, smear_amt * 0.8f, 0.15f);
        break;
      case SCAT_TUFT:   // dry-straw warmth under the grass
        gbake_stamp(pp[t], 0.45f, (v3){{1.03f, 1.00f, 0.90f}}, 0.5f, 0.06f);
        break;
      case SCAT_PAVE:   // the pavement's ground is packed dark under it
        gbake_stamp(pp[t], s * 1.15f, (v3){{0.88f, 0.88f, 0.87f}}, 0.55f, 0.10f);
        break;
      case SCAT_SHRUB:
        gbake_stamp(pp[t], s * 2.2f, dust, 0.4f, 0.14f);
        if (smear_amt > 0.02f)
          gbake_smear(pp[t], v3_add(pp[t], v3_scale(sdir, smear_len * s * 3.0f)),
                      s * 1.2f, dust, smear_amt * 0.6f, 0.10f);
        break;
      default: break;
      }
    }
  }
  // The damage lines: brick dust in the host wall's own cast at every foot.
  for (int n = 0; n < g_meta.nedge; n++) {
    const medge_t *e = &g_meta.edge[n];
    if (e->kind != ME_FOOT) continue;
    int wi = -1;
    scat_wall((e->a.x + e->b.x) * 0.5f, (e->a.z + e->b.z) * 0.5f, &wi, NULL);
    v3 wc2 = wi >= 0 ? g_solids[wi].color : g_theme.wall;
    v3 to = {{f_clamp(0.85f + 0.15f * wc2.x / fmaxf(g_theme.ground.x, 0.05f), 0.7f, 1.15f),
              f_clamp(0.85f + 0.15f * wc2.y / fmaxf(g_theme.ground.y, 0.05f), 0.7f, 1.15f),
              f_clamp(0.85f + 0.15f * wc2.z / fmaxf(g_theme.ground.z, 0.05f), 0.7f, 1.15f)}};
    gbake_smear(e->a, e->b, 0.8f, to, 0.65f, 0.08f);
    gbake_smear(v3_scale(e->a, -1.0f), v3_scale(e->b, -1.0f), 0.8f, to, 0.65f, 0.08f);
  }
  static gvert_t tmp[(GROUND_CELLS + 1) * (GROUND_CELLS + 1)];
  memcpy(tmp, g_gbake, sizeof tmp);
  for (int iz = 0; iz < N; iz++)
    for (int ix = 0; ix < N; ix++) {
      v3 st = {{0, 0, 0}};
      float sr = 0.0f, sa = 0.0f, wsum = 0.0f;
      for (int dz = -1; dz <= 1; dz++)
        for (int dx = -1; dx <= 1; dx++) {
          int jx = ix + dx, jz = iz + dz;
          if (jx < 0 || jz < 0 || jx >= N || jz >= N) continue;
          float k = (dx == 0 && dz == 0) ? 4.0f : (dx == 0 || dz == 0) ? 2.0f : 1.0f;
          const gvert_t *q = &tmp[jz * N + jx];
          st = v3_add(st, v3_scale(q->tint, k));
          sr += q->rough * k; sa += q->ao * k; wsum += k;
        }
      g_gbake[iz * N + ix] =
          (gvert_t){v3_scale(st, 1.0f / wsum), sr / wsum, sa / wsum};
    }
}

static void mesh_ground_vert(int ix, int iz, v3 base) {
  const gvert_t *g = &g_gbake[iz * (GROUND_CELLS + 1) + ix];
  v3 c = {{base.x * g->tint.x, base.y * g->tint.y, base.z * g->tint.z}};
  float r = g->rough < 0.05f ? 0.05f : g->rough > 1.0f ? 1.0f : g->rough;
  mesh_vert((v3){{-ARENA_HALF + (float)ix * GROUND_STEP, 0.0f,
                  -ARENA_HALF + (float)iz * GROUND_STEP}},
            (v3){{0, 1, 0}}, c, r, 0.0f, g->ao);
}

// Defined far below, next to the scene primitives it is built out of: decor is tumbled
// boxes, tapered blades and sloped wedges, and every one of those already exists as a
// scene_* primitive.
static void mesh_decor(void);
// Large horizon silhouettes do not cast into the arena, but they do occlude
// screen-space atmosphere. They are emitted between ground and fine decor so
// the geometry prepass can draw one cheap contiguous prefix.
static void mesh_large_scenery(void);
// The CASTING specials — the boulder pair's chamfered render and the dead
// trees' crowns — live down there too (same primitives), but must be emitted
// BEFORE the shadow prefix latch: a boulder without a shadow is a sticker,
// and the crown's long shadow is the whole reason the tree signature exists.
static void mesh_cast_extra(void);

static void mesh_build(void) {
  g_mesh_floats = 0;
  v3 ground_col = g_theme.ground;
  ao_off();
  mat_set(WORLD_GROUND_R, 0.0f);  // dusty concrete: a wide, weak sheen at grazing
  // Cover surfaces darken toward the floor: the old bake multiplied it into the
  // albedo, which also dimmed the sun; as a material term it dims the AMBIENT,
  // which is what an occluded surface actually loses.
  mat_set(WORLD_SOLID_R, 0.0f);
  // Concentrated in the bottom half metre. Spread linearly over 1.7 m it was an
  // 8% wash with nothing at the contact, which is the one place occlusion shows.
  ao_set(0.0f, WORLD_AO_RATE, WORLD_AO_MIN);
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    // The boulder pair collides as its AABB but RENDERS chamfered (0.04 m —
    // under the wire quantization players already live with); the plain
    // six-quad emit would fight the chamfered copy, so it is skipped here
    // and mesh_cast_extra owns the draw.
    if (i == g_meta.bould[0] || i == g_meta.bould[1]) continue;
    // The roughness sign is render metadata, not negative physics: WORLD_FS
    // takes abs() for the BRDF and uses the sign for the quiet plaster finish
    // on generated wall pieces. The vertex layout stays at 12 floats and
    // concrete/crates remain untouched.
    mat_set(s->mat == SOLID_WALL ? -WORLD_SOLID_R : WORLD_SOLID_R, 0.0f);
    v3 mn = s->min, mx = s->max, c = s->color;
    // +x -x +y -y +z -z faces, wound counter-clockwise from outside.
    mesh_quad((v3){{mx.x,mn.y,mn.z}},(v3){{mx.x,mx.y,mn.z}},(v3){{mx.x,mx.y,mx.z}},(v3){{mx.x,mn.y,mx.z}},(v3){{1,0,0}},c);
    mesh_quad((v3){{mn.x,mn.y,mx.z}},(v3){{mn.x,mx.y,mx.z}},(v3){{mn.x,mx.y,mn.z}},(v3){{mn.x,mn.y,mn.z}},(v3){{-1,0,0}},c);
    mesh_quad((v3){{mn.x,mx.y,mn.z}},(v3){{mn.x,mx.y,mx.z}},(v3){{mx.x,mx.y,mx.z}},(v3){{mx.x,mx.y,mn.z}},(v3){{0,1,0}},c);
    mesh_quad((v3){{mn.x,mn.y,mx.z}},(v3){{mn.x,mn.y,mn.z}},(v3){{mx.x,mn.y,mn.z}},(v3){{mx.x,mn.y,mx.z}},(v3){{0,-1,0}},c);
    mesh_quad((v3){{mn.x,mn.y,mx.z}},(v3){{mx.x,mn.y,mx.z}},(v3){{mx.x,mx.y,mx.z}},(v3){{mn.x,mx.y,mx.z}},(v3){{0,0,1}},c);
    mesh_quad((v3){{mx.x,mn.y,mn.z}},(v3){{mn.x,mn.y,mn.z}},(v3){{mn.x,mx.y,mn.z}},(v3){{mx.x,mx.y,mn.z}},(v3){{0,0,-1}},c);
  }
  ao_off();
  mesh_cast_extra();   // boulder render + tree crowns: casters, so pre-latch
  // EVERYTHING THAT CASTS IS NOW BEHIND US. The floor cannot: it is the lowest surface
  // in the world and there is nothing under it to shadow, so its 4608 triangles were
  // being rasterised into a 4096-square depth map every frame to decide the shadowing
  // of nothing at all.
  R.world_cast_verts = g_mesh_floats / VERT_FLOATS;
  // The scatter plan runs BEFORE the bake on purpose: placement is data, and
  // the ground pass is entitled to read it (a stain under a planned rock is
  // paint continuing where the mesh budget stops).
  scat_plan();
  mat_set(WORLD_GROUND_R, 0.0f);
  ao_off();
  ground_bake();
  for (int iz = 0; iz < GROUND_CELLS; iz++)
    for (int ix = 0; ix < GROUND_CELLS; ix++) {
      mesh_ground_vert(ix,     iz,     ground_col);
      mesh_ground_vert(ix,     iz + 1, ground_col);
      mesh_ground_vert(ix + 1, iz + 1, ground_col);
      mesh_ground_vert(ix,     iz,     ground_col);
      mesh_ground_vert(ix + 1, iz + 1, ground_col);
      mesh_ground_vert(ix + 1, iz,     ground_col);
    }
  int decor0 = g_mesh_floats;
  mesh_large_scenery();
  R.world_pre_verts = g_mesh_floats / VERT_FLOATS;
  mesh_decor();
  R.world_decor_verts = (g_mesh_floats - decor0) / VERT_FLOATS;
}

// The world VBO is static until a fresh map arrives (new match = new arena);
// the sim then flags g_world_dirty and the next frame re-uploads once.
static void world_upload(void) {
  mesh_build();
  R.world_verts = g_mesh_floats / VERT_FLOATS;
  if (R.world_cast_verts > R.world_verts) R.world_cast_verts = R.world_verts;
  if (R.world_pre_verts > R.world_verts) R.world_pre_verts = R.world_verts;
  glBindBuffer(GL_ARRAY_BUFFER, R.vbo_world);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(g_mesh_floats * sizeof(float)), g_mesh, GL_STATIC_DRAW);
}

static void renderer_init(int width, int height) {
  R.width = width;
  R.height = height;
  R.scope_size = height * 2 / 3;
  if (R.scope_size < 768) R.scope_size = 768;
  if (R.scope_size > 1536) R.scope_size = 1536;
  R.prog_world = gl_program(WORLD_VS, WORLD_FS);
  R.prog_hud = gl_program(HUD_VS, HUD_FS);
  R.prog_sky = gl_program(SKY_VS, SKY_FS);
  R.prog_depth = gl_program(DEPTH_VS, DEPTH_FS);
  R.u_dvp = glGetUniformLocation(R.prog_depth, "uVP");
  R.u_lightvp = glGetUniformLocation(R.prog_world, "uLightVP");
  R.u_smtexel = glGetUniformLocation(R.prog_world, "uSmTexel");
  R.u_shadow = glGetUniformLocation(R.prog_world, "uShadow");
  R.u_smstep = glGetUniformLocation(R.prog_world, "uSmStep");
  R.u_sm = glGetUniformLocation(R.prog_world, "uSm");
  R.u_vm = glGetUniformLocation(R.prog_world, "uVm");
  R.u_muzc = glGetUniformLocation(R.prog_world, "uMuzC");
  R.u_muzl = glGetUniformLocation(R.prog_world, "uMuzL");
  R.u_muzp = glGetUniformLocation(R.prog_world, "uMuzP");
  R.u_fill = glGetUniformLocation(R.prog_world, "uFill");
  R.u_rim = glGetUniformLocation(R.prog_world, "uRim");
  R.u_mdlo = glGetUniformLocation(R.prog_world, "uMdlO");
  R.u_mdlx = glGetUniformLocation(R.prog_world, "uMdlX");
  R.u_mdlz = glGetUniformLocation(R.prog_world, "uMdlZ");
  R.u_vp = glGetUniformLocation(R.prog_world, "uVP");
  R.u_eye = glGetUniformLocation(R.prog_world, "uEye");
  R.u_detail = glGetUniformLocation(R.prog_world, "uDetail");
  R.u_surf = glGetUniformLocation(R.prog_world, "uSurf");
  R.rig_world = rig_locs(R.prog_world);
  R.u_emis = glGetUniformLocation(R.prog_world, "uEmis");
  R.u_screen = glGetUniformLocation(R.prog_hud, "uScreen");
  R.u_tex = glGetUniformLocation(R.prog_hud, "uTex");
  R.u_img = glGetUniformLocation(R.prog_hud, "uImg");
  R.prog_pre = gl_program(PRE_VS, PRE_FS);
  R.u_pre_vp = glGetUniformLocation(R.prog_pre, "uVP");
  R.u_pre_pool = glGetUniformLocation(R.prog_pre, "uPoolLevel");
  R.prog_ssr = gl_program(BLUR_VS, SSR_FS);
  R.u_ssr_depth = glGetUniformLocation(R.prog_ssr, "uDepth");
  R.u_ssr_nrm   = glGetUniformLocation(R.prog_ssr, "uNrm");
  R.u_ssr_pic   = glGetUniformLocation(R.prog_ssr, "uPic");
  R.u_ssr_invvp = glGetUniformLocation(R.prog_ssr, "uInvVP");
  R.u_ssr_vp    = glGetUniformLocation(R.prog_ssr, "uVP");
  R.u_ssr_eye   = glGetUniformLocation(R.prog_ssr, "uEye");
  R.u_ssr_z     = glGetUniformLocation(R.prog_ssr, "uZ");
  R.prog_ssr_apply = gl_program(BLUR_VS, SSR_APPLY_FS);
  R.u_sa_ssr   = glGetUniformLocation(R.prog_ssr_apply, "uSsr");
  R.u_sa_nrm   = glGetUniformLocation(R.prog_ssr_apply, "uNrm");
  R.u_sa_depth = glGetUniformLocation(R.prog_ssr_apply, "uDepth");
  R.u_sa_invvp = glGetUniformLocation(R.prog_ssr_apply, "uInvVP");
  R.u_sa_eye   = glGetUniformLocation(R.prog_ssr_apply, "uEye");
  R.u_sa_px    = glGetUniformLocation(R.prog_ssr_apply, "uSsrPx");
  R.u_ao = glGetUniformLocation(R.prog_world, "uAo");
  R.u_aop = glGetUniformLocation(R.prog_world, "uAoP");
  R.u_predepth = glGetUniformLocation(R.prog_world, "uPreDepth");
  R.u_skycut = glGetUniformLocation(R.prog_world, "uSkyCut");
  R.prog_ssao = gl_program(BLUR_VS, SSAO_FS);
  R.u_ao_depth = glGetUniformLocation(R.prog_ssao, "uDepth");
  R.u_ao_nrm   = glGetUniformLocation(R.prog_ssao, "uNrm");
  R.u_ao_invvp = glGetUniformLocation(R.prog_ssao, "uInvVP");
  R.u_ao_eye   = glGetUniformLocation(R.prog_ssao, "uEye");
  R.u_ao_scr   = glGetUniformLocation(R.prog_ssao, "uScr");
  R.u_ao_z     = glGetUniformLocation(R.prog_ssao, "uZ");
  R.u_ao_i     = glGetUniformLocation(R.prog_ssao, "uAoI");
  R.prog_aoblur = gl_program(BLUR_VS, AOBLUR_FS);
  R.u_aob_tex = glGetUniformLocation(R.prog_aoblur, "uTex");
  R.u_aob_px  = glGetUniformLocation(R.prog_aoblur, "uPix");
  R.prog_blur = gl_program(BLUR_VS, BLUR_FS);
  R.prog_glare = gl_program(BLUR_VS, GLARE_FS);   // same fullscreen triangle
  R.u_gl_scene = glGetUniformLocation(R.prog_glare, "uScene");
  R.u_gl_bloom = glGetUniformLocation(R.prog_glare, "uBloom");
  R.u_gl_aa = glGetUniformLocation(R.prog_glare, "uAA");
  R.prog_volume = gl_program(BLUR_VS, VOLUME_FS);
  R.u_vol_depth   = glGetUniformLocation(R.prog_volume, "uDepth");
  R.u_vol_shadow  = glGetUniformLocation(R.prog_volume, "uShadow");
  R.u_vol_invvp   = glGetUniformLocation(R.prog_volume, "uInvVP");
  R.u_vol_lightvp = glGetUniformLocation(R.prog_volume, "uLightVP");
  R.u_vol_eye     = glGetUniformLocation(R.prog_volume, "uEye");
  R.u_vol_sundir  = glGetUniformLocation(R.prog_volume, "uSunDir");
  R.u_vol_suncol  = glGetUniformLocation(R.prog_volume, "uSunCol");
  R.u_vol_atmo    = glGetUniformLocation(R.prog_volume, "uAtmoTint");
  R.u_vol_p       = glGetUniformLocation(R.prog_volume, "uVol");
  R.prog_bdown = gl_program(BLUR_VS, BLOOM_DOWN_FS);
  R.u_bd_tex   = glGetUniformLocation(R.prog_bdown, "uTex");
  R.u_bd_px    = glGetUniformLocation(R.prog_bdown, "uPix");
  R.u_bd_first = glGetUniformLocation(R.prog_bdown, "uFirst");
  R.prog_bup   = gl_program(BLUR_VS, BLOOM_UP_FS);
  R.u_bu_tex   = glGetUniformLocation(R.prog_bup, "uTex");
  R.u_bu_px    = glGetUniformLocation(R.prog_bup, "uPix");
  R.u_smraw  = glGetUniformLocation(R.prog_world, "uSmRaw");
  R.u_smpcss = glGetUniformLocation(R.prog_world, "uSmPcss");
  R.u_gl_tex  = glGetUniformLocation(R.prog_glare, "uTex");
  R.u_gl_sun  = glGetUniformLocation(R.prog_glare, "uSunUV");
  R.u_gl_knee = glGetUniformLocation(R.prog_glare, "uKnee");
  R.u_gl_gain = glGetUniformLocation(R.prog_glare, "uGain");
  R.u_gl_mode = glGetUniformLocation(R.prog_glare, "uMode");
  R.u_gl_grade = glGetUniformLocation(R.prog_glare, "uGrade");
  R.u_blur_tex = glGetUniformLocation(R.prog_blur, "uTex");
  R.u_blur_px = glGetUniformLocation(R.prog_blur, "uPix");
  R.u_sky_fwd = glGetUniformLocation(R.prog_sky, "uFwd");
  R.u_sky_right = glGetUniformLocation(R.prog_sky, "uRight");
  R.u_sky_up = glGetUniformLocation(R.prog_sky, "uUp");
  R.u_sky_tan = glGetUniformLocation(R.prog_sky, "uTan");
  R.u_sky_wind = glGetUniformLocation(R.prog_sky, "uWindD");
  R.u_sky_cov = glGetUniformLocation(R.prog_sky, "uCov");
  R.u_sky_sunxz = glGetUniformLocation(R.prog_sky, "uSunXZ");
  R.u_sky_ridge = glGetUniformLocation(R.prog_sky, "uRidge");
  R.rig_sky = rig_locs(R.prog_sky);
  glGenVertexArrays(1, &R.vao_sky);  // attribute-less: verts from gl_VertexID

  glGenVertexArrays(1, &R.vao_world);
  glGenBuffers(1, &R.vbo_world);
  glBindVertexArray(R.vao_world);
  world_upload();
  GLsizei stride = VERT_FLOATS * sizeof(float);
  for (int pass = 0; pass < 2; pass++) {
    if (pass) {  // the per-frame dynamic batch: same layout, own VAO/VBO
      glGenVertexArrays(1, &R.vao_scene);
      glGenBuffers(1, &R.vbo_scene);
      glBindVertexArray(R.vao_scene);
      glBindBuffer(GL_ARRAY_BUFFER, R.vbo_scene);
    }
    for (int a = 0; a < 4; a++) {
      glVertexAttribPointer((GLuint)a, 3, GL_FLOAT, GL_FALSE, stride,
                            (void *)(size_t)(3 * a * sizeof(float)));
      glEnableVertexAttribArray((GLuint)a);
    }
  }

  glGenVertexArrays(1, &R.vao_hud);
  glGenBuffers(1, &R.vbo_hud);
  glBindVertexArray(R.vao_hud);
  glBindBuffer(GL_ARRAY_BUFFER, R.vbo_hud);
  GLsizei hs = 9 * sizeof(float);  // x y u v r g b a mode
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, hs, (void *)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, hs, (void *)(2 * sizeof(float)));
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, hs, (void *)(4 * sizeof(float)));
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, hs, (void *)(8 * sizeof(float)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glEnableVertexAttribArray(2);
  glEnableVertexAttribArray(3);

  // The distance atlas is baked on the CPU at startup (~half a megabyte of BSS, written
  // once).
  static uint8_t hf_atlas[HF_TEX_H][HF_TEX_W];
  font_bake(&hf_atlas[0][0]);
  glGenTextures(1, &R.font_tex);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, HF_TEX_W, HF_TEX_H, 0, GL_RED_CORE, GL_UNSIGNED_BYTE, hf_atlas);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // Sun shadow map. Built by the one builder, which the frame also calls: the
  // SHADOWS setting decides the size and this is a rebuild like any other.
  shadow_update();

  // Scope target: the magnified view is rendered here once per frame while the sniper
  // is shouldered, then shown as a disc inside the ocular.
  glGenTextures(1, &R.scope_tex);
  glBindTexture(GL_TEXTURE_2D, R.scope_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, R.scope_size, R.scope_size, 0, GL_RGB,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenRenderbuffers(1, &R.scope_depth);
  glBindRenderbuffer(GL_RENDERBUFFER, R.scope_depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                        R.scope_size, R.scope_size);
  glGenFramebuffers(1, &R.scope_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.scope_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         R.scope_tex, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                            R.scope_depth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    R.scope_fbo = 0;  // no scope picture rather than no game
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);

  glViewport(0, 0, width, height);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glClearColor(0.71f, 0.73f, 0.75f, 0.0f);  // ~= tonemap(sky_env) at the horizon
}

static int glyph_index(char ch) {
  unsigned char c = (unsigned char)ch;
  if (c >= 'a' && c <= 'z') c -= 32;           // CAPS-only brand voice
  if (c >= 32 && c < 127) return (int)c - 32;  // printable ASCII
  if (c >= 1 && c <= HF_ICONS) return HF_ASCII + (int)c - 1;  // \x01.. icons
  return -1;
}
