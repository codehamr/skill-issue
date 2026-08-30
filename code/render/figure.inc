// =============================================================================
// render/figure.inc  —  held-hand/gun-contact state and profiles, fig_hand,
// and the shared hand/grip geometry.
// Proof: vmhand, vmtrig, figcheck. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: this fragment owns contact classification/runtime proxies and the
// hand geometry that consumes them; gun_build publishes into that contract.
// =============================================================================

// One hand, CLOSED AROUND A CYLINDER. The figure and the viewmodel both call this,
// because a hand built twice ends up different in the two places the player sees it —
// and the viewmodel's is the closest geometry to the camera in the game.

// A point on the grip cylinder's frame: `ax` along the axis from `gc`, `ph` around it
// measured from e1, at radius `rad`.
static v3    g_gun_trig_p;    // world contact point on the blade's rear face
static v3    g_gun_trig_n;    // unit REARWARD normal there (the pull direction)
static v3    g_gun_guard[4];  // the guard bow's polyline, world space
static float g_gun_guard_r;   // ...and the tube radius round it
// The cross-section scale the rifle is being drawn at — 1.0 in first person,
// GUN_GIRTH_3P on a figure.
static float g_gun_girth;
static v3    g_gun_blade[3];  // the trigger blade's polyline, world space
static float g_gun_blade_r;   // ...and its tube radius. Published for the same
                              // reason the guard is: it is a THIN part in open air, and
                              // the proof has to be able to ask whether a fat finger
                              // has swallowed it.
static v3    g_gun_body_c, g_gun_body_e[3];   // receiver box: centre + 3 half-axes

// Fixed contact classes published by gun_build. Mesh ranges are closed primitive
// intervals for the offline proof; the renderer never collides its hand against the
// runtime mesh. The small OBB table is the deterministic runtime/proof contract.
typedef enum {
  GUN_CONTACT_GRIP,
  GUN_CONTACT_FORE,
  GUN_CONTACT_MAG,
  GUN_CONTACT_BOLT,
  GUN_CONTACT_RECEIVER,
  GUN_CONTACT_SCOPE,
  GUN_CONTACT_STOCK,
  GUN_CONTACT_CHASSIS,
  GUN_CONTACT_COUNT
} gun_contact_kind_t;
enum {
  GUN_MESH_PART_GRIP_BODY       = 1u << 0,
  GUN_MESH_PART_GRIP_PANEL      = 1u << 1,
  GUN_MESH_PART_FORE_BODY       = 1u << 2,
  GUN_MESH_PART_RAIL_BASE       = 1u << 3,
  GUN_MESH_PART_RAIL_CROWN      = 1u << 4,
  GUN_MESH_PART_RAIL_TOOTH      = 1u << 5,
  GUN_MESH_PART_FORE_DETAIL     = 1u << 6,
  GUN_MESH_PART_UNDER_OUTER     = 1u << 7,
  GUN_MESH_PART_UNDER_INNER     = 1u << 8,
  GUN_MESH_PART_UNDER_TOOTH     = 1u << 9,
  GUN_MESH_PART_FORE_ATTACHMENT = 1u << 10,
  GUN_MESH_PART_MAG_BODY        = 1u << 11,
  GUN_MESH_PART_MAG_DETAIL      = 1u << 12,
};
typedef struct {
  int v0, v1, surface;
  uint32_t part, host_mask;
} gun_mesh_range_t;
#define GUN_CONTACT_MESH_MAX 128
typedef struct {
  gun_mesh_range_t range[GUN_CONTACT_MESH_MAX];
  int count, overflow;
} gun_mesh_set_t;
typedef struct { v3 c, e[3]; int enabled; } gun_contact_proxy_t;
#define GUN_CONTACT_PROXY_MAX 8
typedef struct {
  gun_contact_proxy_t box[GUN_CONTACT_PROXY_MAX];
  int count, overflow;
} gun_contact_proxy_set_t;
typedef struct { v3 c, axis; float radius, half; int enabled; } gun_contact_cylinder_t;
// One angular contact profile for a held weapon part. `u`/`rx` and its perpendicular
// `v`/`rz` are the final mesh's two cross-section axes; box01 blends a circle into
// the k=8 outline. `ref_r` preserves the authored hand offsets while the profile
// supplies the complete angular radius.
#define GUN_HOLD_UNDER_MAX 2
#define GUN_HOLD_SECTION_MAX 6
typedef struct {
  // Positive distances from the hold origin towards -u. A radial ray enters the
  // rectangle at `inner`, then leaves through either `outer` or a +/-v side wall.
  float inner, outer, v_half;
} gun_hold_under_t;
typedef struct {
  v3 u;
  float rx, rz, box01, ref_r, fore_r, fore3_r, center;
  // Optional three-section axial profile. `rx`/`rz` are the middle section;
  // the endpoint radii and centre offsets are interpolated on either side of it.
  // The offset is along `u`, perpendicular to the held axis. This is enough to
  // reproduce a gently curved/raked grip without a runtime hull or another mesh.
  float axial_lo, axial_mid, axial_hi;
  float axial_rx_lo, axial_rz_lo, axial_rx_hi, axial_rz_hi;
  float axial_center_lo, axial_center_mid, axial_center_hi;
  // Fixed-axis projections of the final swept render nodes. Longer tapered
  // parts use these instead of freezing the entire hand span to one section.
  float section_along[GUN_HOLD_SECTION_MAX];
  float section_rx[GUN_HOLD_SECTION_MAX];
  float section_rz[GUN_HOLD_SECTION_MAX];
  float section_center[GUN_HOLD_SECTION_MAX];
  // Optional continuous held-detail envelope in the (u,v) cross-section. Side rails
  // occupy side_inner..side_outer on both +/-v flanks, only between
  // side_u_lo..side_u_hi. Under-rail stages are exact rectangles, rather than the
  // bounding box made by combining one stage's depth with another stage's width.
  float side_inner, side_outer, side_u_lo, side_u_hi;
  gun_hold_under_t under[GUN_HOLD_UNDER_MAX];
  int under_count, section_n, axial3;
  int sides;  // regular fig_ring side count; 0 keeps the analytic hold envelope
  int enabled;
} gun_hold_profile_t;
static gun_mesh_set_t g_gun_contact_mesh[GUN_CONTACT_COUNT];
static gun_contact_proxy_set_t g_gun_contact_proxy[GUN_CONTACT_COUNT];
static gun_contact_cylinder_t g_gun_contact_cylinder[GUN_CONTACT_BOLT + 1];
static gun_hold_profile_t g_gun_mag_profile;
#define GUN_CONTACT_SCOPE_MAX 8
static int g_gun_contact_mesh_active = -1;
static int g_gun_contact_mesh_stack[GUN_CONTACT_SCOPE_MAX];
static int g_gun_contact_mesh_depth;
static int g_gun_contact_mesh_ignored;
static int g_gun_contact_surface_active;
static int g_gun_contact_mesh_unclassified;
static int g_gun_contact_mesh_scope_overflow;
static int g_gun_contact_mesh_capture;
static uint32_t g_gun_contact_mesh_part, g_gun_contact_mesh_host_mask;

static void gun_contact_reset(void) {
  for (int i = 0; i < GUN_CONTACT_COUNT; i++) {
    g_gun_contact_proxy[i] = (gun_contact_proxy_set_t){0};
    if (g_probe_on) g_gun_contact_mesh[i] = (gun_mesh_set_t){0};
  }
  for (int i = 0; i <= GUN_CONTACT_BOLT; i++)
    g_gun_contact_cylinder[i].enabled = 0;
  g_gun_mag_profile.enabled = 0;
  g_gun_contact_mesh_active = -1;
  g_gun_contact_mesh_depth = 0;
  g_gun_contact_mesh_ignored = 0;
  g_gun_contact_surface_active = 0;
  g_gun_contact_mesh_unclassified = 0;
  g_gun_contact_mesh_scope_overflow = 0;
  g_gun_contact_mesh_part = 0;
  g_gun_contact_mesh_host_mask = 0;
  // Exact triangle ownership is proof instrumentation. Runtime hands consume only
  // the fixed proxy/cylinder tables, so do not clear or populate 8 x 128 ranges on
  // every ordinary weapon build.
  g_gun_contact_mesh_capture = g_probe_on;
}

static void gun_contact_mesh_begin(gun_contact_kind_t kind) {
  if (!g_gun_contact_mesh_capture) return;
  if (kind < GUN_CONTACT_GRIP || kind >= GUN_CONTACT_COUNT ||
      g_gun_contact_mesh_depth >= GUN_CONTACT_SCOPE_MAX) {
    g_gun_contact_mesh_scope_overflow++;
    return;
  }
  g_gun_contact_mesh_stack[g_gun_contact_mesh_depth++] = g_gun_contact_mesh_active;
  g_gun_contact_mesh_active = kind;
}

static void gun_contact_mesh_end(gun_contact_kind_t kind) {
  if (!g_gun_contact_mesh_capture) return;
  if (g_gun_contact_mesh_active != (int)kind || g_gun_contact_mesh_depth <= 0) {
    g_gun_contact_mesh_scope_overflow++;
    g_gun_contact_mesh_active = -1;
    g_gun_contact_mesh_depth = 0;
    return;
  }
  g_gun_contact_mesh_active = g_gun_contact_mesh_stack[--g_gun_contact_mesh_depth];
}

// Guard and trigger blade have their own polyline proofs. They deliberately do not
// belong to an allowed/forbidden solid class, but every emitted primitive still goes
// through gun_contact_mesh_add_active so an accidental unclassified part is countable.
static void gun_contact_mesh_pause(void) {
  if (!g_gun_contact_mesh_capture) return;
  if (g_gun_contact_mesh_depth >= GUN_CONTACT_SCOPE_MAX) {
    g_gun_contact_mesh_scope_overflow++;
    return;
  }
  g_gun_contact_mesh_stack[g_gun_contact_mesh_depth++] = g_gun_contact_mesh_active;
  g_gun_contact_mesh_active = -1;
  g_gun_contact_mesh_ignored++;
}

static void gun_contact_mesh_resume(void) {
  if (!g_gun_contact_mesh_capture) return;
  if (g_gun_contact_mesh_active >= 0 || g_gun_contact_mesh_ignored <= 0 ||
      g_gun_contact_mesh_depth <= 0) {
    g_gun_contact_mesh_scope_overflow++;
    g_gun_contact_mesh_active = -1;
    g_gun_contact_mesh_depth = 0;
    g_gun_contact_mesh_ignored = 0;
    return;
  }
  g_gun_contact_mesh_ignored--;
  g_gun_contact_mesh_active = g_gun_contact_mesh_stack[--g_gun_contact_mesh_depth];
}

// One closed primitive per held class is the authored CONTACT SURFACE. Decorative
// ribs and overlapping detail remain classified and participate in through-wall
// checks, but cannot distort the 2--4 mm surface-bite measurement with hidden faces.
static void gun_contact_surface_begin(void) {
  if (!g_gun_contact_mesh_capture) return;
  if (g_gun_contact_mesh_active < 0 || g_gun_contact_surface_active) {
    g_gun_contact_mesh_scope_overflow++;
    return;
  }
  g_gun_contact_surface_active = 1;
}

static void gun_contact_surface_end(void) {
  if (!g_gun_contact_mesh_capture) return;
  if (!g_gun_contact_surface_active) {
    g_gun_contact_mesh_scope_overflow++;
    return;
  }
  g_gun_contact_surface_active = 0;
}

// Explicit assembly ownership for overlapping closed primitives. A hosted range is
// still independently closed and proof-visible; only source cells certified wholly
// inside one of these authored host parts may be ignored as buried internal faces.
static void gun_contact_mesh_role_begin(uint32_t part, uint32_t host_mask) {
  if (!g_gun_contact_mesh_capture) return;
  if (!part || g_gun_contact_mesh_part || g_gun_contact_mesh_host_mask) {
    g_gun_contact_mesh_scope_overflow++;
    return;
  }
  g_gun_contact_mesh_part = part;
  g_gun_contact_mesh_host_mask = host_mask;
}

static void gun_contact_mesh_role_end(uint32_t part) {
  if (!g_gun_contact_mesh_capture) return;
  if (g_gun_contact_mesh_part != part) g_gun_contact_mesh_scope_overflow++;
  g_gun_contact_mesh_part = 0;
  g_gun_contact_mesh_host_mask = 0;
}

// Record one CLOSED render primitive. Keeping every box/chain/tube separate is
// essential: signed ray parity across a union of overlapping primitives can cancel
// interiors and miss a hand that is fully embedded in one of them.
static void gun_contact_mesh_add_active(int v0) {
  if (!g_gun_contact_mesh_capture) return;
  int kind = g_gun_contact_mesh_active;
  if (v0 >= g_scene_floats) return;
  if (kind < 0) {
    if (!g_gun_contact_mesh_ignored) g_gun_contact_mesh_unclassified++;
    return;
  }
  gun_mesh_set_t *set = &g_gun_contact_mesh[kind];
  if (set->count >= GUN_CONTACT_MESH_MAX) {
    set->overflow = 1;
    return;
  }
  set->range[set->count++] = (gun_mesh_range_t){
    v0, g_scene_floats, g_gun_contact_surface_active,
    g_gun_contact_mesh_part, g_gun_contact_mesh_host_mask,
  };
}

static void gun_contact_box(gun_contact_kind_t kind, v3 c, v3 e0, v3 e1, v3 e2) {
  if (kind < GUN_CONTACT_GRIP || kind >= GUN_CONTACT_COUNT) return;
  gun_contact_proxy_set_t *set = &g_gun_contact_proxy[kind];
  if (set->count >= GUN_CONTACT_PROXY_MAX) {
    set->overflow = 1;
    return;
  }
  set->box[set->count++] = (gun_contact_proxy_t){c, {e0, e1, e2}, 1};
}

static void gun_contact_cylinder(gun_contact_kind_t kind, v3 c, v3 axis,
                                 float radius, float half) {
  if (kind < GUN_CONTACT_GRIP || kind > GUN_CONTACT_BOLT) return;
  g_gun_contact_cylinder[kind] = (gun_contact_cylinder_t){
    c, v3_norm(axis), radius, half, 1
  };
}

// Positive = how far p lies inside an allowed contact cylinder. Axial misses are not
// contacts and return zero; grip/fore/mag are finite, disjoint ranges.
#ifndef _WIN32
static float gun_contact_bite(v3 p, const gun_contact_cylinder_t *c) {
  if (!c->enabled) return 0.0f;
  v3 rel = v3_sub(p, c->c);
  float along = v3_dot(rel, c->axis);
  if (fabsf(along) > c->half) return 0.0f;
  float radial = v3_len(v3_sub(rel, v3_scale(c->axis, along)));
  return fmaxf(c->radius - radial, 0.0f);
}

