// =============================================================================
// core/hitscan.inc  —  ray_aabb/trace_world (ray vs AABB + floor), pen_span/
// PEN_PATH (the penetration path-length search), TICK_HZ/TICK_DT.
// Proof: parity, netlagcomp (via ent_trace, see sim/bots_mind.inc). One TU:
// included once by game.c, in definition order — moving a definition out is
// a semantic change.
// Contract: PEN_PATH (1.20 m) is ONE number and no curve — no per-weapon
// falloff, no pen column on wpn_def_t.
// =============================================================================
// ---------------------------------------------------------------------------
// Hitscan — ray vs AABB slab test against the solids plus the arena floor.
// Returns the nearest hit distance (or GUN_RANGE if nothing) and the face
// normal, which impact marks and particles align to.
// ---------------------------------------------------------------------------

#define GUN_RANGE 200.0f

static float ray_aabb(v3 o, v3 d, v3 mn, v3 mx, v3 *nrm) {
  float t0 = 0.0f, t1 = GUN_RANGE;
  int ax0 = -1;
  for (int a = 0; a < 3; a++) {
    if (d.e[a] > -1e-8f && d.e[a] < 1e-8f) {
      if (o.e[a] < mn.e[a] || o.e[a] > mx.e[a]) return -1.0f;
      continue;
    }
    float inv = 1.0f / d.e[a];
    float ta = (mn.e[a] - o.e[a]) * inv, tb = (mx.e[a] - o.e[a]) * inv;
    if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
    if (ta > t0) { t0 = ta; ax0 = a; }
    if (tb < t1) t1 = tb;
    if (t0 > t1) return -1.0f;
  }
  if (ax0 < 0) return -1.0f;  // ray starts inside: no sensible entry face
  *nrm = (v3){{0, 0, 0}};
  nrm->e[ax0] = d.e[ax0] > 0 ? -1.0f : 1.0f;
  return t0;
}

// `solid` (optional) reports WHICH box the ray entered, -1 for the arena floor plane or
// for nothing at all.
static float trace_world(v3 o, v3 d, v3 *nrm, int *solid) {
  float best = GUN_RANGE;
  int si = -1;
  for (int i = 0; i < g_num_solids; i++) {
    v3 n;
    float t = ray_aabb(o, d, g_solids[i].min, g_solids[i].max, &n);
    if (t > 0 && t < best) { best = t; *nrm = n; si = i; }
  }
  if (d.y < -1e-8f) {  // arena floor plane at y = 0
    float t = -o.y / d.y;
    if (t > 0 && t < best) {
      float hx = o.x + d.x * t, hz = o.z + d.z * t;
      if (hx > -ARENA_HALF && hx < ARENA_HALF && hz > -ARENA_HALF && hz < ARENA_HALF) {
        best = t;
        *nrm = (v3){{0, 1, 0}};
        si = -1;                     // a nearer floor un-claims the box
      }
    }
  }
  if (solid) *solid = si;
  return best;
}

// --- penetration -----------------------------------------------------------
// A round that meets material keeps going while the PATH LENGTH it has spent inside
// solids stays under the weapon's budget.

// How far the ray runs before it leaves this box. ray_aabb deliberately reports
// NO hit for an origin inside a box (the lean probe and the third-person pivot
// cast both depend on that), so the exit needs its own slab walk: the SMALLEST
// far-side crossing over the three axes.
static float aabb_exit(v3 o, v3 d, v3 mn, v3 mx) {
  float t1 = 1e30f;
  for (int a = 0; a < 3; a++) {
    if (d.e[a] > -1e-8f && d.e[a] < 1e-8f) continue;
    float inv = 1.0f / d.e[a];
    float ta = (mn.e[a] - o.e[a]) * inv, tb = (mx.e[a] - o.e[a]) * inv;
    float hi = ta > tb ? ta : tb;
    if (hi < t1) t1 = hi;
  }
  return t1 < 0.0f ? 0.0f : t1;
}

// ONE RULE, ONE NUMBER: a round crosses PEN_PATH of material and then stops.
#define PEN_PATH  1.20f    // metres of material ANY round may cross, full damage
#define PEN_EPS   0.0015f  // step past a face before re-testing containment
// The two below are COST bounds on the search, NOT rules of the mechanic, and they are
// sized so PEN_PATH is the only thing a player can ever run into.
#define PEN_JOIN  8        // abutting solids one crossing may weld into one run
#define PEN_MAX   8        // separate runs one round may cross

// The far side of a CONTIGUOUS run of material entered at `o`, or -1 if the run
// contains something no round may cross.
static float pen_span(v3 o, v3 d) {
  float t = 0.0f;
  for (int it = 0; it <= PEN_JOIN; it++) {   // <=: the last pass is the one
                                             // that finds open air
    v3 p = v3_add(o, v3_scale(d, t + PEN_EPS));
    float adv = t;
    for (int i = 0; i < g_num_solids; i++) {
      const solid_t *s = &g_solids[i];
      if (p.x < s->min.x || p.x > s->max.x || p.y < s->min.y ||
          p.y > s->max.y || p.z < s->min.z || p.z > s->max.z) continue;
      if (!s->pen) return -1.0f;
      float ex = t + PEN_EPS + aabb_exit(p, d, s->min, s->max);
      if (ex > adv) adv = ex;
    }
    if (adv <= t) return t;                 // open air on the far side
    t = adv;
  }
  return -1.0f;   // still in material after PEN_JOIN welds: treat it as solid
}

// THE CLIFF IS NOT A DEAD BAND, which is the objection the old falloff existed to
// answer: path length through a box is not monotone in the aim, so a hard cut was
// expected to give hit / stopped / hit as the aim walks across cover.

// The sim's tick rate.
#define TICK_HZ        120
#define TICK_DT        (1.0f / TICK_HZ)

