// =============================================================================
// core/hitscan.inc  —  ray_aabb/ray_solid/trace_world (boxes, bank prisms and floor), pen_span/
// PEN_PATH (the penetration path-length search), TICK_HZ/TICK_DT and
// segment_near (pure bounded segment proximity for acoustics and suppression).
// Proof: parity, netlagcomp (via ent_trace, see sim/bots_mind.inc). One TU:
// included once by game.c, in definition order — moving a definition out is
// a semantic change.
// Contract: PEN_PATH (1.20 m) is ONE number and no curve — no per-weapon
// falloff, no pen column on wpn_def_t.
// =============================================================================
// ---------------------------------------------------------------------------
// Hitscan — boxes use slabs; natural banks use their exposed triangles.
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

static float ray_solid(v3 o, v3 d, int index, v3 *normal) {
  const solid_t *s = &g_solids[index];
  float broad = ray_aabb(o, d, s->min, s->max, normal);
  if (s->mat != SOLID_BANK) return broad;
  if (broad < 0 && (o.x < s->min.x || o.x > s->max.x ||
      o.y < s->min.y || o.y > s->max.y || o.z < s->min.z || o.z > s->max.z)) return -1;
  float best = GUN_RANGE;
  // Neighbouring side faces are internal to the hillside union.
  for (int i = 0; i < BANK_EXPOSED_TRIS; i++) {
    v3 a = s->bank[BANK_TRIS[i][0]], b = s->bank[BANK_TRIS[i][1]], c = s->bank[BANK_TRIS[i][2]];
    v3 e = v3_sub(b, a), f = v3_sub(c, a), h = v3_cross(d, f);
    float det = v3_dot(e, h);
    if (fabsf(det) < 1e-7f) continue;
    v3 q = v3_sub(o, a);
    float u = v3_dot(q, h) / det;
    if (u < -1e-6f || u > 1.000001f) continue;
    v3 cross = v3_cross(q, e);
    float v = v3_dot(d, cross) / det;
    if (v < -1e-6f || u + v > 1.000001f) continue;
    float t = v3_dot(f, cross) / det;
    if (t <= 0 || t >= best) continue;
    best = t; *normal = v3_norm(v3_cross(e, f));
    if (v3_dot(*normal, d) > 0) *normal = v3_scale(*normal, -1);
  }
  return best < GUN_RANGE ? best : -1;
}

// `solid` (optional) reports WHICH box the ray entered, -1 for the arena floor plane or
// for nothing at all.
static float trace_world(v3 o, v3 d, v3 *nrm, int *solid) {
  float best = GUN_RANGE;
  int si = -1;
  for (int i = 0; i < g_num_solids; i++) {
    v3 n;
    float t = ray_solid(o, d, i, &n);
    if (t > 0 && t < best) { best = t; *nrm = n; si = i; }
  }
  if (d.y < -1e-8f) {  // arena floor plane at y = 0
    float t = -o.y / d.y;
    if (t > 0 && t < best) {
      float hx = o.x + d.x * t, hz = o.z + d.z * t;
      if (hx > -map_half() && hx < map_half() && hz > -map_half() && hz < map_half()) {
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
      if (s->mat == SOLID_BANK) {
        // Boundary impacts start on the shared mesh. An outward probe in its
        // footprint cannot penetrate the hillside, regardless of thickness.
        if (p.y >= 0 && p.y < fminf(s->bank[4].y, s->bank[5].y) &&
            bank_distance(s, p, 0) > 0) return -1.0f;
        continue;
      }
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
// Closest point on the actual finite travelled segment. The offset points
// from the query point toward the segment, matching ordinary source panning.
// This pure geometry owner has no entity, audio or simulation side effects.
typedef struct {
  v3 closest, offset;
  float fraction, along, length, miss;
} segment_near_t;

static int segment_near(v3 start, v3 end, v3 point, segment_near_t *out) {
  *out = (segment_near_t){0};
  for (int axis = 0; axis < 3; axis++)
    if (f_is_nonfinite(start.e[axis]) || f_is_nonfinite(end.e[axis]) ||
        f_is_nonfinite(point.e[axis])) return 0;
  v3 travel = v3_sub(end, start);
  float squared = v3_dot(travel, travel);
  if (f_is_nonfinite(squared) || squared <= 1e-12f) return 0;
  float fraction = v3_dot(v3_sub(point, start), travel) / squared;
  if (f_is_nonfinite(fraction)) return 0;
  fraction = f_clamp(fraction, 0.0f, 1.0f);
  v3 closest = v3_add(start, v3_scale(travel, fraction));
  v3 offset = v3_sub(closest, point);
  float miss_squared = v3_dot(offset, offset);
  if (f_is_nonfinite(miss_squared)) return 0;
  float length = sqrtf(squared);
  *out = (segment_near_t){closest, offset, fraction, fraction * length,
                           length, sqrtf(miss_squared)};
  return 1;
}

#define TICK_HZ        120
#define TICK_DT        (1.0f / TICK_HZ)