// Signed OBB distance: negative inside, positive outside.
static float gun_contact_sd(v3 p, const gun_contact_proxy_t *b) {
  if (!b->enabled) return 1e9f;
  v3 rel = v3_sub(p, b->c);
  float q[3], out2 = 0.0f, in = -1e9f;
  for (int k = 0; k < 3; k++) {
    float h = v3_len(b->e[k]);
    if (h < 1e-7f) return 1e9f;
    q[k] = fabsf(v3_dot(rel, v3_scale(b->e[k], 1.0f / h))) - h;
    if (q[k] > 0.0f) out2 += q[k] * q[k];
    if (q[k] > in) in = q[k];
  }
  return out2 > 0.0f ? sqrtf(out2) : in;
}
#endif

// Push a node outside one expanded OBB. This is used only for the few published
// forbidden solids; allowed grip/fore/mag bite remains untouched.
static v3 gun_contact_clear(v3 p, float r, const gun_contact_proxy_t *b) {
  if (!b->enabled) return p;
  v3 rel = v3_sub(p, b->c);
  float best = 1e9f, sgn = 1.0f;
  int axis = -1;
  for (int k = 0; k < 3; k++) {
    float h = v3_len(b->e[k]);
    if (h < 1e-7f) return p;
    float d = v3_dot(rel, v3_scale(b->e[k], 1.0f / h));
    float push = h + r - fabsf(d);
    if (push <= 0.0f) return p;
    if (push < best) { best = push; axis = k; sgn = d < 0.0f ? -1.0f : 1.0f; }
  }
  float h = v3_len(b->e[axis]);
  return v3_add(p, v3_scale(b->e[axis], sgn * best / h));
}

enum { FIG_HAND_FIRE, FIG_HAND_SUPPORT, FIG_HAND_COUNT };
typedef struct {
  float palm_mm;              // wrist-to-knuckle chord
  float wrist_mm;             // wrist stand-off from the grip surface
  v3 palm_dir;                // final rendered palm axis
  v3 forearm_back;            // final rendered wrist-to-elbow axis
  int palm_valid;
  int forearm_valid;
  int clamped;                // reach clamp moved the contact cylinder
} fig_hand_diag_t;
static fig_hand_diag_t g_fig_hand_diag[FIG_HAND_COUNT];
// A moving hand can leave one authored surface while closing on another. Evaluate
// both real endpoint profiles and blend their radial exits; interpolating the fields
// themselves would invent intermediate rectangles and reintroduce false corners.
typedef struct {
  gun_hold_profile_t endpoint[2];
  float mix;
} gun_hold_profile_blend_t;
static gun_hold_profile_blend_t g_hand_profile;
// Bit 0 = firing hand, bit 1 = support hand. A build accumulates both hands instead
// of letting whichever hand happened to draw last erase the first one's diagnosis.
static unsigned g_fig_clamped;
static void fig_hand_diag_reset(void) {
  g_fig_clamped = 0;
  for (int h = 0; h < FIG_HAND_COUNT; h++) {
    g_fig_hand_diag[h] = (fig_hand_diag_t){0};
    g_fig_hand_diag[h].wrist_mm = 1e9f;
  }
}
static void gun_profile_section(const gun_hold_profile_t *profile, float along,
                                float *rx, float *rz, float *center) {
  *rx = profile->rx;
  *rz = profile->rz;
  if (center) *center = profile->center;
  int n = profile->section_n;
  if (n > GUN_HOLD_SECTION_MAX) n = GUN_HOLD_SECTION_MAX;
  if (n >= 2) {
    int i = 0;
    while (i + 2 < n && along > profile->section_along[i + 1]) i++;
    float den = profile->section_along[i + 1] - profile->section_along[i];
    float t = den > 1e-6f
            ? f_clamp((along - profile->section_along[i]) / den, 0.0f, 1.0f)
            : 0.0f;
    *rx = profile->section_rx[i] +
          (profile->section_rx[i + 1] - profile->section_rx[i]) * t;
    *rz = profile->section_rz[i] +
          (profile->section_rz[i + 1] - profile->section_rz[i]) * t;
    if (center)
      *center = profile->section_center[i] +
                (profile->section_center[i + 1] -
                 profile->section_center[i]) * t;
    return;
  }
  if (!profile->axial3) return;
  float t;
  if (along <= profile->axial_mid) {
    float den = profile->axial_mid - profile->axial_lo;
    t = den > 1e-6f ? f_clamp((along - profile->axial_lo) / den, 0.0f, 1.0f)
                    : 1.0f;
    *rx = profile->axial_rx_lo + (profile->rx - profile->axial_rx_lo) * t;
    *rz = profile->axial_rz_lo + (profile->rz - profile->axial_rz_lo) * t;
    if (center)
      *center = profile->axial_center_lo +
                (profile->axial_center_mid - profile->axial_center_lo) * t;
  } else {
    float den = profile->axial_hi - profile->axial_mid;
    t = den > 1e-6f ? f_clamp((along - profile->axial_mid) / den, 0.0f, 1.0f)
                    : 0.0f;
    *rx = profile->rx + (profile->axial_rx_hi - profile->rx) * t;
    *rz = profile->rz + (profile->axial_rz_hi - profile->rz) * t;
    if (center)
      *center = profile->axial_center_mid +
                (profile->axial_center_hi - profile->axial_center_mid) * t;
  }
}

static v3 gun_profile_u(const gun_hold_profile_t *profile, v3 ga) {
  v3 u = v3_sub(profile->u, v3_scale(ga, v3_dot(profile->u, ga)));
  return v3_dot(u, u) < 1e-10f ? (v3){{0, 0, 0}} : v3_norm(u);
}

typedef struct {
  float t, nu, nv;
  int valid, ambiguous;
} gun_profile_ray_hit_t;
typedef struct {
  float t;
  v3 normal;
  int valid, ambiguous;
} gun_profile_hit_t;

#define GUN_PROFILE_TIE_EPS 1e-6f

static void gun_profile_ray_hit_take(gun_profile_ray_hit_t *best, float t,
                                     float nu, float nv, int ambiguous) {
  if (t <= 0.0f) return;
  if (!best->valid || t > best->t + GUN_PROFILE_TIE_EPS) {
    *best = (gun_profile_ray_hit_t){t, nu, nv, 1, ambiguous};
    return;
  }
  if (fabsf(t - best->t) > GUN_PROFILE_TIE_EPS) return;
  float al = sqrtf(nu * nu + nv * nv);
  float bl = sqrtf(best->nu * best->nu + best->nv * best->nv);
  int different = al > 1e-8f && bl > 1e-8f &&
                  (nu * best->nu + nv * best->nv) / (al * bl) < 0.999f;
  int sticky = best->ambiguous || ambiguous || different;
  if (t > best->t) {
    best->t = t;
    best->nu = nu;
    best->nv = nv;
  }
  best->ambiguous = sticky;
}

static float gun_profile_under_ray_exit(const gun_hold_profile_t *profile,
                                        float du, float dv,
                                        float center_shift,
                                        gun_profile_ray_hit_t *out_hit) {
  gun_profile_ray_hit_t best = {0};
  if (du >= -1e-6f) {
    if (out_hit) *out_hit = best;
    return 0.0f;
  }
  float down = -du, across = fabsf(dv);
  int n = profile->under_count;
  if (n > GUN_HOLD_UNDER_MAX) n = GUN_HOLD_UNDER_MAX;
  for (int i = 0; i < n; i++) {
    const gun_hold_under_t *r = &profile->under[i];
    if (r->inner < 0.0f || r->outer <= r->inner || r->v_half <= 0.0f)
      continue;
    float enter = (r->inner + center_shift) / down;
    float exit_u = (r->outer + center_shift) / down;
    float exit_v = across > 1e-6f ? r->v_half / across : 1e30f;
    float exit = fminf(exit_u, exit_v);
    // Include a ray that lands exactly on the upper corner. The returned exit is
    // still the exact far wall/side-wall intersection, never the bounding corner.
    if (exit + 1e-7f >= enter) {
      int corner = fabsf(exit_u - exit_v) <= GUN_PROFILE_TIE_EPS;
      float nu = exit_u <= exit_v ? -1.0f : 0.0f;
      float nv = exit_u <= exit_v ? 0.0f : (dv < 0.0f ? -1.0f : 1.0f);
      gun_profile_ray_hit_take(&best, exit, nu, nv, corner);
    }
  }
  if (out_hit) *out_hit = best;
  return best.t;
}

// Exact exit from either of the two side-rail rectangles. A ray can leave through
// the outer v wall OR either u wall; testing only its point on side_outer drops the
// narrow corner fan that entered through side_inner but left through a u wall.
static float gun_profile_side_ray_exit(const gun_hold_profile_t *profile,
                                       float du, float dv,
                                       float center_shift,
                                       gun_profile_ray_hit_t *out_hit) {
  gun_profile_ray_hit_t hit = {0};
  float across = fabsf(dv);
  if (profile->side_inner < 0.0f ||
      profile->side_outer <= profile->side_inner || across <= 1e-6f ||
      profile->side_u_hi <= profile->side_u_lo) {
    if (out_hit) *out_hit = hit;
    return 0.0f;
  }
  float side_u_lo = profile->side_u_lo - center_shift;
  float side_u_hi = profile->side_u_hi - center_shift;
  float enter = profile->side_inner / across;
  float outer_exit = profile->side_outer / across;
  float exit = outer_exit;
  hit.nv = dv < 0.0f ? -1.0f : 1.0f;
  if (fabsf(du) <= 1e-6f) {
    if (side_u_lo > 0.0f || side_u_hi < 0.0f) exit = 0.0f;
  } else {
    float u0 = side_u_lo / du;
    float u1 = side_u_hi / du;
    if (u1 < u0) { float swap = u0; u0 = u1; u1 = swap; }
    enter = fmaxf(enter, fmaxf(u0, 0.0f));
    if (u1 < outer_exit) {
      exit = u1;
      hit.nu = du < 0.0f ? -1.0f : 1.0f;
      hit.nv = 0.0f;
    }
    // Ambiguity records the near tie, but the representative remains the exact
    // nearer wall. Returning the farther wall here would inflate the closed union.
    if (fabsf(u1 - outer_exit) <= GUN_PROFILE_TIE_EPS)
      hit.ambiguous = 1;
  }
  if (exit + 1e-7f < enter) exit = 0.0f;
  hit.t = exit;
  hit.valid = exit > 0.0f;
  if (out_hit) *out_hit = hit;
  return exit;
}

static int gun_profile_hit_candidate(gun_profile_hit_t *hit, float t,
                                     v3 normal, int ambiguous, v3 d) {
  float n2 = v3_dot(normal, normal);
  if (t <= 0.0f || n2 < 1e-12f) return 0;
  normal = v3_scale(normal, 1.0f / sqrtf(n2));
  if (v3_dot(normal, d) <= 1e-6f) return 0;
  *hit = (gun_profile_hit_t){.t = t, .normal = normal,
                             .valid = 1, .ambiguous = ambiguous};
  return 1;
}

// Resolve against the exact final extremum, then inspect only witnesses within
// its tie band. This prevents A~B and B~C from making A transitively tied to C.
static gun_profile_hit_t gun_profile_hit_reduce(
    const gun_profile_hit_t *candidate, int n, int take_max) {
  int winner = -1;
  for (int i = 0; i < n; i++) {
    if (!candidate[i].valid) continue;
    if (winner < 0 || (take_max ? candidate[i].t > candidate[winner].t
                                : candidate[i].t < candidate[winner].t))
      winner = i;
  }
  if (winner < 0) return (gun_profile_hit_t){0};
  gun_profile_hit_t best = candidate[winner];
  int ambiguous = best.ambiguous;
  for (int i = 0; i < n; i++) {
    if (i == winner || !candidate[i].valid ||
        fabsf(candidate[i].t - best.t) > GUN_PROFILE_TIE_EPS)
      continue;
    ambiguous |= candidate[i].ambiguous ||
                 v3_dot(candidate[i].normal, best.normal) < 0.999f;
  }
  best.ambiguous = ambiguous;
  return best;
}

static gun_profile_hit_t gun_profile_polygon_hit(
    const gun_hold_profile_t *profile, v3 ga, v3 u, v3 d,
    float rx, float rz) {
  gun_profile_hit_t candidate[FIG_KMAX];
  int candidate_n = 0;
  int k = profile->sides;
  if (k < 4 || k > FIG_KMAX || rx <= 1e-7f || rz <= 1e-7f)
    return (gun_profile_hit_t){0};
  if (!g_prof_ready) fig_prof_init();
  if (FIG_KS[fig_pi(k)] != k) return (gun_profile_hit_t){0};
  v3 w = v3_cross(u, ga);  // fig_ring's physical second profile axis
  float wl = v3_len(w);
  if (wl < 1e-7f) return (gun_profile_hit_t){0};
  w = v3_scale(w, 1.0f / wl);
  float du = v3_dot(d, u), dw = v3_dot(d, w);
  const float (*pr)[2] = g_prof[fig_pi(k)];
  for (int i = 0; i < k; i++) {
    int j = (i + 1) % k;
    float x0 = pr[i][0] * rx, z0 = pr[i][1] * rz;
    float x1 = pr[j][0] * rx, z1 = pr[j][1] * rz;
    // g_prof is counter-clockwise. Its right-hand edge normal points outward.
    float nu = z1 - z0, nw = x0 - x1;
    float plane = nu * x0 + nw * z0;
    float den = nu * du + nw * dw;
    if (plane <= 0.0f || den <= 1e-8f) continue;
    float t = plane / den;
    v3 normal = v3_norm(v3_add(v3_scale(u, nu), v3_scale(w, nw)));
    if (candidate_n < FIG_KMAX &&
        gun_profile_hit_candidate(&candidate[candidate_n], t, normal, 0, d))
      candidate_n++;
  }
  return gun_profile_hit_reduce(candidate, candidate_n, 0);
}

// One section-aware query is the source for both scalar hold placement and any
// consumer that needs the physical winning face. `normal` is world-space and
// points out of the closed held union; `ambiguous` marks a facet/primitive seam.
static gun_profile_hit_t gun_profile_query(const gun_hold_profile_t *profile,
                                           v3 ga, v3 d, float along) {
  gun_profile_hit_t candidate[3];
  int candidate_n = 0;
  v3 u = gun_profile_u(profile, ga);
  if (v3_dot(u, u) < 1e-10f) return (gun_profile_hit_t){0};
  v3 v = v3_norm(v3_cross(ga, u));
  float x = fabsf(v3_dot(d, u)), z = fabsf(v3_dot(d, v));
  float rx, rz, center;
  gun_profile_section(profile, along, &rx, &rz, &center);
  float ellipse = 1.0f / sqrtf(x * x / (rx * rx) + z * z / (rz * rz));
  float octagon = fminf(rx / fmaxf(x, 1e-6f),
                  fminf(rz / fmaxf(z, 1e-6f),
                        1.42f / (x / rx + z / rz)));
  float surface = ellipse + (octagon - ellipse) * profile->box01;
  float du = v3_dot(d, u), dv = v3_dot(d, v);
  v3 base_n = d;
  if (profile->sides >= 4) {
    // Enumerate the actual g_prof edges. Selecting one rounded angle hides the
    // two physical faces at a vertex seam and can hand a pad a fictitious plane.
    gun_profile_hit_t polygon =
      gun_profile_polygon_hit(profile, ga, u, d, rx, rz);
    if (polygon.valid)
      candidate[candidate_n++] = polygon;
  } else if (profile->box01 >= 0.999f) {
    float tx = rx / fmaxf(fabsf(du), 1e-6f);
    float tz = rz / fmaxf(fabsf(dv), 1e-6f);
    float td = 1.42f /
               fmaxf(fabsf(du) / rx + fabsf(dv) / rz, 1e-6f);
    if (tx <= tz && tx <= td)
      base_n = v3_scale(u, du < 0.0f ? -1.0f : 1.0f);
    else if (tz <= td)
      base_n = v3_scale(v, dv < 0.0f ? -1.0f : 1.0f);
    else
      base_n = v3_add(v3_scale(u, (du < 0.0f ? -1.0f : 1.0f) / rx),
                      v3_scale(v, (dv < 0.0f ? -1.0f : 1.0f) / rz));
  } else if (profile->box01 <= 0.001f) {
    base_n = v3_add(v3_scale(u, surface * du / (rx * rx)),
                    v3_scale(v, surface * dv / (rz * rz)));
  }
  if (profile->sides < 4 &&
      gun_profile_hit_candidate(&candidate[candidate_n], surface, base_n, 0, d))
    candidate_n++;
  float center_shift = center - profile->center;
  gun_profile_ray_hit_t side = {0}, under = {0};
  (void)gun_profile_side_ray_exit(profile, du, dv, center_shift, &side);
  (void)gun_profile_under_ray_exit(profile, du, dv, center_shift, &under);
  if (side.valid &&
      gun_profile_hit_candidate(
        &candidate[candidate_n], side.t,
        v3_add(v3_scale(u, side.nu), v3_scale(v, side.nv)),
        side.ambiguous, d))
    candidate_n++;
  if (under.valid &&
      gun_profile_hit_candidate(
        &candidate[candidate_n], under.t,
        v3_add(v3_scale(u, under.nu), v3_scale(v, under.nv)),
        under.ambiguous, d))
    candidate_n++;
  return gun_profile_hit_reduce(candidate, candidate_n, 1);
}

static float gun_profile_radius_at(const gun_hold_profile_t *profile, v3 ga,
                                   v3 d, float along) {
  gun_profile_hit_t hit = gun_profile_query(profile, ga, d, along);
  return hit.valid ? hit.t : profile->ref_r;
}

static float gun_profile_radius(const gun_hold_profile_t *profile, v3 ga, v3 d) {
  return gun_profile_radius_at(profile, ga, d, 0.0f);
}

static v3 hand_profile_endpoint_center(const gun_hold_profile_t *profile,
                                       v3 ga, float along) {
  if (!profile->enabled)
    return (v3){{0, 0, 0}};
  float rx, rz, center;
  gun_profile_section(profile, along, &rx, &rz, &center);
  return v3_scale(gun_profile_u(profile, ga), center);
}

static v3 hand_profile_center(v3 ga, float along) {
  v3 a = hand_profile_endpoint_center(&g_hand_profile.endpoint[0], ga, along);
  v3 b = hand_profile_endpoint_center(&g_hand_profile.endpoint[1], ga, along);
  return v3_lerp(a, b, g_hand_profile.mix);
}

static float hand_profile_endpoint_radius(const gun_hold_profile_t *profile,
                                          v3 ga, v3 d, float along,
                                          float authored_r) {
  if (!profile->enabled) return authored_r;
  float surface = gun_profile_radius_at(profile, ga, d, along);
  return surface + (authored_r - profile->ref_r);
}

static float hand_profile_radius(v3 ga, v3 e1, v3 e2, float along, float ph,
                                 float authored_r) {
  v3 d = v3_add(v3_scale(e1, cosf(ph)), v3_scale(e2, sinf(ph)));
  float a = hand_profile_endpoint_radius(&g_hand_profile.endpoint[0], ga, d,
                                         along, authored_r);
  float b = hand_profile_endpoint_radius(&g_hand_profile.endpoint[1], ga, d,
                                         along, authored_r);
  return a + (b - a) * g_hand_profile.mix;
}

static v3 hand_pt(v3 gc, v3 ga, v3 e1, v3 e2, float along, float ph,
                  float rad) {
  rad = hand_profile_radius(ga, e1, e2, along, ph, rad);
  v3 d = v3_add(v3_scale(e1, cosf(ph)), v3_scale(e2, sinf(ph)));
  return v3_add(gc, v3_add(v3_scale(ga, along),
         v3_add(hand_profile_center(ga, along), v3_scale(d, rad))));
}

// Every independently proved palm/thumb/digit child exists at this named tier.
#define FIG_HAND_DETAIL_K 16
// Detailed hands keep one fixed 12-sided phalanx family in FP and 3P alike.
#define FIG_DIGIT_K (g_fig_k >= FIG_HAND_DETAIL_K ? 12 : 8)
// Aim the trigger index along a quadratic Bezier from metacarpal head to blade
// contact. The grip-surface control point bows the digit while keeping it between
// guard and receiver.
static v3 fig_trig_pt(v3 a, v3 c, v3 b, float t) {
  float u = 1.0f - t;
  return v3_add(v3_scale(a, u * u), v3_add(v3_scale(c, 2.0f * u * t),
                                           v3_scale(b, t * t)));
}

// The aimed tip's own cross-section, NAMED because the pad's stand-off from the blade
// is derived from it.
#define FTIP_RX 0.798f
#define FTIP_RZ 0.730f
// A pad PRESSES on a trigger, it does not pass through it: the surface stops on the
// steel and deforms about a millimetre and a half.
#define TRIG_PAD_SQUASH 0.0015f

// How much air a digit keeps from the guard's wire, before the girth scale. 6.0 mm and
// not 1.5, because the push moves NODES and the swept surface between two cleared nodes
// is a chord where the arc is not — a margin sized for the nodes leaves the BAND
// between them inside the tube.
#define HAND_GUARD_CLR 0.0090f

// Push a digit node out of the trigger guard's tube.
static v3 fig_body_clear(v3 p, float r) {
  // Two passes settle the rare case where leaving one adjacent proxy enters another.
  // The exact receiver body and compact proxy table are independent hard exclusions;
  // apply both on every pass so proxy presence cannot bypass receiver clearance.
  for (int pass = 0; pass < 2; pass++) {
    for (int kind = GUN_CONTACT_RECEIVER; kind <= GUN_CONTACT_CHASSIS; kind++) {
      const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[kind];
      for (int q = 0; q < set->count; q++) {
        const gun_contact_proxy_t *b = &set->box[q];
        if (!b->enabled) continue;
        p = gun_contact_clear(p, r, b);
      }
    }
    gun_contact_proxy_t body = {
      .c = g_gun_body_c,
      .e = {g_gun_body_e[0], g_gun_body_e[1], g_gun_body_e[2]},
      .enabled = 1,
    };
    p = gun_contact_clear(p, r, &body);
  }
  return p;
}

static v3 fig_guard_clear(v3 p, float r) {
  // The margin scales with the weapon's own girth for the same reason the
  // radii do: the third-person rifle draws at GUN_GIRTH_3P and its digits are
  // fatter in proportion, so a fixed millimetre margin covers less of the
  // chord sag between two cleared nodes there than it does in first person.
  float need = r + g_gun_guard_r + HAND_GUARD_CLR * g_gun_girth;
  for (int e = 0; e + 1 < 4; e++) {
    v3 a = g_gun_guard[e], ab = v3_sub(g_gun_guard[e + 1], a);
    float l2 = v3_dot(ab, ab);
    float u = l2 > 1e-9f ? f_clamp(v3_dot(v3_sub(p, a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    v3 c = v3_add(a, v3_scale(ab, u)), d = v3_sub(p, c);
    float len = v3_len(d);
    if (len < 1e-5f || len >= need) continue;
    p = v3_add(c, v3_scale(d, need / len));
  }
  return p;
}

// A swept band is contained by the capsule around its station-centre segment, but
// independently cleared endpoints do not constrain that capsule: a long chord may
// still cut back through the body envelope or curved guard. Project three interior
// witnesses and distribute each correction to the adjacent stations with the
// least-squares weights that move that witness by the complete correction. Repeating
// settles neighbouring segments that share a station; the ordinary node solve below
// retains final priority.
static void fig_exposed_segment_clear(fnode_t *node, int n) {
  static const float sample[3] = {0.25f, 0.50f, 0.75f};
  for (int pass = 0; pass < 3; pass++)
    for (int i = 0; i + 1 < n; i++)
      for (int j = 0; j < 3; j++) {
        float t = sample[j], u = 1.0f - t;
        float r0 = fmaxf(node[i].rx, node[i].rz);
        float r1 = fmaxf(node[i + 1].rx, node[i + 1].rz);
        float r = fmaxf(r0, r1);
        v3 p = v3_lerp(node[i].p, node[i + 1].p, t);
        v3 q = p;
        for (int solve = 0; solve < 2; solve++) {
          q = fig_body_clear(q, r + 0.0030f);
          q = fig_guard_clear(q, r);
        }
        v3 d = v3_sub(q, p);
        float den = u * u + t * t;
        node[i].p = v3_add(node[i].p, v3_scale(d, u / den));
        node[i + 1].p = v3_add(node[i + 1].p, v3_scale(d, t / den));
      }
}

// Production-authored contact patches.  The points are the hand skeleton projected
// onto the held surface before fig_chain sweeps it; the proof compares the FINAL
// triangles against these paths.  A bad chord therefore cannot move its own allowed
// patch onto an opposite wall.  This metadata is populated only while probe capture is
// active and has no runtime mesh-distance path.
#define FIG_CONTACT_PATCH_MAX 16
typedef struct {
  const char *name;
  int hand, v0, v1, n;
  v3 origin, axis, radial_ref;
  v3 p[FIG_NMAX], center[FIG_NMAX];
  float width_ax[FIG_NMAX], width_tan[FIG_NMAX];
  float along[FIG_NMAX], angle[FIG_NMAX], surface_r[FIG_NMAX];
  // Optional exact-facet certificate.  A zero tag preserves the legacy radial
  // patch proof byte-for-byte.  Tagged patches keep the endpoint value (never a
  // pointer to the transient hand blend) and bind it to one captured BODY range.
  gun_hold_profile_t endpoint_profile;
  uint32_t cert_tag;
} fig_contact_patch_t;

// cert_tag packs kind+1, captured range+1, one GUN_MESH_PART_* bit and the
// profile's authorized axial-section bands.  Zero disables the certificate.
// The range ordinal prevents a later same-part range from inheriting an older
// patch's authority; only captured render triangles in tagged bands are truth.
#define FIG_CONTACT_CERT_KIND_MASK UINT32_C(0x0f)
#define FIG_CONTACT_CERT_RANGE_SHIFT 4
#define FIG_CONTACT_CERT_RANGE_MASK UINT32_C(0xff)
#define FIG_CONTACT_CERT_PART_SHIFT 12
#define FIG_CONTACT_CERT_PART_MASK UINT32_C(0x1fff)
#define FIG_CONTACT_CERT_SECTION_SHIFT 25
#define FIG_CONTACT_CERT_SECTION_MASK UINT32_C(0x3f)
#define FIG_CONTACT_CERT_TAG(kind, range, part, section_mask) \
  ((uint32_t)((kind) + 1) | \
   ((uint32_t)((range) + 1) << FIG_CONTACT_CERT_RANGE_SHIFT) | \
   ((uint32_t)(part) << FIG_CONTACT_CERT_PART_SHIFT) | \
   ((uint32_t)(section_mask) << FIG_CONTACT_CERT_SECTION_SHIFT))
#define FIG_CONTACT_CERT_KIND(tag) \
  ((int)((tag) & FIG_CONTACT_CERT_KIND_MASK) - 1)
#define FIG_CONTACT_CERT_RANGE(tag) \
  ((int)(((tag) >> FIG_CONTACT_CERT_RANGE_SHIFT) & \
         FIG_CONTACT_CERT_RANGE_MASK) - 1)
#define FIG_CONTACT_CERT_PART(tag) \
  (((uint32_t)(tag) >> FIG_CONTACT_CERT_PART_SHIFT) & \
   FIG_CONTACT_CERT_PART_MASK)
#define FIG_CONTACT_CERT_SECTIONS(tag) \
  (((uint32_t)(tag) >> FIG_CONTACT_CERT_SECTION_SHIFT) & \
   FIG_CONTACT_CERT_SECTION_MASK)

// FIRE-F1C is deliberately not encoded in cert_tag.  That compact Phase-A tag
// identifies one BODY range; the firing-grip feature instead has two independent
// closed ranges (a hosted PANEL contact face and its q0 BODY backing) plus one exact
// hand source interval.  Keeping the descriptor separate makes an all-zero value a
// byte-exact no-feature state and prevents a malformed feature from falling back to
// the BODY certificate.
#define FIG_CONTACT_FIRE_F1C_MAGIC UINT32_C(0xf1c10c01)
typedef struct {
  uint32_t magic;
  int weapon, kind;
  int feature_range, backing_range, section;
  uint32_t feature_part, feature_host;
  int feature_source0, feature_source1, feature_source_count;
  int backing_full_lo, backing_full_hi;
  int backing_zero_lo, backing_zero_hi;
  int backing_seam_lo, backing_seam_hi, backing_source_count;
  int hand, hand_section, hand_v0, hand_v1;
  int hand_source_lo, hand_source_hi, hand_source_count;
  // Authored proof-table identity for the complete weapon capture surrounding
  // this feature.  It is never derived from the captured ranges themselves:
  // removing an emitter must disagree with the fixed census and fail closed.
  int foreign_census;
  v3 origin, normal, tangent, axial;
  float half_tangent, half_axial;
  float zero_half_tangent, zero_half_axial;
  float profile_epsilon, backing_min, foreign_radius;
} fig_contact_fire_f1c_t;
#define FIG_CONTACT_FIRE_F1C_MAX 3
static fig_contact_fire_f1c_t g_fig_contact_fire_f1c[FIG_CONTACT_FIRE_F1C_MAX];
static int g_fig_contact_fire_f1c_n, g_fig_contact_fire_f1c_overflow;

static fig_contact_patch_t g_fig_contact_patch[FIG_CONTACT_PATCH_MAX];
static int g_fig_contact_patch_n, g_fig_contact_patch_overflow;
static gun_hold_profile_t g_gun_grip_profile;
static gun_hold_profile_t g_gun_fore_profile;

typedef enum {
  FIG_HAND_CONTACT_NONE,
  FIG_HAND_CONTACT_GRIP,
  FIG_HAND_CONTACT_FORE,
  FIG_HAND_CONTACT_MAG,
  FIG_HAND_CONTACT_BOLT,
} fig_hand_contact_kind_t;
typedef struct {
  fig_hand_contact_kind_t from, to;
  float mix;
} fig_hand_contact_t;

// FP and 3P use this one semantic transition contract. `style` below describes the
// finger pose only; it must never decide which weapon surface moves a hand.
static fig_hand_contact_t fig_hand_contact_pose(int hand_slot,
                                                float mag01, float bolt01,
                                                int detached) {
  if (detached)
    return (fig_hand_contact_t){FIG_HAND_CONTACT_NONE,
                                FIG_HAND_CONTACT_NONE, 0.0f};
  if (hand_slot == FIG_HAND_SUPPORT)
    return (fig_hand_contact_t){FIG_HAND_CONTACT_FORE,
                                FIG_HAND_CONTACT_MAG,
                                f_clamp(mag01, 0.0f, 1.0f)};
  return (fig_hand_contact_t){FIG_HAND_CONTACT_GRIP,
                              FIG_HAND_CONTACT_BOLT,
                              f_clamp(bolt01, 0.0f, 1.0f)};
}

static float fig_hand_contact_weight(fig_hand_contact_t contact,
                                     fig_hand_contact_kind_t kind) {
  float a = contact.from == kind ? 1.0f - contact.mix : 0.0f;
  float b = contact.to == kind ? contact.mix : 0.0f;
  return a + b;
}

#ifndef _WIN32
static int fig_hand_contact_selftest(float *fore_weight, float *mag_weight,
                                     int *slide_none) {
  fig_hand_contact_t support =
    fig_hand_contact_pose(FIG_HAND_SUPPORT, 0.35f, 0.0f, 0);
  fig_hand_contact_t slide =
    fig_hand_contact_pose(FIG_HAND_SUPPORT, 0.0f, 0.0f, 1);
  fig_hand_contact_t bolt =
    fig_hand_contact_pose(FIG_HAND_FIRE, 0.0f, 0.60f, 0);
  float fore = fig_hand_contact_weight(support, FIG_HAND_CONTACT_FORE);
  float mag = fig_hand_contact_weight(support, FIG_HAND_CONTACT_MAG);
  int detached = slide.from == FIG_HAND_CONTACT_NONE &&
                 slide.to == FIG_HAND_CONTACT_NONE && slide.mix == 0.0f;
  if (fore_weight) *fore_weight = fore;
  if (mag_weight) *mag_weight = mag;
  if (slide_none) *slide_none = detached;
  return support.from != FIG_HAND_CONTACT_FORE ||
         support.to != FIG_HAND_CONTACT_MAG ||
         fabsf(fore - 0.65f) > 1e-6f || fabsf(mag - 0.35f) > 1e-6f ||
         !detached || bolt.from != FIG_HAND_CONTACT_GRIP ||
         bolt.to != FIG_HAND_CONTACT_BOLT || fabsf(bolt.mix - 0.60f) > 1e-6f;
}
#endif

static gun_hold_profile_t fig_hand_contact_profile(fig_hand_contact_kind_t kind) {
  gun_hold_profile_t profile = {0};
  if (kind == FIG_HAND_CONTACT_GRIP) {
    profile = g_gun_grip_profile;
  } else if (kind == FIG_HAND_CONTACT_FORE) {
    profile = g_gun_fore_profile;
  } else if (kind == FIG_HAND_CONTACT_MAG) {
    profile = g_gun_mag_profile;
  }
  return profile;
}

#ifndef _WIN32
static void fig_contact_patch_reset(void) {
  g_fig_contact_patch_n = 0;
  g_fig_contact_patch_overflow = 0;
  g_fig_contact_fire_f1c_n = 0;
  g_fig_contact_fire_f1c_overflow = 0;
}
#endif

static void fig_contact_patch_add(const char *name, int hand, int v0,
                                  const fnode_t *node, int n,
                                  v3 gc, v3 ga, v3 e1, v3 e2, float gr) {
  if (!g_probe_on || v0 >= g_scene_floats) return;
  if (g_fig_contact_patch_n >= FIG_CONTACT_PATCH_MAX || n > FIG_NMAX) {
    g_fig_contact_patch_overflow++;
    return;
  }
  fig_contact_patch_t *patch = &g_fig_contact_patch[g_fig_contact_patch_n++];
  *patch = (fig_contact_patch_t){.name = name, .hand = hand,
                                 .v0 = v0, .v1 = g_scene_floats, .n = n,
                                 .origin = gc, .axis = ga};
  for (int i = 0; i < n; i++) {
    patch->center[i] = node[i].p;
    v3 rel = v3_sub(node[i].p, gc);
    float along = v3_dot(rel, ga);
    v3 section_c = v3_add(gc, v3_add(v3_scale(ga, along),
                                     hand_profile_center(ga, along)));
    v3 radial = v3_sub(node[i].p, section_c);
    float len = v3_len(radial);
    v3 dir = len > 1e-6f ? v3_scale(radial, 1.0f / len) : e1;
    float ph = atan2f(v3_dot(dir, e2), v3_dot(dir, e1));
    float surface = hand_profile_radius(ga, e1, e2, along, ph, gr);
    patch->p[i] = v3_add(section_c, v3_scale(dir, surface));
    // On the contact surface rx is the phalanx/palm span along the held part and rz
    // is its complete pad half-width around the held part.  Keeping these axes
    // separate prevents a broad palm from accidentally blessing the weapon's
    // opposite wall; the proof measures the 2..4 mm bite independently.
    // The final bevel rings may project slightly beyond their station ellipse;
    // this one-triangle seam allowance keeps that same authored pad connected.
    patch->width_ax[i] = node[i].rx + 0.0035f;
    patch->width_tan[i] = node[i].rz + 0.0035f;
  }
  {
    v3 rel = v3_sub(patch->p[0], gc);
    float along = v3_dot(rel, ga);
    v3 radial = v3_sub(rel, v3_scale(ga, along));
    patch->radial_ref = v3_norm(radial);
    v3 side = v3_norm(v3_cross(ga, patch->radial_ref));
    for (int i = 0; i < n; i++) {
      rel = v3_sub(patch->p[i], gc);
      patch->along[i] = v3_dot(rel, ga);
      radial = v3_sub(rel, v3_scale(ga, patch->along[i]));
      patch->surface_r[i] = v3_len(radial);
      v3 dir = v3_scale(radial, 1.0f / patch->surface_r[i]);
      float angle = atan2f(v3_dot(dir, side), v3_dot(dir, patch->radial_ref));
      if (i)
        while (angle - patch->angle[i - 1] > F_PI) angle -= F_TAU;
      if (i)
        while (angle - patch->angle[i - 1] < -F_PI) angle += F_TAU;
      patch->angle[i] = angle;
    }
  }
}

[[maybe_unused]] static void fig_contact_fire_f1c_add(
    fig_contact_fire_f1c_t descriptor) {
  if (!g_probe_on) return;
  if (g_fig_contact_fire_f1c_n >= FIG_CONTACT_FIRE_F1C_MAX) {
    g_fig_contact_fire_f1c_overflow++;
    return;
  }
  g_fig_contact_fire_f1c[g_fig_contact_fire_f1c_n++] = descriptor;
}

static v3 fig_hand_hold_clear(v3 p, float pad_r, float bite,
                              v3 gc, v3 ga, v3 e1, v3 e2, float gr) {
  v3 rel = v3_sub(p, gc);
  float along = v3_dot(rel, ga);
  v3 section_c = v3_add(gc, v3_add(v3_scale(ga, along),
                                   hand_profile_center(ga, along)));
  v3 radial = v3_sub(p, section_c);
  float len = v3_len(radial);
  if (len < 1e-6f) return p;
  v3 dir = v3_scale(radial, 1.0f / len);
  float ph = atan2f(v3_dot(dir, e2), v3_dot(dir, e1));
  float need = hand_profile_radius(ga, e1, e2, along, ph,
                                   gr + pad_r - bite);
  return len < need ? v3_add(p, v3_scale(dir, need - len)) : p;
}

// Resolve the three constraints that own every exposed digit station. Held-surface
// compression is soft; receiver and guard exclusion are hard, so each pass applies
// them in that order and the guard has final priority. A second pass settles the
// corner case where leaving one expanded solid enters another. Hidden root stations
// deliberately do not call this helper: their closed caps belong inside the palm.
static v3 fig_hand_exposed_clear(v3 p, float held_r, float clear_r, int held,
                                 v3 gc, v3 ga, v3 e1, v3 e2, float gr) {
  for (int pass = 0; pass < 2; pass++) {
    if (held)
      p = fig_hand_hold_clear(p, held_r, 0.0034f, gc, ga, e1, e2, gr);
    p = fig_body_clear(p, clear_r);
    p = fig_guard_clear(p, clear_r);
  }
  return p;
}

// A rounded support segment changing from the rail's side support plane to its top
// plane needs one node on their intersection. Clearing the two endpoints against an
// OBB independently is insufficient: their straight chord cuts back through the
// corner. The long hero receiver proxy is the authored base/crown envelope; this
// projects the first top-supported station onto the same side plane as its predecessor.
static v3 fig_hand_ridge_corner(v3 prev, float prev_r, v3 p, float r) {
  const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[GUN_CONTACT_RECEIVER];
  for (int q = 0; q < set->count; q++) {
    const gun_contact_proxy_t *b = &set->box[q];
    float hx = v3_len(b->e[0]), hy = v3_len(b->e[1]), hz = v3_len(b->e[2]);
    if (!b->enabled || hx < 1e-6f || hy < 1e-6f || hz < 0.15f) continue;
    v3 ex = v3_scale(b->e[0], 1.0f / hx);
    v3 ey = v3_scale(b->e[1], 1.0f / hy);
    v3 pr = v3_sub(prev, b->c), cr = v3_sub(p, b->c);
    float px = v3_dot(pr, ex), py = v3_dot(pr, ey);
    float cx = v3_dot(cr, ex), cy = v3_dot(cr, ey);
    float sign = px < 0.0f ? -1.0f : 1.0f;
    int prev_side = fabsf(px) >= hx + prev_r - 0.0002f &&
                    py < hy + prev_r - 0.0002f;
    int curr_top = cy >= hy + r - 0.0002f &&
                   fabsf(cx) < hx + r - 0.0002f;
    if (prev_side && curr_top)
      p = v3_add(p, v3_scale(ex, sign * (hx + r) - cx));
  }
  return p;
}

#ifndef _WIN32
static float fig_hand_guard_gap(v3 p, float r) {
  float gap = 1e9f;
  float need = r + g_gun_guard_r + HAND_GUARD_CLR * g_gun_girth;
  for (int e = 0; e + 1 < 4; e++) {
    v3 a = g_gun_guard[e], ab = v3_sub(g_gun_guard[e + 1], a);
    float l2 = v3_dot(ab, ab);
    float u = l2 > 1e-9f
            ? f_clamp(v3_dot(v3_sub(p, a), ab) / l2, 0.0f, 1.0f) : 0.0f;
    gap = fminf(gap, v3_len(v3_sub(p, v3_add(a, v3_scale(ab, u)))) - need);
  }
  return gap;
}
#endif

// Held phalanges are authored as dense arcs around one weapon axis. fig_chain's
// general parallel-transport frame is ideal for limbs, but across a near-half-turn
// grip it can accumulate enough roll to connect opposite ring vertices and turn one
// band into a chord through the weapon. This narrow sweep rebuilds the frame from the
// shared authored hint at every station, preserving vertex correspondence explicitly.
static void fig_hand_chain(const fnode_t *node, int n, int k, v3 hint) {
  if (n < 2 || n > FIG_NMAX) return;
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  fring_t ring[FIG_NMAX];
  v3 prev_nx = {{0, 0, 0}};
  for (int i = 0; i < n; i++) {
    v3 ny = i == 0 ? v3_sub(node[1].p, node[0].p)
          : i == n - 1 ? v3_sub(node[n - 1].p, node[n - 2].p)
          : v3_sub(node[i + 1].p, node[i - 1].p);
    if (v3_dot(ny, ny) < 1e-10f) return;
    ny = v3_norm(ny);
    v3 nx = fig_perp(ny, hint);
    if (i && v3_dot(nx, prev_nx) < 0.0f) nx = v3_scale(nx, -1.0f);
    prev_nx = nx;
    ring[i] = fig_ring(node[i].p, ny, nx, node[i].rx, node[i].rz, k);
  }
  g_ao_mul = node[0].ao > 0.0f ? node[0].ao : 1.0f;
  fig_cap(&ring[0], -1.0f, node[0].col);
  if (!fig_round(k)) {
    for (int i = 1; i < n; i++) {
      g_ao_mul = node[i].ao > 0.0f ? node[i].ao : 1.0f;
      fig_band(&ring[i - 1], &ring[i], node[i].col);
    }
  } else {
    v3 na[FIG_NMAX][FIG_KMAX], nb[FIG_NMAX][FIG_KMAX];
    for (int i = 1; i < n; i++) {
      fig_ring_nrm(&ring[i - 1], ring[i - 1].c, ring[i].c, na[i]);
      fig_ring_nrm(&ring[i], ring[i - 1].c, ring[i].c, nb[i]);
    }
    for (int i = 1; i < n - 1; i++)
      for (int j = 0; j < k; j++) {
        v3 av = v3_add(nb[i][j], na[i + 1][j]);
        if (v3_dot(av, av) > 1e-12f)
          nb[i][j] = na[i + 1][j] = v3_norm(av);
      }
    for (int i = 1; i < n; i++) {
      g_ao_mul = node[i].ao > 0.0f ? node[i].ao : 1.0f;
      fig_band_smooth(&ring[i - 1], &ring[i], na[i], nb[i], node[i].col);
    }
  }
  g_ao_mul = node[n - 1].ao > 0.0f ? node[n - 1].ao : 1.0f;
  fig_cap(&ring[n - 1], 1.0f, node[n - 1].col);
  g_ao_mul = 1.0f;
}

// --- palm: wrist -> knuckle row, and the palm's own frame for the digits --
// THE BACK OF A HAND IS FLAT — only the fingers curl — so the palm is a straight
// tapered slab from the wrist to the knuckle row with a shallow cup in it, and the
// whole wrap lives in the digits. Every digit below used to be placed purely in the
// GRIP CYLINDER's polar frame — hand_pt(FAX[f], phi, rad) — while the palm is a chord
// from the CALLER's wrist to pk, so this also builds the palm's OWN frame (pbx/pu/
// root_c/root_x) the digits root into. Outputs: pk/rk (the knuckle-row point and its
// radius — also feeds the far-tier one-mass hand), p3/p4/prx3/prx4 (pm[3]/pm[4] and
// their authored half-widths — also feeds the far-tier hand's own root), pbx/pu/
// root_c/root_x (the digit root frame), and out_palm_dir (the caller's own out-param,
// written straight through, per the palm's own axis, so the arm can enter ALONG it).
static void fig_hand_palm(v3 wrist, v3 gc, v3 ga, float gr, v3 col,
                          v3 e1, v3 e2, float KN_PH, v3 *out_palm_dir,
                          v3 *out_pk, float *out_rk, v3 *out_p3, v3 *out_p4,
                          float *out_prx3, float *out_prx4,
                          float *out_prz3, float *out_prz4,
                          v3 *out_pbx, v3 *out_pu, v3 *out_root_c,
                          float *out_root_x, int hand_slot) {
  // The broad palm spans thin raised panels whose hidden back walls are not a valid
  // soft-contact band. Keep its inner face half a millimetre proud; the four rounded
  // digit pads below own the measured 2--4 mm grip compression.
  const float HAND_BITE = -0.0005f;
  static const float PRX[5] = {0.0267f, 0.0336f, 0.0399f, 0.0484f, 0.0452f};
  static const float PRZ[5] = {0.0168f, 0.0208f, 0.0206f, 0.0164f, 0.0113f};
  float rk = gr + PRZ[4] - HAND_BITE;
  v3 pk = hand_pt(gc, ga, e1, e2, 0.0f, KN_PH, rk);
  // The cup: 3 mm of sag toward the tube across the middle of the slab, which is
  // enough to stop the back of the hand reading as a plank and far too little to
  // trigger the bevel clamp above.
  v3 mid = v3_lerp(wrist, pk, 0.5f);
  v3 md = v3_sub(mid, gc);
  md = v3_sub(md, v3_scale(ga, v3_dot(md, ga)));
  v3 inw = v3_dot(md, md) > 1e-9f ? v3_scale(v3_norm(md), -0.0026f)
                                  : (v3){{0, 0, 0}};
  // Keep the back of the glove one continuous value. A short colour-only panel on
  // this small surface reads as a stray rectangle in both first and third person;
  // the palm taper and separate digits already provide the needed articulation.
  fnode_t pm[5];
  const float PT[5] = {0.0f, 0.0f, 0.42f, 0.78f, 1.0f};
  for (int i = 1; i < 5; i++) {
    pm[i].p = v3_lerp(wrist, pk, PT[i]);
    // The cup grows with distance from the wrist.
    pm[i].p = v3_add(pm[i].p, v3_scale(inw, PT[i] * PT[i]));
    pm[i].col = pm[i].bcol = col;
    pm[i].rx = PRX[i]; pm[i].rz = PRZ[i]; pm[i].roll = 0; pm[i].ao = 0.0f;
  }
  // Node 0 reaches BACK past the wrist along the palm's own direction, so whatever the
  // arm is — a sleeve chain on the figure, a forearm chain on the viewmodel — swallows
  // this end cap.
  v3 bk = v3_norm(v3_sub(pm[1].p, pm[2].p));
  pm[0].p = v3_add(pm[1].p, v3_scale(bk, 0.012f));
  pm[0].col = pm[0].bcol = col;
  pm[0].rx = PRX[0]; pm[0].rz = PRZ[0]; pm[0].roll = 0; pm[0].ao = 0.0f;
  // Clear the swept palm, not merely its digits. A conservative runtime OBB can be
  // wider than a chamfered receiver corner; a full ring radius keeps every segment
  // outside that proxy instead of leaving an edge to cut back through it.
  for (int i = 0; i < 5; i++) {
    pm[i].p = fig_body_clear(pm[i].p, fmaxf(pm[i].rx, pm[i].rz));
    pm[i].p = fig_hand_hold_clear(pm[i].p, pm[i].rz, HAND_BITE,
                                  gc, ga, e1, e2, gr);
  }
  bk = v3_norm(v3_sub(pm[1].p, pm[2].p));
  pk = pm[4].p;
  // The palm's own axis, handed back so the arm can enter ALONG it.
  if (out_palm_dir) *out_palm_dir = v3_scale(bk, -1.0f);
  // The last palm span turns almost a radian around the held surface. Two authored
  // polar stations keep its inner face on that surface instead of letting the broad
  // final quad cut across the grip as a chord.
  fnode_t palm_mesh[7] = {pm[0], pm[1], pm[2], pm[3], pm[3], pm[3], pm[4]};
  v3 r3 = v3_sub(pm[3].p, gc), r4 = v3_sub(pm[4].p, gc);
  float a3 = v3_dot(r3, ga), a4 = v3_dot(r4, ga);
  r3 = v3_sub(r3, v3_scale(ga, a3));
  r4 = v3_sub(r4, v3_scale(ga, a4));
  float ph3 = atan2f(v3_dot(r3, e2), v3_dot(r3, e1));
  float ph4 = atan2f(v3_dot(r4, e2), v3_dot(r4, e1));
  while (ph4 - ph3 > F_PI) ph4 -= F_TAU;
  while (ph4 - ph3 < -F_PI) ph4 += F_TAU;
  for (int j = 1; j <= 2; j++) {
    float u = (float)j / 3.0f;
    fnode_t *palm_mid = &palm_mesh[3 + j];
    palm_mid->rx = pm[3].rx + (pm[4].rx - pm[3].rx) * u;
    palm_mid->rz = pm[3].rz + (pm[4].rz - pm[3].rz) * u;
    palm_mid->col = palm_mid->bcol = v3_lerp(pm[3].col, pm[4].col, u);
    palm_mid->ao = pm[3].ao + (pm[4].ao - pm[3].ao) * u;
    palm_mid->p = hand_pt(gc, ga, e1, e2, a3 + (a4 - a3) * u,
                          ph3 + (ph4 - ph3) * u,
                          gr + palm_mid->rz - HAND_BITE);
    palm_mid->p = fig_body_clear(palm_mid->p,
                                  fmaxf(palm_mid->rx, palm_mid->rz));
    palm_mid->p = fig_hand_hold_clear(palm_mid->p, palm_mid->rz, HAND_BITE,
                                 gc, ga, e1, e2, gr);
  }
  int patch_v0 = g_scene_floats;
  if (g_fig_k >= FIG_HAND_DETAIL_K) {
    fig_hand_chain(palm_mesh, 7, 12, ga);   // hint = ga, so rx is the BREADTH
    fig_contact_patch_add("palm", hand_slot, patch_v0, palm_mesh, 7,
                          gc, ga, e1, e2, gr);
  } else {
    fnode_t palm_lod[5] = {palm_mesh[0], palm_mesh[1], palm_mesh[2],
                           palm_mesh[3], palm_mesh[6]};
    fig_hand_chain(palm_lod, 5, 12, ga);
    fig_contact_patch_add("palm", hand_slot, patch_v0, palm_lod, 5,
                          gc, ga, e1, e2, gr);
  }

  // --- THE PALM'S OWN FRAME, AND WHY THE DIGITS HAVE TO BE ROOTED IN IT -----
  v3 pu  = v3_norm(v3_sub(pm[4].p, pm[3].p));
  v3 pbx = v3_sub(ga, v3_scale(pu, v3_dot(ga, pu)));
  pbx = v3_dot(pbx, pbx) > 1e-9f ? v3_norm(pbx) : e1;
  // Where a digit may leave the hand: on the palm's CENTRE LINE, so the palm's
  // whole local thickness is available to swallow the root ring, ROOT_IN of the
  // last palm bone back from the knuckle row — where the section is 14.1 x 46.9
  // mm against 11.3 x 45.2 at the row itself.
  const float ROOT_IN = 0.55f;
  v3    root_c = v3_lerp(pm[4].p, pm[3].p, ROOT_IN);
  float root_x = PRX[4] + (PRX[3] - PRX[4]) * ROOT_IN;

  *out_pk = pk; *out_rk = rk;
  *out_p3 = pm[3].p; *out_p4 = pm[4].p;
  *out_prx3 = PRX[3]; *out_prx4 = PRX[4];
  *out_prz3 = PRZ[3]; *out_prz4 = PRZ[4];
  *out_pbx = pbx; *out_pu = pu; *out_root_c = root_c; *out_root_x = root_x;
}

// Build one close-range digit. `f` selects little through index; explicit arguments
// carry the grip frame, palm anchor frame, palm half-width, and knuckle radius. The
// caller owns iteration and probe scope.
static void fig_hand_digit(v3 gc, v3 ga, v3 e1, v3 e2, float gr, float rk,
                           float side, int style, v3 col,
                           const v3 *trig_pt, int slid,
                           v3 pbx, v3 root_c, float root_x, float prx4,
                           int f, int hand_slot) {
  // Each is an ARC about the grip axis: root buried in the palm's knuckle end, then
  // two thirds of a turn round the tube.
  // Roughly 20 mm centre pitch keeps adjacent ~22 mm digit sections from forming
  // same-tier near overlaps.
  static const float FAX[4] = {-0.0300f, -0.0100f, 0.0100f, 0.0295f}; // little->index
  // THE MIDDLE FINGER IS THE THICKEST, not the index.
  static const float FGR[4] = {0.0098f, 0.0110f, 0.0118f, 0.0111f};  // girth, x1.13
  float fax = FAX[f];
  // 6-8 degrees PAST THE CREST of the tube, measured on the player's own frame rather
  // than judged. Stopping at 12 o'clock put the whole finger row on the FAR side of
  // the fore-end, so the only digit the player ever saw was the thumb; going 13
  // degrees past laid all four tips FLAT across the top, which reads as "laid over
  // it" instead of "wrapped around it". The distal node also pitches further in,
  // because the last phalanx of a curled finger points at the palm.
  // Index-most-closed, not middle-most-closed: on a fore-end the index is the highest
  // and most wrapped finger. FKN also gave index and ring the same knuckle height, so
  // the arc across the knuckles was flat exactly where it should peak.
  // WRAP is x1.12 over the authored values — a closed hand on a 26 mm pistol grip
  // carries its tips ~200 degrees round, and at 139-170 the four digits read as lumps
  // stacked against the grip. Bounded by vmtrig: past x1.24 the index's own sweep
  // starts crowding the guard's front strap.
  static const float FSW[4] = {2.710f, 3.069f, 3.203f, 3.315f};      // wrap, rad
  static const float FLF[4] = {0.0015f, 0.0006f, 0.0000f, 0.0009f};  // radial lift
  static const float FKN[4] = {-0.22f, -0.08f, 0.01f, 0.06f};        // knuckle arc
  // Recomputed here from `side` rather than threaded from the wrapper: same
  // closed-form as fig_hand's own copy, bit-identical by construction — kept
  // local to spare a parameter rather than piped through as one.
  const float KN_PH = side * 1.52f;
  float r = FGR[f], rr = gr + r - 0.0034f + FLF[f];
  float ph0 = KN_PH + side * FKN[f];
  // THE TRIGGER FINGER. Four identical curled digits clamped on a pistol grip is a
  // clamp, not a firing grip: what says "about to shoot" is one finger doing
  // something different from the other three.
  float sweep = FSW[f], reach = 0.0f;
  if (style == 2) sweep *= f == 3 && g_fig_fp ? 0.42f : 0.58f;
  int aimed = style == 1 && f == 3 && trig_pt != NULL && !slid;
  if (style == 1 && f == 3 && !aimed) { sweep = 1.42f; reach = 0.0300f; }
  fnode_t fg[FIG_NMAX];   // 5 authored + critical stations + one dome
  // Finger anatomy stays at viewmodel scale even though third-person weapons are
  // deliberately 5% girthier.  Compensating that presentation scale here keeps the
  // authored trigger-pad depth identical in FP and 3P without a second fingertip
  // constant.
  float tip_rz = FTIP_RZ / fmaxf(g_gun_girth, 1.0f);
  // Node 0 sits BEHIND the knuckle row and inside the palm, so the root cap is
  // buried whatever the wrap angle; node 1 is the METACARPAL HEAD and is
  // deliberately fat enough to stand ~3 mm proud of the palm's distal end, which is
  // the only landmark the back of a hand has and the thing whose absence made this
  // read as a spade from the one angle the player looks at it from.
  static const float FT5[5] = {-0.16f, 0.08f, 0.46f, 0.76f, 1.00f};
  // 0.86 / 0.72 on the ROOT, not 1.16 / 0.90.
  static const float FRX5[5] = {0.86f, 1.16f, 1.04f, 0.94f, 0.76f};
  static const float FRZ5[5] = {0.72f, 1.11f, 0.94f, 0.84f, 0.64f};
  int nnode = 5;
  // The aimed index runs a Bezier instead of an arc: A = the metacarpal head, where
  // the digit leaves the palm; B = the blade, pulled back along the guard's own
  // axis by the finger's own radius so the PAD touches it rather than the tip's
  // centre landing inside the steel; C = out in front of the knuckle, which is what
  // bows the proximal phalanx away from the grip and takes the middle of the finger
  // clear of the guard's rear upright.
  v3 tA = {{0, 0, 0}}, tB = tA, tC = tA;
  if (aimed) {
    // The METACARPAL joint carries the whole digit onto the blade, so the bearing
    // is set here rather than at the last two nodes.
    float aph = ph0 + side * 0.30f;
    float arad = rr + 0.0018f;
    tA = hand_pt(gc, ga, e1, e2, fax - 0.0050f, aph, arad);
    // The tip's own half-thickness minus the squash: the pad's SURFACE
    // lands on the steel and deforms into it by TRIG_PAD_SQUASH, which is
    // what vmtrig's `pad` reads back (-0.8 mm at rest, -1.9 pressed).
    // Seat the final supporting facet (rather than its ideal ellipse) on the blade;
    // A 1.5 mm station bias keeps the exact k=12 facet on the authored
    // -0.5..-2.5 mm pad band while the neighbouring blade centreline remains at
    // the independent -4.5 mm limit.
    const float PAD_FACET_SEAT = -0.001500f;
    tB = v3_add(*trig_pt,
                v3_scale(g_gun_trig_n,
                         r * tip_rz - TRIG_PAD_SQUASH + PAD_FACET_SEAT));
    // THE CONTROL POINT IS THE ARC'S OWN TANGENT, and that is a manifold
    // requirement rather than a nicety.
    v3 tg = v3_sub(hand_pt(gc, ga, e1, e2, fax - 0.0050f,
                           aph + side * 0.08f, arad), tA);
    tg = v3_dot(tg, tg) > 1e-12f ? v3_norm(tg) : v3_norm(v3_sub(tB, tA));
            // 0.35, not 0.62, and the departure 0.30 rad rather than 0.60: with the
            // Bezier leaving a third of a radian further round the grip and bowing
            // out by two thirds of its own chord, the index left the knuckle, ran
            // AROUND the grip and then turned back to the trigger — a hard V that
            // read as a folded sausage from every angle behind the weapon.
    tC = v3_add(tA, v3_scale(tg, 0.35f * v3_len(v3_sub(tB, tA))));
  }
  for (int i = 0; i < nnode; i++) {
    float t = FT5[i];
    float frx = FRX5[i], frz = FRZ5[i];
    float node_rx = r * frx, node_rz = r * frz;
    if (style == 2 && i >= 1) {
      // The heel-grip arc changes supporting faces around a faceted guard. A circular
      // phalanx section is invariant under fig_chain's transported frame, so its
      // capsule radius remains the authored pad radius instead of an ellipse's broad
      // axis twisting into the forbidden wall between nodes.
      float pad = sqrtf(node_rx * node_rz);
      node_rx = node_rz = pad;
    }
    // Derive each exposed centre from its final pad radius so taper preserves the
    // authored bite along the complete distal segment.
    const float HAND_DIGIT_BITE = 0.0034f;
    float rad = i == 0 ? rk - 0.0022f
              : i == 1 ? rr + 0.0016f : gr + node_rz - HAND_DIGIT_BITE;
    rad += reach * t * t;              // straightens off the grip's front
    fg[i].p = hand_pt(gc, ga, e1, e2, fax, ph0 + side * sweep * t, rad);
    if (aimed && i >= 2) {
      // THE PRESS SLIDES THE NODES ALONG THE CURVE, IT DOES NOT BOW IT. The tip is
      // pinned to the blade and the two proximal nodes sit on the grip arc, so the
      // only thing a press can shape is what happens between them — and shaping it
      // with the CONTROL POINT was wrong: measured, pushing tC out by 0.30 took
      // node 3's turn from 71.5 to 92.8 degrees on the SR, and fig_chain answers a
      // corner it cannot fillet by collapsing the cross-section, so the fingertip
      // pinched and then flared into a trumpet exactly when the trigger was pulled.
      float u = (t - FT5[2]) / (1.0f - FT5[2]);
      fg[i].p = fig_trig_pt(tA, tC, tB, u);
    }
    fg[i].col = fg[i].bcol = col;
    int tip = i == nnode - 1;
    fg[i].rx = aimed && tip ? r * FTIP_RX
             : reach > 0.0f && tip ? r * 0.80f : node_rx;
    fg[i].rz = aimed && tip ? r * tip_rz
             : reach > 0.0f && tip ? r * 0.74f : node_rz;
    float pad_r = fmaxf(fg[i].rx, fg[i].rz);
    if (i > 0)
      fg[i].p = fig_hand_exposed_clear(fg[i].p, fg[i].rz, pad_r, !aimed,
                                       gc, ga, e1, e2, gr);
    fg[i].roll = 0.0f;
    // AO 0.88..0.96, not 0.58..0.66.
    fg[i].ao = 0.88f + 0.080f * (float)i / (float)(nnode - 1);
  }
  // A KNUCKLE IS A POINT ON THE PALM, NOT A BEARING ON THE TUBE. Node 0 is the only
  // node of a digit that is never seen and its only job is ATTACHMENT, so it
  // belongs in the palm's frame while nodes 1-4 stay on the wrap.
  const float ROOT_SPREAD = 0.92f;
  {
    v3 a = v3_add(root_c,
                  v3_scale(pbx, fax * (root_x / prx4) * ROOT_SPREAD));
    if (style == 2) {
      float pad = sqrtf(fg[0].rx * fg[0].rz);
      // The palm-hidden closing ring uses 0.62 of the exposed pad radius so its short
      // attachment feather remains contained.
      fg[0].rx = fg[0].rz = pad * 0.62f;
    }
    float root_pad = fmaxf(fg[0].rx, fg[0].rz);
    fg[0].p = a;
    if (style != 1 || aimed) fg[0].p = fig_body_clear(fg[0].p, root_pad);
  }
  if (!aimed) {
    // Exact critical stations on the authored polar curve, not chord lerps. They
    // bound every turn of the swept radial pad below 0.4 rad, so fig_chain never has
    // to bridge two different supporting faces with one deep bevel facet. The heel
    // grip uses circular sections because its transported frame crosses successive
    // guard faces; pistol-grip fingers retain their anatomical oval section.
    static const float CT[4] = {0.27f, 0.365f, 0.61f, 0.92f};
    static const float CX[4] = {1.10f, 1.07f, 0.99f, 0.82f};
    static const float CZ[4] = {1.025f, 0.98f, 0.89f, 0.707f};
    static const int CI[4] = {2, 3, 5, 7};
    fnode_t base2 = fg[2], base3 = fg[3], tip = fg[4];
    for (int j = 0; j < 4; j++) {
      fnode_t mid = CT[j] < 0.6f ? base2 : base3;
      float rx = r * CX[j], rz = r * CZ[j];
      if (style == 2) rx = rz = sqrtf(rx * rz);
      float rad = gr + rz - 0.0034f;
      mid.p = hand_pt(gc, ga, e1, e2, fax,
                      ph0 + side * sweep * CT[j], rad);
      mid.rx = rx; mid.rz = rz;
      mid.ao = 0.93f + 0.02f * (float)j;
      mid.p = fig_hand_exposed_clear(mid.p, mid.rz,
                                     fmaxf(mid.rx, mid.rz), 1,
                                     gc, ga, e1, e2, gr);
      fg[CI[j]] = mid;
    }
    fg[4] = base2;
    fg[6] = base3;
    fg[8] = tip;
    nnode = 9;
    if (style == 2)
      for (int i = 1; i < nnode; i++)
        fg[i].p = fig_hand_ridge_corner(
            fg[i - 1].p, fmaxf(fg[i - 1].rx, fg[i - 1].rz),
            fg[i].p, fmaxf(fg[i].rx, fg[i].rz));
    // Ridge-corner projection can move a node after its first constraint solve.
    // Finish every exposed station against the same ordered constraints.
    for (int i = 1; i < nnode; i++)
      fg[i].p = fig_hand_exposed_clear(
          fg[i].p, fg[i].rz, fmaxf(fg[i].rx, fg[i].rz), 1,
          gc, ga, e1, e2, gr);
    if (style == 1 && f == 3) {
      fig_exposed_segment_clear(fg + 1, nnode - 1);
      for (int i = 1; i < nnode; i++)
        fg[i].p = fig_hand_exposed_clear(
            fg[i].p, fg[i].rz, fmaxf(fg[i].rx, fg[i].rz), 1,
            gc, ga, e1, e2, gr);
    }
  }
  // Flatten the firing middle finger's hidden radial pad, then re-seat its centre
  // against the raked primary grip facet. Visible breadth remains governed by rx while
  // the inner surface preserves a closed 0.4 mm authored contact.
  if (style == 1 && f == 2)
    for (int i = 1; i < nnode; i++) {
      fg[i].rz = fmaxf(fg[i].rz - 0.0030f, 0.0050f);
      fg[i].p = fig_hand_hold_clear(
          fg[i].p, fmaxf(fg[i].rx, fg[i].rz), 0.0004f,
          gc, ga, e1, e2, gr);
    }
  // The aimed index gets its own probe row so `vmtrig` can ask about the ONE digit
  // that reaches into the weapon.
  int chain_nodes = nnode;
  if (nnode < FIG_NMAX) {
    int tip = nnode - 1, dome_i = nnode;
    v3 d = v3_sub(fg[tip].p, fg[tip - 1].p);
    float dl = v3_len(d);
    v3 dir = dl > 1e-6f ? v3_scale(d, 1.0f / dl) : ga;
    fg[dome_i] = fg[tip];
    fg[dome_i].rx = fg[tip].rx * 0.34f;
    fg[dome_i].rz = fg[tip].rz * 0.34f;
    // The support index closes almost normal to the AR rail's rear facet. Its dome
    // therefore needs 0.3 mm more run than the other tips: at the shared 0.58 factor
    // the end cap sat 0.02 mm parallel to that facet (figcheck z-fight), while 0.62
    // keeps the same grip bite and moves only the hidden closing fan.
    float dome = style == 2 && f == 3 ? 0.62f : 0.58f;
    fg[dome_i].p = v3_add(fg[tip].p,
                          v3_scale(dir, fmaxf(fg[tip].rx, fg[tip].rz) * dome));
    fg[dome_i].p = fig_hand_exposed_clear(
        fg[dome_i].p, fg[dome_i].rz,
        fmaxf(fg[dome_i].rx, fg[dome_i].rz), !aimed,
        gc, ga, e1, e2, gr);
    chain_nodes++;
  }
  int patch_v0 = g_scene_floats;
  // The palm buries this short 3 mm feather from a proud attachment cap to the first
  // compressed pad station. It cannot traverse a neighbouring rail land axially.
  fig_hand_chain(fg, chain_nodes, FIG_DIGIT_K, ga);
  fig_contact_patch_add(aimed ? "trigger_digit" : "grip_digits",
                        hand_slot, patch_v0, fg + 1, chain_nodes - 1,
                        gc, ga, e1, e2, gr);
}

// The thumb: the OPPOSITION, and the strongest grip cue there is — a hand whose
// digits all wrap the same way is a clamp. Self-contained given the palm's own
// knuckle point `pk` (the root has to be BURIED in the palm, not sitting on its
// surface) and the grip frame; it needs none of the other digits' root frame.
static void fig_hand_thumb(v3 wrist, v3 pk, v3 gc, v3 ga, v3 e1, v3 e2,
                           float gr, float side, int style, v3 gun_right, v3 col,
                           int hand_slot, int braced) {
  // The root has to be BURIED, and it was not: 25.0 mm along ga plus its own 12.8 mm
  // of half-breadth put its outer extent 3.8 mm outside a palm that is 34.0 mm there,
  // so the thumb's proximal end sat ON the palm surface with a dark seam under it and
  // read as a rod glued to the hand.
  float thumb_root0 = g_fig_fp ? 0.30f : 0.42f;
  float thumb_root1 = g_fig_fp ? 0.52f : 0.62f;
  float thumb_lift0 = g_fig_fp ? 0.0100f : 0.0150f;
  float thumb_lift1 = g_fig_fp ? 0.0280f : 0.0382f;
  v3 tb0 = v3_add(v3_lerp(wrist, pk, thumb_root0), v3_scale(ga, thumb_lift0));
  v3 tb1 = v3_add(v3_lerp(wrist, pk, thumb_root1), v3_scale(ga, thumb_lift1));
  // Tip at 100 degrees round the near side; the index tip comes over the top at about
  // 74 degrees the other way, so the two end ~26 mm apart on a 28 mm tube. That gap
  // IS the grip. Each node rides so its INNER face is 3 mm inside the surface — the
  // same HAND_BITE the palm and the fingers obey — because the thumb is the one digit
  // the player actually sees and it was the only one that never touched the weapon.
  // A THUMB ON A PISTOL GRIP DOES NOT WRAP IT: it lies ALONG the frame, because above
  // a pistol grip there is a RECEIVER and a thumb that keeps climbing runs into it —
  // the tip came out the FAR side of the AR's receiver as a severed blob, invisible to
  // every distance-based proof because the thumb is not inside anything, it is
  // THROUGH it. The support hand keeps the full wrap: a handguard has nothing above
  // it, and there the C the thumb makes with the fingers is the whole grip cue.
  // ...and it TIGHTENS with the press: the thumb is 3627 px of the AR hip frame
  // against the index finger's 12, so a squeeze that does not move it is a squeeze the
  // player cannot see.
  const float TWRAP = style == 1 ? 0.68f : 1.0f;
  v3 tb2, tb3, tb4;
  if (style == 2) {
    // FORWARD THUMB. The heel grip trades the C's opposition cue for the drive-
    // forward cue: phalanges at INCREASING axial stations along ga (little-finger ->
    // index -> muzzle) at a nearly constant bearing on the near-upper flank, radius
    // easing down so the pad RIDES the handguard surface (same HAND_BITE philosophy
    // as the wrap).
    tb2 = hand_pt(gc, ga, e1, e2, 0.0420f, -side * 0.84f, gr + 0.0070f);
    tb3 = hand_pt(gc, ga, e1, e2, 0.0560f, -side * 0.76f, gr + 0.0052f);
    // Stop two millimetres before the authored nose section: the final dome still
    // carries a full cap radius, so a centre on the end plane clipped q26's closure.
    tb4 = hand_pt(gc, ga, e1, e2, 0.0640f, -side * 0.70f, gr + 0.0033f);
  } else if (style == 1) {
    // The firing thumb rests longitudinally on the receiver-side flank. Use weapon
    // right, not wrist bearing, as its lateral surface normal; targets include each
    // node radius so the surface touches while every centre remains outside.
    v3 right = v3_norm(gun_right);
    float root_side = v3_dot(v3_sub(tb0, gc), right) < 0.0f ? -1.0f : 1.0f;
    v3 lat = braced && !g_fig_fp ? v3_scale(right, root_side)
                                  : v3_scale(right, -1.0f);
    float x1 = gr + 0.0190f;
    // Keep tb0 inside the palm. Projecting both proximal nodes onto the receiver
    // made a closed thumb mesh that was visibly detached from the hand.
    tb1 = v3_add(tb1, v3_scale(lat, x1 - v3_dot(v3_sub(tb1, gc), lat)));
    tb2 = v3_add(gc, v3_add(v3_scale(ga, 0.0300f),
                            v3_scale(lat, gr + 0.0185f)));
    tb3 = v3_add(gc, v3_add(v3_scale(ga, 0.0220f),
                            v3_scale(lat, gr + 0.0180f)));
    tb4 = v3_add(gc, v3_add(v3_scale(ga, 0.0140f),
                            v3_scale(lat, gr + 0.0160f)));
    if (braced && !g_fig_fp) {
      // In the slide the firing palm rolls under the action. Fold the exposed thumb
      // down the grip with it; leaving the ordinary longitudinal rest above the palm
      // puts the SR bolt between the root and tip even when both endpoints are clear.
      v3 down_grip = v3_scale(ga, -0.0250f);
      tb1 = v3_add(tb1, down_grip);
      tb2 = v3_add(tb2, down_grip);
      tb3 = v3_add(tb3, down_grip);
      tb4 = v3_add(tb4, down_grip);
    }
  } else {
    // The firing thumb lies on the receiver-side flank rather than tunnelling
    // through it. Its centres sit one phalanx radius outside the grip surface;
    // the proximal root remains buried in the palm and the three distal nodes
    // ease outward into a visible, longitudinal rest.
    tb2 = hand_pt(gc, ga, e1, e2, 0.0455f, -side * 0.62f * TWRAP,
                  gr + 0.0092f);
    tb3 = hand_pt(gc, ga, e1, e2, 0.0430f, -side * 1.28f * TWRAP,
                  gr + 0.0079f);
    tb4 = hand_pt(gc, ga, e1, e2, 0.0395f, -side * 1.76f * TWRAP,
                  gr + 0.0040f);
  }
  fnode_t th[FIG_NMAX] = {
    {tb0, col, col, 0.0119f, 0.0132f, 0, 0.86f},
    {tb1, col, col, 0.0134f, 0.0150f, 0, 0.92f},
    {tb2, col, col, 0.0123f, 0.0137f, 0, 0.96f},
    {tb3, col, col, 0.0110f, 0.0122f, 0, 0.96f},
    // The last node domes the tip instead of leaving a flat octagon facing
    // whatever the camera happens to be. Same trick on the fingers.
    {tb4, col, col, 0.0071f, 0.0078f, 0, 0.96f}};
  int nth = 5;
  if (style == 2) {
    // th[0] remains the palm-buried tb0. th[1] is the first handguard contact and
    // th[2] eases into the forward rest, so the connection is visible without an
    // extra ring or a second thumb mesh.
    float root_pad = sqrtf(th[1].rx * th[1].rz);
    th[1].rx = th[1].rz = root_pad;
    th[1].p = hand_pt(gc, ga, e1, e2, -0.0080f,
                      -side * (g_fig_fp ? 0.64f : 0.78f),
                      gr + root_pad - 0.0034f);
    for (int i = 4; i >= 2; i--) th[i + 1] = th[i];
    float pad = sqrtf(0.0128f * 0.0142f);
    th[2] = (fnode_t){
      hand_pt(gc, ga, e1, e2, 0.0180f, -side * 0.95f,
              gr + pad - 0.0034f), col, col, pad, pad, 0, 0.94f};
    nth = 6;
    // Circular exposed sections preserve their authored capsule radius while the
    // transported sweep frame turns from the palm onto the top-side support face.
    for (int i = 2; i < nth; i++) {
      pad = sqrtf(th[i].rx * th[i].rz);
      th[i].rx = th[i].rz = pad;
    }
  }
  // The thumb sweeps 1.9 rad over four bones, so every interior node earns a bevel
  // PAIR — five nodes is eight rings, which is more geometry than the whole far-tier
  // hand it sits on.
  for (int i = 0; i < nth; i++) {
    float pad_r = fmaxf(th[i].rx, th[i].rz);
    if (i == 0 && style == 1 && !g_fig_fp) {
      // The firing-thumb root must stay buried in the palm, but the one-handed
      // third-person slide can turn that hidden ring a couple of millimetres into
      // the receiver. Keep every 3P firing-root centre palm-buried and apply only the
      // proxy clearance; projecting it onto the receiver is what used to tear the
      // visible thumb away from the hand.
      th[i].p = fig_guard_clear(fig_body_clear(th[i].p, pad_r), pad_r);
    } else if (i >= 2)
      th[i].p = fig_hand_exposed_clear(th[i].p, th[i].rz, pad_r, style != 1,
                                       gc, ga, e1, e2, gr);
    else if (i > 0) {
      // Palm-buried attachment rings are not held-surface constraints, but they
      // still must not enter receiver steel.
      th[i].p = fig_body_clear(th[i].p, pad_r);
      th[i].p = fig_guard_clear(th[i].p, pad_r);
    }
  }
  g_fig_jit = 0.0f;
  if (g_fig_k >= FIG_HAND_DETAIL_K) {
    // One small closing shoulder turns the last flat cap into a rounded fingertip.
    // It exists only in the near hand tier; distant hands keep their two-ring mass.
    int tip = nth - 1;
    v3 tip_d = v3_sub(th[tip].p, th[tip - 1].p);
    float tip_l = v3_len(tip_d);
    if (nth < FIG_NMAX && tip_l > 1e-6f) {
      th[nth] = th[tip];
      th[nth].rx *= 0.30f;
      th[nth].rz *= 0.30f;
      th[nth].p = v3_add(th[tip].p, v3_scale(tip_d,
          0.52f * fmaxf(th[tip].rx, th[tip].rz) / tip_l));
      th[nth].p = fig_hand_exposed_clear(
          th[nth].p, th[nth].rz, fmaxf(th[nth].rx, th[nth].rz), style != 1,
          gc, ga, e1, e2, gr);
      nth++;
    }
    int patch_v0 = g_scene_floats;
    v3 hint = ga;
    if (style == 2) {
      v3 rel = v3_sub(th[2].p, gc);
      hint = v3_sub(rel, v3_scale(ga, v3_dot(rel, ga)));
    }
    fig_hand_chain(th, nth, FIG_DIGIT_K, hint);
    fig_contact_patch_add("thumb", hand_slot, patch_v0, th, nth,
                          gc, ga, e1, e2, gr);
  } else {
    fnode_t t3[3] = {th[0], th[2], th[4]};
    t3[2].rx = 0.0082f; t3[2].rz = 0.0092f;
    int patch_v0 = g_scene_floats;
    fig_chain(t3, 3, 8, 1, 1, ga);
    fig_contact_patch_add("thumb", hand_slot, patch_v0, t3, 3,
                          gc, ga, e1, e2, gr);
  }
  g_fig_jit = 1.0f;
}

// `style`: 1 = trigger hand (aimed index, TWRAP thumb cut), 0 = support hand
// closing a full C-wrap (magazine, bolt knob — anything gripped ACROSS), 2 =
// the support HEEL grip: palm heel under the handguard, fingers wrapping the
// far flank, THUMB POINTING FORWARD along the near-upper flank — the modern
// C-clamp-family hold both weapons now share on the fore-end.
static void fig_hand(v3 wrist, v3 gc, v3 ga, float gr, float side, v3 col,
                     int style, const v3 *trig_pt, int hand_slot, int braced,
                     fig_hand_contact_t contact, v3 gun_right,
                     v3 *out_palm_dir) {
  hand_slot = hand_slot == FIG_HAND_SUPPORT ? FIG_HAND_SUPPORT : FIG_HAND_FIRE;
  // `probe` ranges are deliberately flat, never nested. Remember the caller's outer
  // arm range, split the hand into disjoint children, then reopen the outer range for
  // whatever the caller draws after the hand. No triangle belongs to two groups.
  const char *outer_probe = g_probe_on && g_nprobe
                          ? g_probe[g_nprobe - 1].name : NULL;
  g_hand_profile = (gun_hold_profile_blend_t){0};
  contact.mix = f_clamp(contact.mix, 0.0f, 1.0f);
  ga = v3_norm(ga);
  v3 w = v3_sub(wrist, gc);
  float aw = v3_dot(w, ga);                     // where along the tube the hand is
  v3 e1 = v3_sub(w, v3_scale(ga, aw));
  float rw = v3_len(e1);
  if (rw < 1e-4f) {                             // wrist on the axis: any bearing
    e1 = v3_cross(ga, (v3){{0, 1, 0}});
    if (v3_dot(e1, e1) < 1e-8f) e1 = v3_cross(ga, (v3){{1, 0, 0}});
    rw = gr + 0.050f;
  }
  e1 = v3_norm(e1);
  v3 e2 = v3_cross(ga, e1);        // (e1, e2, ga) right-handed BY CONSTRUCTION
  g_hand_profile.endpoint[0] = fig_hand_contact_profile(contact.from);
  g_hand_profile.endpoint[1] = fig_hand_contact_profile(contact.to);
  g_hand_profile.mix = contact.mix;
  float mag_weight = fig_hand_contact_weight(contact, FIG_HAND_CONTACT_MAG);
  if (mag_weight > 0.0f && g_gun_mag_profile.enabled) {
    // A rounded pad spans the bevel beside a flat magazine face. The circular
    // GUN_HOLD reference otherwise puts that span too deep into the k=8 body,
    // especially on the much wider SR magazine. Fade the relief with the same
    // explicit FORE->MAG contact weight as the two real endpoint surfaces.
    float aspect = g_gun_mag_profile.rx /
                   fmaxf(g_gun_mag_profile.rz, 1e-6f);
    float relief = 0.0005f + f_clamp((aspect - 1.65f) * 0.0034f,
                                     0.0f, 0.0028f);
    gr += mag_weight * relief;
  }
  float bolt_weight = fig_hand_contact_weight(contact, FIG_HAND_CONTACT_BOLT);
  if (hand_slot == FIG_HAND_FIRE && bolt_weight > 0.0f) {
    // The round bolt knob is the one held part smaller than a fingertip. Its mesh
    // chords sit just outside the circular contact reference, so blend in 0.4 mm of
    // authored pad compression while the hand closes on it.
    gr -= 0.0004f * bolt_weight;
  }
  // A HAND IS A FIXED SIZE. The palm runs from the wrist, which is the caller's, to the
  // knuckle row, which is the weapon's — and if those two get far apart the palm simply
  // stretches to span the gap.
  float wrist_surface = hand_profile_radius(ga, e1, e2, aw, 0.0f, gr);
  const float HAND_REACH_LO = wrist_surface + 0.026f;
  const float HAND_REACH_HI = wrist_surface + 0.096f;
  // A hand is a fixed size in EVERY direction, and this clamp once fixed only one
  // of them: `rw` is the distance from the tube's AXIS, `aw` how far ALONG it the
  // wrist sits, and passing `aw` through untouched let a wrist a metre up the
  // barrel still produce a metre-long palm. 95 mm clears every live pose measured
  // and still catches a corpse's draped wrist and the hard-landing reach.
  const float HAND_AX_MAX = 0.095f;
  float rc = f_clamp(rw, HAND_REACH_LO, HAND_REACH_HI);
  float ac = f_clamp(aw, -HAND_AX_MAX, HAND_AX_MAX);
  // Whether the clamp had to MOVE the weapon out from under the wrist.
  int slid = (rc != rw || ac != aw);
  float wrist_mm = (rw - wrist_surface) * 1000.0f;
  if (wrist_mm < g_fig_hand_diag[hand_slot].wrist_mm)
    g_fig_hand_diag[hand_slot].wrist_mm = wrist_mm;
  g_fig_hand_diag[hand_slot].clamped |= slid;
  if (slid) g_fig_clamped |= 1u << hand_slot;
  if (slid) {
    gc = v3_sub(wrist, v3_add(v3_scale(ga, ac), v3_scale(e1, rc)));
    rw = rc; aw = ac;
  }

  // The knuckle row: 62 degrees round the tube from the wrist's bearing, riding 10.5 mm
  // off the surface so the palm's inner face bites 3 mm INTO it.
  const float KN_PH = side * 1.52f;
  v3 pk, p3, p4, pbx, pu, root_c;
  float rk, prx3, prx4, prz3, prz4, root_x;
  probe("palm");
  fig_hand_palm(wrist, gc, ga, gr, col, e1, e2, KN_PH, out_palm_dir,
                &pk, &rk, &p3, &p4, &prx3, &prx4, &prz3, &prz4,
                &pbx, &pu, &root_c, &root_x, hand_slot);
  g_fig_hand_diag[hand_slot].palm_dir = *out_palm_dir;
  g_fig_hand_diag[hand_slot].palm_valid = 1;
  g_fig_hand_diag[hand_slot].palm_mm = v3_len(v3_sub(pk, wrist)) * 1000.0f;

  // SEPARATE FINGERS ARE THE MOST EXPENSIVE THING ON THE FIGURE, so the gate is the
  // tightest tier: 16 = inside 6 m.
  if (g_fig_k >= 16) {
    // --- four fingers, close range only ------------------------------------
    g_fig_jit = 0.0f;    // a digit is not a crate — see the note on g_fig_jit
    for (int f = 0; f < 4; f++) {
      int aimed = style == 1 && f == 3 && trig_pt != NULL && !slid;
      probe(aimed ? "trigger_digit" : "grip_digits");
      fig_hand_digit(gc, ga, e1, e2, gr, rk, side, style, col, trig_pt, slid,
                     pbx, root_c, root_x, prx4, f, hand_slot);
    }
    g_fig_jit = 1.0f;
  } else {
    // Far tier: ONE mass, and it is STRAIGHT — two nodes, no interior node, therefore
    // no bevel and nothing that can be thinned. Not a simplification for looks: it is
    // the only shape this primitive can hold at this width, because fig_chain sizes a
    // bevel off the profile's CIRCUMRADIUS, so a 40 mm slab turning 67 degrees over a
    // 12 mm bone asked for 26.9 mm of fillet, got 7.3, and was thinned to 27%.
    // The root is the PALM's, exactly as the four fingers' roots are. A 2-node chain has
    // ONE ring plane, perpendicular to its own bone, so a full-breadth slab rooted on
    // the tube ARC drives its corners straight out through the back of the hand
    // (measured 17.9 mm, wandering with the pose). Rooted on the palm's centre line the
    // residual is sub-pixel. It has to stay TWO nodes, so sub-pixel rather than zero is
    // the target; the narrower root is also the anatomy — four merged fingers are
    // ~69 mm across, not the whole hand's 90.
    const float FAR_IN = 0.80f;
    fnode_t fg[2];
    probe("grip_digits");
    fg[0].p = v3_lerp(p4, p3, FAR_IN);
    fg[1].p = hand_pt(gc, ga, e1, e2, -0.0015f, KN_PH + side * 1.34f,
                      gr + 0.0098f);
    fg[0].col = fg[0].bcol = col; fg[1].col = fg[1].bcol = col;
    fg[0].rx = (prx4 + (prx3 - prx4) * FAR_IN) * 0.72f;
    fg[0].rz = (prz4 + (prz3 - prz4) * FAR_IN) * 0.78f;
    fg[1].rx = 0.0340f; fg[1].rz = 0.0096f;
    fg[1].p = fig_guard_clear(fg[1].p, fg[1].rx);
    fg[0].roll = fg[1].roll = 0; fg[0].ao = 0.90f; fg[1].ao = 0.96f;
    int patch_v0 = g_scene_floats;
    fig_chain(fg, 2, 8, 1, 1, ga);
    fig_contact_patch_add("grip_digits", hand_slot, patch_v0, fg, 2,
                          gc, ga, e1, e2, gr);
  }

  // --- thumb: the OPPOSITION, and the strongest grip cue there is -----------
  probe("thumb");
  fig_hand_thumb(wrist, pk, gc, ga, e1, e2, gr, side, style, gun_right, col,
                 hand_slot, braced);
  probe(outer_probe);
  g_hand_profile = (gun_hold_profile_blend_t){0};
}

// A corpse owns its solved wrist, not the separately dropped rifle. Build a compact,
// relaxed fist directly from wrist/forearm/side: no weapon hold, proxy lookup or joint
// mutation participates. Close corpses keep the six detailed child ranges; lower tiers
// collapse them into three closed masses so death rows stay cheaper than live figures.
static void fig_hand_relaxed(v3 wrist, v3 elbow, float side, v3 right, v3 col,
                             v3 *out_palm_dir) {
  const char *outer_probe = g_probe_on && g_nprobe
                          ? g_probe[g_nprobe - 1].name : NULL;
  v3 along = v3_sub(wrist, elbow);
  along = v3_dot(along, along) > 1e-8f ? v3_norm(along) : (v3){{0, -1, 0}};
  v3 lat = v3_sub(right, v3_scale(along, v3_dot(right, along)));
  lat = v3_dot(lat, lat) > 1e-8f ? v3_norm(lat) : (v3){{1, 0, 0}};
  v3 curl = v3_norm(v3_cross(lat, along));
  if (out_palm_dir) *out_palm_dir = along;

  if (g_fig_k < FIG_HAND_DETAIL_K) {
    probe("palm");
    fnode_t palm_lod[2] = {
      {v3_add(wrist, v3_scale(along, -0.010f)), col, col,
       0.026f, 0.017f, 0, 0.90f},
      {v3_add(wrist, v3_add(v3_scale(along, 0.052f),
                            v3_scale(curl, -0.006f))), col, col,
       0.036f, 0.015f, 0, 0.96f},
    };
    fig_chain(palm_lod, 2, 8, 1, 1, lat);

    probe("grip_digits");
    fnode_t digits_lod[2] = {
      {v3_add(wrist, v3_add(v3_scale(along, 0.050f),
                            v3_scale(curl, -0.006f))), col, col,
       0.0340f, 0.0105f, 0, 0.92f},
      {v3_add(wrist, v3_add(v3_scale(along, 0.010f),
                            v3_scale(curl, -0.030f))), col, col,
       0.0290f, 0.0088f, 0, 0.97f},
    };
    fig_chain(digits_lod, 2, 8, 1, 1, lat);

    probe("thumb");
    v3 thumb_root = v3_add(wrist, v3_add(v3_scale(along, 0.025f),
                                         v3_scale(lat, side * 0.034f)));
    fnode_t thumb_lod[2] = {
      {v3_add(thumb_root, v3_scale(lat, -side * 0.010f)), col, col,
       0.0120f, 0.0130f, 0, 0.90f},
      {v3_add(thumb_root, v3_add(v3_scale(along, 0.021f),
                                 v3_scale(curl, -0.022f))), col, col,
       0.0062f, 0.0068f, 0, 0.97f},
    };
    fig_chain(thumb_lod, 2, 8, 1, 1, lat);
    probe(outer_probe);
    return;
  }

  probe("palm");
  fnode_t palm[4] = {
    {v3_add(wrist, v3_scale(along, -0.010f)), col, col, 0.026f, 0.017f, 0, 0.90f},
    {v3_add(wrist, v3_scale(along,  0.016f)), col, col, 0.037f, 0.020f, 0, 0.92f},
    {v3_add(wrist, v3_scale(along,  0.044f)), col, col, 0.041f, 0.019f, 0, 0.94f},
    {v3_add(wrist, v3_add(v3_scale(along, 0.058f), v3_scale(curl, -0.006f))),
     col, col, 0.036f, 0.015f, 0, 0.96f},
  };
  fig_chain(palm, 4, 12, 1, 1, lat);

  static const float OFF[4] = {-0.026f, -0.009f, 0.009f, 0.026f};
  static const float RAD[4] = {0.0090f, 0.0102f, 0.0108f, 0.0098f};
  for (int f = 0; f < 4; f++) {
    v3 root = v3_add(wrist, v3_add(v3_scale(along, 0.052f),
                                    v3_scale(lat, OFF[f])));
    fnode_t digit[4] = {
      {v3_add(root, v3_scale(along, -0.010f)), col, col,
       RAD[f] * 1.05f, RAD[f], 0, 0.90f},
      {v3_add(root, v3_add(v3_scale(along, 0.010f), v3_scale(curl, -0.004f))),
       col, col, RAD[f], RAD[f] * 0.92f, 0, 0.93f},
      {v3_add(root, v3_add(v3_scale(along, 0.005f), v3_scale(curl, -0.020f))),
       col, col, RAD[f] * 0.88f, RAD[f] * 0.78f, 0, 0.96f},
      {v3_add(root, v3_add(v3_scale(along, -0.009f), v3_scale(curl, -0.025f))),
       col, col, RAD[f] * 0.58f, RAD[f] * 0.54f, 0, 0.97f},
    };
    probe("grip_digits");
    fig_chain(digit, 4, FIG_DIGIT_K, 1, 1, lat);
  }

  probe("thumb");
  v3 tr = v3_add(wrist, v3_add(v3_scale(along, 0.025f),
                                v3_scale(lat, side * 0.034f)));
  fnode_t thumb[4] = {
    {v3_add(tr, v3_scale(lat, -side * 0.010f)), col, col, 0.0120f, 0.0130f, 0, 0.90f},
    {v3_add(tr, v3_add(v3_scale(along, 0.013f), v3_scale(lat, side * 0.006f))),
     col, col, 0.0114f, 0.0122f, 0, 0.93f},
    {v3_add(tr, v3_add(v3_scale(along, 0.026f), v3_scale(curl, -0.010f))),
     col, col, 0.0098f, 0.0106f, 0, 0.96f},
    {v3_add(tr, v3_add(v3_scale(along, 0.021f), v3_scale(curl, -0.022f))),
     col, col, 0.0062f, 0.0068f, 0, 0.97f},
  };
  fig_chain(thumb, 4, FIG_DIGIT_K, 1, 1, lat);
  probe(outer_probe);
}

// ONE UPPER ARM, FOR BOTH VIEWS. This is defined before kit_t in the include
// order, so the six authored colour stations are plain arguments. The caller
// still owns its probe, material and profile choice; node order lives here once.
static inline __attribute__((always_inline))
void fig_upper_arm(v3 shoulder, v3 elbow, v3 into, v3 hint,
                   v3 c0, v3 c1, v3 c2, v3 c3, v3 c4, v3 c5, int k) {
  v3 ua = v3_sub(elbow, shoulder);
  // At the near smooth tier the deltoid and biceps carry a small mirrored lateral
  // mass instead of sharing the centreline of a tapered tube. `into` points toward
  // the torso for either arm, so its negative is the handed outboard direction.
  v3 outboard = v3_scale(v3_norm(into), -1.0f);
  float near_k = k >= 12 ? 1.0f : 0.0f;
  fnode_t ar[6] = {
    {v3_add(shoulder, v3_scale(into, 0.052f)), c0, c0,
     0.0540f, 0.0580f, 0, 0.74f},
    {v3_add(v3_add(shoulder, v3_scale(ua, 0.020f)),
            v3_scale(outboard, 0.0060f + 0.0035f * near_k)), c1, c1,
     0.0680f + 0.0015f * near_k, 0.0700f - 0.0010f * near_k, 0, 0.80f},
    {v3_add(v3_add(shoulder, v3_scale(ua, 0.240f)),
            v3_scale(outboard, 0.0030f * near_k)), c2, c2,
     0.0650f + 0.0010f * near_k, 0.0675f - 0.0005f * near_k, 0, 0.0f},
    {v3_add(v3_add(shoulder, v3_scale(ua, 0.500f)),
            v3_scale(outboard, 0.0020f * near_k)), c3, c3,
     0.0580f, 0.0605f, 0, 0.0f},
    {v3_add(shoulder, v3_scale(ua, 0.680f)), c4, c4,
     0.0590f, 0.0620f, 0, 0.0f},
    {elbow, c5, c5, 0.0425f, 0.0445f, 0, 0.0f}};
  fig_chain(ar, 6, k, 1, 1, hint);
}

// ONE FOREARM, FOR BOTH VIEWS. The arm the player sees in first person and the arm on
// his own body in third person were two different objects: nine nodes with a gauntlet,
// a pronation roll and a five-step value ramp at profile 12, against three nodes of
// plain tapered sleeve at profile 8.
static void fig_forearm(v3 wrist, v3 back, v3 pd, v3 lat, float side,
                        v3 sleeve, v3 gnt, const v3 *elbow, int k) {
  // A MONOTONE ramp, and only three steps in it.
  v3 slv1 = v3_scale(sleeve, 0.94f), slv2 = v3_scale(sleeve, 0.86f);
  v3 slv3 = v3_scale(sleeve, 0.76f), slv4 = v3_scale(sleeve, 0.58f);
  float sc = 1.0f;
  if (elbow) {
    float l = v3_len(v3_sub(*elbow, wrist));
    if (l > 0.06f) { sc = l / 0.268f; back = v3_scale(v3_sub(*elbow, wrist), 1.0f / l); }
  }
  // `fig_forearm` receives the pose after sdf_rifle's reload/bolt rigid transform
  // and elbow re-solve. Publish that final wrist-to-elbow axis beside the palm axis;
  // a proof that rereads anim.pose would silently measure the pre-action skeleton.
  {
    int hand_slot = side > 0.0f ? FIG_HAND_FIRE : FIG_HAND_SUPPORT;
    float back_l2 = v3_dot(back, back);
    if (back_l2 > 1e-8f) {
      g_fig_hand_diag[hand_slot].forearm_back =
          v3_scale(back, 1.0f / sqrtf(back_l2));
      g_fig_hand_diag[hand_slot].forearm_valid = 1;
    }
  }
  // The ellipse ROLLS down the arm, which is pronation and is why a real forearm
  // looks different from every angle while a swept tube looks the same from all
  // of them. `fnode_t.roll` costs nothing — it turns the ring in the transported
  // frame — and 0 at the wrist to ~17 degrees at the elbow is the twist a hand
  // on a fore-end actually puts in. The small lateral drift is the ulnar bow.
  #define FAN(T, RX, RZ, C, AO)                                            \
    {v3_add(v3_add(wrist, v3_scale(back, (T) * sc)),                       \
            v3_scale(lat, side * (T) * sc * 0.075f)), C, C, RX, RZ,        \
     side * (T) * 1.10f, AO}
  fnode_t fa[9] = {
    // Node 0 sits 26 mm INSIDE the palm, along the PALM's own axis rather than the
    // arm's, so the sleeve's end cap is buried in a cross-section it actually fits
    // inside.
    {v3_add(wrist, v3_scale(pd, 0.026f)), sleeve, sleeve, 0.0240f, 0.0122f, 0, 0.0f},
    // THE CUFF IS A FAT NODE OF THIS CHAIN, not a nested one: as its own chain it had
    // to cross the sleeve's surface to bury its far cap, and two 12-gons transported
    // from DIFFERENT first bones are angularly out of phase, so the intersection came
    // out as a zig-zag torn edge round the whole wrist.
    FAN(-0.010f, 0.0316f, 0.0234f, gnt,    0.99f),   // cuff lip, proud in BOTH axes
    FAN( 0.008f, 0.0342f, 0.0260f, gnt,    0.96f),   // cuff crown, tight to the wrist
    FAN( 0.030f, 0.0338f, 0.0254f, gnt,    0.93f),
    FAN( 0.050f, 0.0368f, 0.0278f, sleeve, 0.90f),   // leather -> cloth
    FAN( 0.088f, 0.0392f, 0.0302f, slv1,   0.85f),
    FAN( 0.140f, 0.0434f, 0.0338f, slv2,   0.77f),
    FAN( 0.200f, 0.0470f, 0.0370f, slv3,   0.62f),   // the flexor belly
    // -2.9 degrees on the last band, not -4.7: a band that pinches out hard
    // reads as an amputated stub in any debug view, where a gentle one reads as
    // an arm continuing off frame.
    FAN( 0.268f, 0.0426f, 0.0336f, slv4,   0.46f)};
  #undef FAN
  // Past 12 m the cuff's three rings are under two pixels apart and the whole gauntlet
  // is one band, so the far tier keeps only the stations that carry the silhouette: the
  // buried root, the cuff crown, the leather/cloth boundary, the flexor belly and the
  // elbow.
  if (g_fig_k >= 12) {
    // Near anatomy keeps the same nine stations and profile budget, but stops
    // interpolating one round cone. A tight post-cuff waist opens into the
    // brachioradialis/flexor belly, the inner depth stays flatter, and the
    // ulnar bow returns to the solved elbow instead of missing it by 20 mm.
    fnode_t fn[9];
    for (int i = 0; i < 9; i++) fn[i] = fa[i];
    static const float FT[9] = {0.0f, -0.010f, 0.008f, 0.030f, 0.050f,
                                0.088f, 0.140f, 0.200f, 0.268f};
    static const float BOW[9] = {0.0f, -0.0008f, 0.0006f, 0.0022f, 0.0040f,
                                 0.0070f, 0.0100f, 0.0090f, 0.0020f};
    for (int i = 1; i < 9; i++)
      fn[i].p = v3_add(v3_add(wrist, v3_scale(back, FT[i] * sc)),
                        v3_scale(lat, side * BOW[i] * sc));
    fn[3].rx = 0.0328f; fn[3].rz = 0.0240f;  // cuff fall
    fn[4].rx = 0.0340f; fn[4].rz = 0.0246f;  // sleeve waist
    fn[5].rx = 0.0395f; fn[5].rz = 0.0280f;
    fn[6].rx = 0.0460f; fn[6].rz = 0.0320f;  // brachioradialis
    fn[7].rx = 0.0490f; fn[7].rz = 0.0350f;  // flexor belly
    fn[8].rx = 0.0408f; fn[8].rz = 0.0312f;  // elbow taper
    fn[4].roll = side * 0.070f;
    fn[5].roll = side * 0.120f;
    fn[6].roll = side * 0.200f;
    fn[7].roll = side * 0.270f;
    fn[8].roll = side * 0.310f;
    fig_chain(fn, 9, k, 1, 1, lat);
  } else {
    fnode_t f5[5] = {fa[0], fa[2], fa[4], fa[7], fa[8]};
    fig_chain(f5, 5, k, 1, 1, lat);
  }
}

// A HOLLOW round tube: two concentric walls closed by an annulus at each end. fig_chain
// closes a chain with a fan to its own axis, which is right for a solid body and
// exactly wrong for anything the player has to see THROUGH — the window of a red dot,
// the bore of a muzzle brake.
static void fig_tube(v3 a, v3 b, float rox, float roz, float rix, float riz,
                     int k, v3 col, v3 hint) {
  if (fig_diss_cut(v3_lerp(a, b, 0.5f))) return;
  if (!g_prof_ready) fig_prof_init();
  v3 ax = v3_sub(b, a);
  float l = v3_len(ax);
  if (l < 1e-5f || rix >= rox || riz >= roz || rix <= 0.0f) return;
  ax = v3_scale(ax, 1.0f / l);
  if (k > g_fig_k) k = g_fig_k < 4 ? 4 : g_fig_k;
  v3 nx = fig_perp(ax, hint);
  fring_t ao = fig_ring(a, ax, nx, rox, roz, k), bo = fig_ring(b, ax, nx, rox, roz, k);
  fring_t ai = fig_ring(a, ax, nx, rix, riz, k), bi = fig_ring(b, ax, nx, rix, riz, k);
  int round = fig_round(k);
  v3 rn[FIG_KMAX], fn[FIG_KMAX];
  for (int i = 0; i < k; i++) {
    rn[i] = v3_norm(v3_sub(ao.v[i], a));                       // per vertex
    v3 e = v3_sub(ao.v[(i + 1) % k], ao.v[i]);
    v3 f = v3_cross(e, ax);                                    // per face
    fn[i] = v3_dot(f, f) > 1e-14f ? v3_norm(f) : rn[i];
    if (v3_dot(fn[i], rn[i]) < 0.0f) fn[i] = v3_scale(fn[i], -1.0f);
  }
  for (int i = 0; i < k; i++) {
    int j = (i + 1) % k;
    v3 n0 = round ? rn[i] : fn[i], n1 = round ? rn[j] : fn[i];
    scene_push(ao.v[i], n0, col);  scene_push(bo.v[i], n0, col);
    scene_push(bo.v[j], n1, col);  scene_push(ao.v[i], n0, col);
    scene_push(bo.v[j], n1, col);  scene_push(ao.v[j], n1, col);
    v3 m0 = v3_scale(n0, -1.0f), m1 = v3_scale(n1, -1.0f);
    scene_push(ai.v[j], m1, col);  scene_push(bi.v[j], m1, col);
    scene_push(bi.v[i], m0, col);  scene_push(ai.v[j], m1, col);
    scene_push(bi.v[i], m0, col);  scene_push(ai.v[i], m0, col);
    scene_quad(ai.v[i], ao.v[i], ao.v[j], ai.v[j], v3_scale(ax, -1.0f), col);
    scene_quad(bi.v[j], bo.v[j], bo.v[i], bi.v[i], ax, col);
  }
}
