// =============================================================================
// sim/skeleton.inc  —  anim_tick, skel_solve, anim_carry_floor. Do NOT
// decompose anim_tick/skel_solve further: a verbatim block move changes FP
// rounding under -O3 -ffast-math -flto (measured: one hoist flipped flank_pct
// by a full tick). SKEL_POSE_ONLY advances no state, so event/proof restaging may
// RE-RUN a pose whose blends changed mid-tick — a shot does exactly that.
// SKEL_ADVANCE_STATE mutates the persistent ground/ankle followers and belongs only
// to anim_tick/reset.
// Proof: skel, kine, figm, netfigure, vmframe. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// =============================================================================
// ---------------------------------------------------------------------------
// Skeleton locomotion — procedural humanoid animation for every third-person
// figure (bots today, any future visible player body). No keyframe clips: the pose is
// solved fresh every tick from the character's physics state.
// ---------------------------------------------------------------------------

#define ANIM_ANKLE_Y  0.075f  // ankle joint height with the sole on the floor
#define ANIM_THIGH    0.435f
#define ANIM_SHIN     0.425f
// USABLE leg reach, hip joint to ankle.
#define ANIM_LEG      (ANIM_THIGH + ANIM_SHIN)
#define ANIM_REACH_K  0.965f
#define ANIM_HIP_HALF 0.095f  // half distance between the hip joints
// Hip joint height at MID-STANCE: ~93% of thigh+shin above the ankle, so the support
// knee passes over the foot at ~136 deg — visibly flexed rather than locked.
#define ANIM_HIP_Y    0.872f
#define ANIM_HIP_BOB  0.050f
// 5-7 deg of toe-out is the population-normal foot progression angle; a
// perfectly parallel plant reads subtly robotic and toe-IN reads injured.
#define ANIM_TOE_OUT  0.14f
#define ANIM_STANCE_MIN 0.090f  // each foot stays this far on its own side of
                                // the hip midline — legs cannot cross
#define ANIM_TWIST_MAX  1.10f   // ~63 deg: how far the hips may lead the aim
// 0.44, DOWN from 0.55.
#define ANIM_HIP_CR   0.44f   // crouched
#define ANIM_HIP_SL   0.19f   // sliding: the trail-leg HIP is the contact
// Arm segments, measured rather than eyeballed.
#define ANIM_UARM     0.315f
#define ANIM_FARM     0.265f
// Half the biacromial breadth (415 mm p50), pulled in slightly to the joint centres.
#define ANIM_SHO_HALF 0.212f
// The shouldered shoulder-line blade (rad) and the ARM's two-bone reach guard
// (the legs have their own, ANIM_REACH_K 0.965 above) — hoisted because the
// FIRST-PERSON arms (vm_build) anchor their shoulders and clamp their reach
// off the same numbers, and a retune that moved only one copy would silently
// split the FP man from the 3P one.
#define ANIM_BLADE       0.38f
#define ANIM_ARM_REACH_K 0.96f
// Swing fraction of the FULL cycle per foot (stance = 1 - duty).
#define ANIM_DUTY     0.40f   // walking
#define ANIM_DUTY_RUN 0.78f   // sprinting: >0.5 means BOTH feet leave the ground
#define ANIM_DUTY_REF 11.0f   // m/s at which the duty reaches ANIM_DUTY_RUN
// Fraction of the leg's full fore/aft span one stance may actually consume; the
// rest is the margin the reach clamp in skel_solve needs in order to stay unused.
#define ANIM_SPAN_K   0.90f
#define ANIM_DUTY_MAX 0.88f   // never leave a foot less than 12% of the cycle on
                              // the ground — below that a plant is a touch
#define ANIM_RUN_REF  5.5f    // m/s that counts as "full run" for pose scaling

// Presentation-only Lean step: 80/25 mm foot travel with a 30 mm local
// weight-transfer drop. The small same-direction hip follow shortens the active leg
// enough for 8--15 degrees of extra knee bend. These values are published beside the
// state and diagnostics so the harness proves production values rather than copies.
#define LEAN_STEP_OUT_M       0.080f
#define LEAN_STEP_FWD_M       0.025f
#define LEAN_STEP_HIP_DROP_M  0.030f
#define LEAN_STEP_HIP_FOLLOW_M 0.032f
#define LEAN_STEP_CROUCH_K    0.52f
// The toe-out boot needs 72 mm of radial clearance at its widest projected edge. This
// module owns the 80 mm Lean-only floor; the renderer applies no second correction.
#define LEAN_STEP_MARGIN_M    0.080f
#define LEAN_STEP_RESPONSE_HZ 3.5f
#define LEAN_PRESENT_HZ      40.0f

// Presentation recoil has one authored source for both views. Impulses are velocities:
// the event tick changes no pose value, then the fixed-tick springs produce a fast
// shoulder compression, a later muzzle rise and a very small deterministic side beat.
// Gameplay recoil/spread and their RNG stream never read this table.
typedef struct {
  // Unit gun-space human targets for one FP hand. Hip and ADS are explicit even
  // where they currently agree, so a later sight retune cannot silently change the
  // firing grip. Reload and bolt bearings/offsets are bounded end targets, blended
  // only by the production gunanim lobes.
  v3 bearing_hip, bearing_ads, bearing_reload, bearing_bolt;
  float axial_hip, axial_ads, lift_hip, lift_ads;
  float reload_axial, reload_lift, bolt_axial, bolt_lift;
  float ext_hip, ext_ads, dev_hip, dev_ads;
  float bias_hip, bias_ads, bias_reload;
} gun_hand_pose_t;

typedef struct {
  float vm_back_v, vm_lift_v, vm_side_v;
  float body_back_v, body_lift_v, body_side_v;
  float ads_k;
} recoil_pose_t;

static const recoil_pose_t RECOIL_POSE[WPN_COUNT] = {
  [WPN_AR] = {1.25f, 0.95f, 0.20f, 3.10f, 1.15f, 0.24f, 0.72f},
  [WPN_SR] = {2.25f, 2.90f, 0.32f, 5.30f, 3.00f, 0.36f, 0.65f},
};

// Solved against the 120 Hz semi-implicit recurrence: back peaks on tick 3 and lift
// on tick 5, both settle inside ~200 ms with a small first overshoot.
#define RECOIL_BACK_K 1200.0f
#define RECOIL_BACK_C   44.0f
#define RECOIL_LIFT_K  600.0f
#define RECOIL_LIFT_C   36.0f
#define RECOIL_SIDE_K 1000.0f
#define RECOIL_SIDE_C   45.0f

static const recoil_pose_t *recoil_pose(int wpn) {
  return &RECOIL_POSE[wpn >= 0 && wpn < WPN_COUNT ? wpn : WPN_AR];
}

// Stateless presentation variation. The pattern chooses the learnable side whenever
// it has one; its zero steps use a hash bit. A different hash byte adds only +/-15%
// amplitude, with no draw from either gameplay RNG stream.
static uint32_t recoil_hash(int src, int shot, int wpn) {
  uint32_t x = (uint32_t)src * 0x9e3779b9u ^
               (uint32_t)shot * 0x85ebca6bu ^
               (uint32_t)wpn * 0xc2b2ae35u ^ 0x27d4eb2fu;
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}

static float recoil_side_sample(int src, int shot, int wpn) {
  uint32_t h = recoil_hash(src, shot, wpn);
  float p = GUN_PATTERN[(uint32_t)shot & 31u];
  float sign = p > 0.001f ? 1.0f : p < -0.001f ? -1.0f
                                                   : (h & 1u) ? 1.0f : -1.0f;
  float amp = 0.85f + (float)((h >> 8) & 255u) * (0.30f / 255.0f);
  return sign * amp;
}

static float recoil_brace(const recoil_pose_t *r, float ads) {
  return 1.0f + (r->ads_k - 1.0f) * f_clamp(ads, 0.0f, 1.0f);
}

// The local view and the event-fed body deliberately share the same source values,
// bracing rule and stateless side sample. Proofs call these two entry points too, so
// they can exercise production impulses without copying presentation constants.
static void recoil_vm_impulse(vm_t *vm, int src, int shot, int wpn, float ads) {
  const recoil_pose_t *rp = recoil_pose(wpn);
  float brace = recoil_brace(rp, ads);
  float side = recoil_side_sample(src, shot, wpn);
  vm->kick_back_v += rp->vm_back_v * brace;
  vm->kick_lift_v += rp->vm_lift_v * brace;
  vm->kick_side_v += rp->vm_side_v * brace * side;
}

static void recoil_spring(float *value, float *velocity, float k, float c) {
  // Idle figures dominate a normal frame. Preserve the exact zero fixed point
  // without paying three spring integrations per figure and tick.
  if (*value == 0.0f && *velocity == 0.0f) return;
  *velocity += (-k * *value - c * *velocity) * TICK_DT;
  *value += *velocity * TICK_DT;
}

// One weapon-aware human/weapon relationship for both views. Vectors use
// {gun-right, gun-up, gun-forward}; their metres are authored at unit model scale and
// every consumer supplies its scale explicitly. Angles and pole directions do not
// scale. GUN_HOLD remains the sole source for the actual grip cylinders -- these values
// describe the wrists and arms around those contacts, never a second weapon surface.
typedef struct {
  float carry_down, carry_down_run;
  float carry_cross, carry_cross_run;
  v3 butt_pin;
  float carry_axial, carry_right, carry_up, carry_body_forward;
  v3 trigger_wrist;
  v3 trigger_wrist_idle_delta, trigger_wrist_slide_delta;
  v3 support_wrist_ready, support_wrist_delta;
  v3 bolt_wrist;
  v3 reload_route;
  v3 trigger_pole, support_pole;
  v3 trigger_pole_ads_delta, trigger_pole_idle_delta, trigger_pole_slide_delta;
  gun_hand_pose_t fp_hand[2];
  // Relative FP low-ready composition: camera right/up/forward metres followed by
  // pitch/yaw/roll radians in the vm_stage_springs rotation convention. It is read
  // only through the shared ready blend and is therefore exactly zero in ADS/busy.
  v3 fp_low_pos, fp_low_ang;
} gun_pose_desc_t;

#define GPOSE_M(v) ((v) * (1.0f / GUN_GIRTH_3P))
static const gun_pose_desc_t GUN_POSE[WPN_COUNT] = {
  [WPN_AR] = {
    .carry_down = 0.60f, .carry_down_run = 0.22f,
    .carry_cross = 0.50f, .carry_cross_run = 0.10f,
    // These exact inverse-scale floats make the explicit x1.05 round trip reproduce
    // the authored 3P AR coefficients bit-identically.
    .butt_pin = {{0.0f, GPOSE_M(-0.050f), -0x1.c86954p-3f}},
    .carry_axial = GPOSE_M(0.040f), .carry_right = GPOSE_M(0.165f),
    .carry_up = GPOSE_M(0.070f), .carry_body_forward = GPOSE_M(0.065f),
    .trigger_wrist = {{GPOSE_M(0.029f), GPOSE_M(-0.137f), GPOSE_M(-0.080f)}},
    .trigger_wrist_slide_delta = {{0.0f, GPOSE_M(0.022f), 0.0f}},
    .support_wrist_ready = {{GPOSE_M(-0.075f), GPOSE_M(-0.073f), GPOSE_M(0.190f)}},
    .support_wrist_delta = {{0.0f, GPOSE_M(0.050f), GPOSE_M(0.060f)}},
    .reload_route = {{-0.086f, 0.0f, 0.020f}},
    .trigger_pole = {{1.0f, -1.6f, 0.8f}},
    .trigger_pole_ads_delta = {{0.30f, 1.30f, -0.98f}},
    .trigger_pole_idle_delta = {{0.0f, 0.0f, -1.0f}},
    .support_pole = {{-0.60f, -0.95f, -0.10f}},
    .fp_hand = {
      [0] = { // firing hand; the renderer's named enum is defined later
        .bearing_hip = {{0.34f, 0.16f, -0.92f}},
        .bearing_ads = {{0.34f, 0.16f, -0.92f}},
        .bearing_reload = {{0.34f, 0.16f, -0.92f}},
        .bearing_bolt = {{0.34f, 0.16f, -0.92f}},
        .axial_hip = -0.048f, .axial_ads = -0.048f,
        .lift_hip = 0.070f, .lift_ads = 0.070f,
        .ext_hip = 20.0f * DEG2RAD, .ext_ads = 20.0f * DEG2RAD,
        .dev_hip = 15.0f * DEG2RAD, .dev_ads = 15.0f * DEG2RAD,
        .bias_hip = -0.16f, .bias_ads = -0.16f,
      },
      [1] = { // support hand
        .bearing_hip = {{-0.55f, -0.84f, 0.0f}},
        .bearing_ads = {{-0.55f, -0.84f, 0.0f}},
        .bearing_reload = {{-0.82f, 0.0f, 0.57f}},
        .bearing_bolt = {{-0.55f, -0.84f, 0.0f}},
        .axial_hip = -0.046f, .axial_ads = -0.046f,
        .lift_hip = 0.066f, .lift_ads = 0.066f,
        .reload_axial = 0.010f, .reload_lift = 0.004f,
        .ext_hip = 10.0f * DEG2RAD, .ext_ads = 10.0f * DEG2RAD,
        .dev_hip = -6.0f * DEG2RAD, .dev_ads = -6.0f * DEG2RAD,
        .bias_hip = -0.30f, .bias_ads = -0.30f, .bias_reload = -0.60f,
      },
    },
  },
  [WPN_SR] = {
    // The long rifle stays close to the shoulder pocket at a calm 20-degree
    // depression and 15-degree cross-body angle.
    .carry_down = 20.0f * DEG2RAD, .carry_down_run = 7.5f * DEG2RAD,
    .carry_cross = 15.0f * DEG2RAD, .carry_cross_run = 3.0f * DEG2RAD,
    .butt_pin = {{0.0f, GPOSE_M(-0.070f), GPOSE_M(-0.234f)}},
    .carry_axial = GPOSE_M(0.040f), .carry_right = GPOSE_M(0.165f),
    .carry_up = GPOSE_M(0.070f), .carry_body_forward = GPOSE_M(0.065f),
    // The SR grip and fore-end sit lower/rearward than the AR's. These bearings keep
    // the same palm relationship while giving the two elbow solves their own poles.
    .trigger_wrist = {{GPOSE_M(0.029f), GPOSE_M(-0.144f), GPOSE_M(-0.054f)}},
    .trigger_wrist_idle_delta = {{0.0f, 0.0f, GPOSE_M(-0.012f)}},
    .trigger_wrist_slide_delta = {{0.0f, GPOSE_M(0.064f), GPOSE_M(-0.006f)}},
    .support_wrist_ready = {{GPOSE_M(-0.075f), GPOSE_M(-0.099f), GPOSE_M(0.181f)}},
    .support_wrist_delta = {{0.0f, GPOSE_M(0.050f), 0x1.d41d42p-6f}},
    .bolt_wrist = {{GPOSE_M(0.090f), GPOSE_M(0.060f), GPOSE_M(0.030f)}},
    .reload_route = {{-0.086f, 0.0f, 0.020f}},
    .trigger_pole = {{-3.4f, -2.5f, -0.7f}},
    .trigger_pole_ads_delta = {{4.70f, 2.20f, 0.52f}},
    .trigger_pole_idle_delta = {{6.0f, 0.0f, -2.0f}},
    .trigger_pole_slide_delta = {{8.0f, 3.0f, 0.0f}},
    .support_pole = {{-0.78f, -0.62f, -0.12f}},
    .fp_hand = {
      [0] = { // firing hand; the renderer's named enum is defined later
        .bearing_hip = {{0.34f, 0.16f, -0.92f}},
        .bearing_ads = {{0.34f, 0.16f, -0.92f}},
        .bearing_reload = {{0.34f, 0.16f, -0.92f}},
        .bearing_bolt = {{0.0f, 0.75f, 0.66f}},
        .axial_hip = -0.048f, .axial_ads = -0.048f,
        .bolt_axial = 0.118f,
        .lift_hip = 0.070f, .lift_ads = 0.070f,
        .ext_hip = 22.0f * DEG2RAD, .ext_ads = 22.0f * DEG2RAD,
        .dev_hip = 12.0f * DEG2RAD, .dev_ads = 12.0f * DEG2RAD,
        .bias_hip = -0.18f, .bias_ads = -0.18f,
      },
      [1] = { // support hand
        .bearing_hip = {{-0.55f, -0.84f, 0.0f}},
        .bearing_ads = {{-0.55f, -0.84f, 0.0f}},
        .bearing_reload = {{-0.82f, 0.0f, 0.57f}},
        .bearing_bolt = {{-0.55f, -0.84f, 0.0f}},
        .axial_hip = -0.046f, .axial_ads = -0.046f,
        .lift_hip = 0.066f, .lift_ads = 0.066f,
        .reload_axial = 0.010f, .reload_lift = 0.004f,
        .ext_hip = 14.0f * DEG2RAD, .ext_ads = 14.0f * DEG2RAD,
        .dev_hip = -8.0f * DEG2RAD, .dev_ads = -8.0f * DEG2RAD,
        .bias_hip = -0.34f, .bias_ads = -0.34f, .bias_reload = -0.60f,
      },
    },
    // The SR FP low-ready composition is a deliberately small relative offset, not
    // the 20/15-degree third-person carriage.
    .fp_low_pos = {{-0.006f, -0.010f, -0.004f}},
    .fp_low_ang = {{-3.0f * DEG2RAD, 1.5f * DEG2RAD, -1.0f * DEG2RAD}},
  },
};
#undef GPOSE_M

static const gun_pose_desc_t *gun_pose_desc(int wpn) {
  return &GUN_POSE[wpn >= 0 && wpn < WPN_COUNT ? wpn : WPN_AR];
}

static v3 gun_pose_offset(v3 right, v3 up, v3 forward, v3 authored, float scale) {
  return v3_add(v3_scale(forward, authored.z * scale),
         v3_add(v3_scale(up, authored.y * scale),
                v3_scale(right, authored.x * scale)));
}

// One C1 local-space detour for FP contact, 3P contact and the 3P wrist: zero value
// and slope at either live grip, with a flat maximum through the detached travel.
static v3 gun_reload_route_offset(const gun_pose_desc_t *erg,
                                  v3 right, v3 up, v3 forward,
                                  float hand01, float scale) {
  float edge = fminf(hand01, 1.0f - hand01);
  float route = smooth01(f_clamp(edge / 0.10f, 0.0f, 1.0f));
  return gun_pose_offset(right, up, forward, erg->reload_route, scale * route);
}

typedef struct {
  // world-space joint positions first (a death topple rotates these around
  // the ground point)...
  v3 hip[2], knee[2], ankle[2], toe[2];  // legs, 0 = left / 1 = right
  v3 sho[2], elb[2], wri[2];             // arms
  v3 pelvis, chest, neck, head, gun_o;
  // ...then pure directions (a topple only rotates these)
  v3 right, head_f, gun_f, gun_u;
  v3 ant_sway;   // antenna whip: local tip offset from a 2nd-order follower
} pose_t;
#define POSE_NPOS 19  // leading v3 members that are positions

// TWO EXTENTS, BECAUSE THEY ANSWER TWO QUESTIONS. "How far can this part poke into a
// wall" and "how high does it hold the body off the floor" are different numbers,
// and using one for both is why raising the wall margins made settled corpses FLOAT
// (see `ragx`'s `float` column).
//   POSE_RAD   the WORST reach in any direction — a helmet is 190 mm from the head
//              node along the spine, which is HORIZONTAL once the body is down. The
//              wall clamp and push-out want this; too small and geometry ends up
//              inside a solid.
//   POSE_HALF  the mesh's HALF-THICKNESS about the joint, i.e. what actually rests on
//              the floor. The settle and floor guard want this; too large and the
//              body hovers by the excess.
static const float POSE_RAD[POSE_NPOS] = {
  0.115f, 0.115f,   // hip
  0.105f, 0.105f,   // knee — the joint ball plus the thigh and shin tapers
  0.100f, 0.100f,   // ankle — the boot hangs 85 mm below it and 195 forward
  0.075f, 0.075f,   // toe
  0.115f, 0.115f,   // shoulder — deltoid pad
  0.090f, 0.090f,   // elbow
  0.095f, 0.095f,   // wrist — the glove, not the joint
  0.150f,           // pelvis
  0.255f,           // chest — carrier in front, PACK AND STRAPS BEHIND, and it
                    // is the pack that sets this: at 0.210 a body lying on its
                    // back had 69 mm of `packstrap` inside the platform it was
                    // lying on, for its whole life
  0.070f,           // neck
  0.190f,           // head — the helmet shell stands 120 mm past the node along
                    // the body's own spine, which is HORIZONTAL once it is down
  0.260f,           // gun anchor. A rifle is 900 mm long and no radius describes
                    // it; this keeps the receiver and magazine out of a wall and
                    // lets the barrel graze one, which is the right trade — the
                    // alternative is shoving the whole corpse a rifle-length off
                    // every piece of cover it dies against.
};
// Measured against `figm`, not authored: the boot's sole sits 76 mm under the ankle
// node (`outsole y0 = -0.9` with ANIM_ANKLE_Y 0.075) but only half a sole wide on its
// side, the glove is a ~45 mm ball on the wrist, and the PACK is the deepest thing on
// the chest — a body on its back rests on it.
static const float POSE_HALF[POSE_NPOS] = {
  0.090f, 0.090f,   // hip
  0.072f, 0.072f,   // knee
  0.056f, 0.056f,   // ankle
  0.026f, 0.026f,   // toe
  0.090f, 0.090f,   // shoulder
  0.052f, 0.052f,   // elbow
  0.048f, 0.048f,   // wrist — the glove
  0.150f,           // pelvis
  0.255f,           // chest — the SAME as POSE_RAD, and measured that way: the
                    // pack really does stand this far off the spine, so a body on its
                    // back rests on it.
  0.055f,           // neck
  0.120f,           // head — the helmet shell, not its crown
  0.010f,           // gun anchor: laid, not rested
};

// What a corpse is doing this frame: limb slack, topple angle and axis, last impact
// hardness, and spin about its own vertical.
typedef struct { float slack, tilt, slam; v3 axis; float yaw, swing; } death_t;

typedef struct {
  v3    vel;                  // linear velocity
  v3    axis;                 // horizontal axis the body topples about
  float tilt, prev_tilt;      // accumulated topple angle (rad) — UNBOUNDED in
                              // the air, which is what lets a body cartwheel
  float spin;                 // ...and its rate (rad/s)
  float slam, prev_slam;      // impact crumple spike, decays
  float yaw, prev_yaw, yawv;  // spin about the body's OWN vertical, and its rate
  v3    push;                 // horizontal launch still owed, per tick. THE
                              // ORDER OF A DEATH IS ROTATE, THEN GO: applied whole on
                              // the killing tick the body had already translated 410 mm
                              // before it had rotated 10 degrees — it moonwalks away
                              // from the shot still standing upright, which is the
                              // single loudest "this is not a body" tell in the whole
                              // death.
  int   push_t;               // ticks of it left
  float rest_off;             // per-kill offset on the attitude the body comes
                              // to rest in.
  float lag, prev_lag;        // a smoothed `tilt`. The DIFFERENCE tilt-lag is
                              // how far the limbs trail the trunk, which is the
                              // only articulation a rigidly-rotated pose can
                              // have and the whole reason a flung body reads as
                              // a ragdoll rather than a mannequin on a spit.
  int   grounded;
} rag_t;

// Ground sparks off a SLIDING DEAD BODY. Declared here because rag_advance — the one
// place both ragdoll owners integrate — sits far above the particle pool.
static void vfx_rag_sparks(const rag_t *r, v3 pos);
// (the integrator itself, `rag_step`, lives with the death constants below — it needs
// ent_move, the arena clamp and every DEATH_* number.
_Static_assert(sizeof(pose_t) == 24 * sizeof(v3), "pose_t must stay a flat v3 array");

typedef struct {
  v3    pos, from, to;  // world ankle: current, swing start, swing goal
  float yaw;            // heading the foot was last planted with
  float t;              // swing progress, >= 1 = planted
} afoot_t;

typedef struct {
  v3      vel_s;             // smoothed world velocity
  float   spd;               // smoothed horizontal speed
  float   phase;             // gait cycle: L swings [0,DUTY), R [.5,.5+DUTY)
  float   leg_yaw;           // hip heading; trails the aim yaw
  float   crouch_s, slide_s, air_s;  // eased pose blends 0..1, S-SHAPED
  float   crouch_lin, slide_lin, air_lin;  // ...and the plain first-order eases
                             // driving them; see anim_tick for why both exist
  float   slide_t;           // seconds since the slide began (entry envelope)
  float   slide_v;           // eased slide speed factor 0..1.2 (recline depth)
  int     was_slide;
  float   land;              // landing dip impulse, decays
  float   hdown;             // hard-landing hand-to-ground reach, decays slower
  // ...and the IMPULSES those two chase.
  float   land_i, hdown_i;
  float   fall;              // most negative vel.y of the current air time
  float   brace;             // 0..1 preparation for a landing that is coming
  float   lean_f, lean_r;    // fwd/right acceleration lean (rad)
  float   fire_k;            // shot/carry-floor latch, decays; not recoil distance
  // Third-person shoulder response. prev_pose, rather than duplicate prev scalars,
  // interpolates the solved gun and hands for rendering.
  float   recoil_back, recoil_back_v;  // m toward the shoulder
  float   recoil_lift, recoil_lift_v;  // rad of muzzle rise
  float   recoil_side, recoil_side_v;  // m, signed in gun-right
  float   hit_k;             // TAKING a round: body flinch impulse, decays
  float   hit_s;             // ...and what the POSE sees, chasing it: assigned
                             // outright the impulse stepped the chest 42 mm and
                             // the head 23.5 mm in ONE tick, which is the same
                             // defect land_i/land and bob_lag both exist for
  v3      hit_d;             // unit horizontal direction that round travelled
  // WHERE the round landed, in the victim's own frame, latched with the impulse.
  float   hit_y;             // impact height / HEIGHT_STAND, 0 boots .. 1 crown
  float   hit_x;             // lateral offset in the body's own right axis,
                             // -1 = full left, +1 = full right (0.26 m out)
  float   hit_w;             // wobble phase, ticks — the "zittern" after the
                             // fold: one damped oscillation, not a second decay
  v3      slide_dir;         // unit horizontal slide direction (travel)
  v3      slide_pd;          // pose direction: travel mirrored into the front
                             // hemisphere — legs always lead where knees bend
  float   slide_bk;          // 0 = forward/lateral slide, 1 = full butt-slide
  float   slide_ld;          // 0 = the LEFT leg leads the skid, 1 = the right.
                             // A BLEND: a slide aimed dead ahead sits exactly on
                             // the boundary and a boolean flips there
  int     was_grounded;
  float   idle_t;            // always-advancing clock for idle breathing
  float   bwd;               // eased 0..1 how much of the travel is backward
  float   wshift;            // eased single-support weight shift, -1 L .. +1 R
  float   sink;              // eased hip drop that keeps stance feet planted
  // The gait clock is allowed to TELEPORT: when a stance foot is stranded the
  // controller adopts the cycle mid-window by assigning `phase` outright.
  float   bob_lag;           // phase the visual bob still owes the gait clock
  // The same rule one level up: the ENTITY ORIGIN is allowed to teleport, the BODY is
  // not.
  float   root_off;          // m of base.y that is tuck, not ground
  // Last tick's baked tuck.
  float   tuck_p;            // m of tuck the caller baked LAST tick
  // SPINE COMPLIANCE. The upper body has mass and the neck is not a weld;
  // measured on the pre-patch build, head.y - pelvis.y varied 3.98 mm across a
  // whole jump whose pelvis takes +864 m/s^2 at take-off and +758 at touchdown
  // (max |v_head - v_pelvis| 0.054 m/s over 400 ticks). See anim_tick.
  float   spc, spc_v;        // upper-body vertical give (m) and its rate
  float   spc_py, spc_pv;    // last tick's solved pelvis y, and its velocity
  // A FROZEN POSE IS NOT A MOVING ONE, and only the solver knows the difference.
  // player_tick stops calling anim_tick the moment the player dies (it just copies
  // prev_pose), so the whole state — slide_s, vel_s, the wrist on the floor — freezes
  // at its death-tick values and stays there. vfx_skid read that as a live 21 m/s skid
  // and ran a spark fountain nailed to the corpse for the death screen and through the
  // respawn.
  long    live_t;            // g_tick this pose was last solved on
  float   yaw_vel;           // smoothed aim-yaw rate — the head leads a turn
  float   vy;                // smoothed vertical velocity — stages the air pose
  v3      ant_p, ant_v;      // antenna follower: offset + velocity (2nd order)
  float   yaw_prev;          // last tick's aim yaw for the rate estimate
  float   ads_s;             // eased shouldered-weapon blend, 0 = hip, 1 = aimed
  float   ready_s, prev_ready_s; // eased carry state and render-interpolation twin:
                                 // 1 = stock in shoulder, 0 = low-ready
  float   lean_s;            // eased lateral body lean, -1 = left .. +1 = right
  v3      lean_step_off;     // presentation ankle offset; never written to afoot_t
  float   lean_step_k;       // signed achieved lean: -left .. +right
  float   stance[2];         // 0..1 progress through the current foot contact
  float   fpitch[2];         // eased ankle pitch (+ = toe up), keeps the heel
                             // strike continuous across the swing->stance seam
  afoot_t foot[2];
  pose_t  pose, prev_pose;   // solved joints, cur + prev tick for interpolation
} anim_t;

// Last-solve diagnostics plus a monotonic call counter. anim_tick writes all of these
// from the same clamp result that drives the pose; skel_solve and render never scan.
static long  g_lean_step_scans;
static v3    g_lean_step_want_off, g_lean_step_got_off;
static float g_lean_step_ratio;
static int   g_lean_step_side = -1;

static void recoil_body_impulse(anim_t *a, int src, int shot, int wpn, float ads) {
  const recoil_pose_t *rp = recoil_pose(wpn);
  float brace = recoil_brace(rp, ads);
  float side = recoil_side_sample(src, shot, wpn);
  a->recoil_back_v += rp->body_back_v * brace;
  a->recoil_lift_v += rp->body_lift_v * brace;
  a->recoil_side_v += rp->body_side_v * brace * side;
}

// A low-ready lobe is allowed only by real state, never by a transient gun-animation
// curve. The helper is pure so server, prediction, remote presentation and proofs make
// the same decision. A plain ready intent is deliberately NOT a hard gate: it is the
// target of ready_s and must remain visible as the fast raise transition. Cooldown
// includes the SR bolt cycle; fire_k covers the immediate carry floor before that
// clock can be observed by a viewer.
static int gun_pose_idle_ok(const weapon_t *w, const anim_t *a, v3 vel,
                            int grounded, int slide) {
  int cur = w->cur >= 0 && w->cur < WPN_COUNT ? w->cur : WPN_AR;
  float hspd2 = vel.x * vel.x + vel.z * vel.z;
  return grounded && !slide && hspd2 <= 0.20f * 0.20f &&
         !w->ads_want && w->ads_s <= 0.01f &&
         w->reload_t == 0 && w->switch_t == 0 && w->cool[cur] == 0 &&
         a->fire_k <= 0.01f;
}


// Clamp a foot target so it never pokes into world geometry: cast from the body center
// at the target's height toward it and stop short of the first solid.
static v3 foot_clamp_m(v3 base, v3 tgt, float margin) {
  v3 o = {{base.x, tgt.y, base.z}};
  v3 d = {{tgt.x - o.x, 0, tgt.z - o.z}};
  float l = sqrtf(d.x * d.x + d.z * d.z);
  if (l < 1e-4f) return tgt;
  d.x /= l;
  d.z /= l;
  float best = l;
  // Prefilter before the full slab test: the ray is horizontal and only a hit inside
  // [0, l + margin) can lower `best`, so a solid that misses the segment's padded XZ
  // bounds — or the ray's own y plane — cannot change the result.
  float reach = l + margin;
  float bx0 = (d.x > 0 ? o.x : o.x + d.x * reach) - 1e-3f;
  float bx1 = (d.x > 0 ? o.x + d.x * reach : o.x) + 1e-3f;
  float bz0 = (d.z > 0 ? o.z : o.z + d.z * reach) - 1e-3f;
  float bz1 = (d.z > 0 ? o.z + d.z * reach : o.z) + 1e-3f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (o.y < s->min.y - 1e-3f || o.y > s->max.y + 1e-3f) continue;
    if (bx1 < s->min.x || bx0 > s->max.x || bz1 < s->min.z || bz0 > s->max.z)
      continue;
    v3 n;
    float t = ray_aabb(o, d, s->min, s->max, &n);
    if (t > 0 && t - margin < best) best = t - margin;
  }
  if (best < 0) best = 0;
  if (best < l) {
    tgt.x = o.x + d.x * best;
    tgt.z = o.z + d.z * best;
  }
  return tgt;
}

static v3 foot_clamp_world(v3 base, v3 tgt) { return foot_clamp_m(base, tgt, 0.06f); }

typedef struct {
  v3 p;
  int escaped;
} lean_foot_sweep_t;

// Sweep the ACTUAL rendered ankle chord, not another radial proxy from the body.
// The renderer linearly interpolates prev_pose -> pose; consequently this segment is
// the only segment whose clearance can make that interpolation a construction
// guarantee. A synthetic proof may introduce a wall around the previously rendered
// endpoint. In that discontinuous case `escaped` returns the first point outside the
// union of the margin-expanded boxes and the caller snaps to it; ordinary/static
// geometry starts outside and retains the motion ease. One call is one solid traversal.
static lean_foot_sweep_t lean_foot_sweep(v3 from, v3 to, float margin) {
  lean_foot_sweep_t out = {.p = to, .escaped = 0};
  v3 o = from;
  v3 d = v3_sub(to, from);
  float len = v3_len(d);
  g_lean_step_scans++;
  if (len < 1e-6f) return out;
  d = v3_scale(d, 1.0f / len);

  // Intervals are collected during the single world traversal, then merged in local
  // storage.  This also handles abutting boxes without rescanning the world.
  float lo[MAX_SOLIDS], hi[MAX_SOLIDS];
  int n = 0;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    float mn[3] = {s->min.x - margin, s->min.y - 1e-3f,
                   s->min.z - margin};
    float mx[3] = {s->max.x + margin, s->max.y + 1e-3f,
                   s->max.z + margin};
    float org[3] = {o.x, o.y, o.z}, dir[3] = {d.x, d.y, d.z};
    float a = -1e30f, b = 1e30f;
    int hit = 1;
    for (int k = 0; k < 3; k++) {
      if (fabsf(dir[k]) < 1e-8f) {
        if (org[k] < mn[k] || org[k] > mx[k]) { hit = 0; break; }
      } else {
        float ta = (mn[k] - org[k]) / dir[k];
        float tb = (mx[k] - org[k]) / dir[k];
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        if (ta > a) a = ta;
        if (tb < b) b = tb;
        if (a > b) { hit = 0; break; }
      }
    }
    if (!hit || b < 0.0f || a > len) continue;
    lo[n] = a;
    hi[n] = b;
    n++;
  }
  for (int i = 1; i < n; i++) {
    float al = lo[i], ah = hi[i];
    int j = i;
    while (j > 0 && lo[j - 1] > al) {
      lo[j] = lo[j - 1];
      hi[j] = hi[j - 1];
      j--;
    }
    lo[j] = al;
    hi[j] = ah;
  }

  const float eps = 1e-4f;
  if (n > 0 && lo[0] <= 0.0f && hi[0] >= 0.0f) {
    float leave = hi[0];
    for (int i = 1; i < n && lo[i] <= leave + eps; i++)
      if (hi[i] > leave) leave = hi[i];
    if (leave + eps <= len) {
      out.p = v3_add(o, v3_scale(d, leave + eps));
      out.escaped = 1;
    }
    return out;
  }
  if (n > 0 && lo[0] <= len) {
    float stop = f_clamp(lo[0] - eps, 0.0f, len);
    out.p = v3_add(o, v3_scale(d, stop));
  }
  return out;
}

// Distance from `p` straight down to the first surface under it, capped at `maxd`.
static float ground_drop(v3 p, float maxd) {
  float best = p.y >= 0.0f && p.y < maxd ? p.y : maxd;   // the arena floor
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (p.x < s->min.x || p.x > s->max.x || p.z < s->min.z || p.z > s->max.z) continue;
    float d = p.y - s->max.y;
    if (d >= -1e-3f && d < best) best = d;
  }
  return best < 0.0f ? 0.0f : best;
}

// Two-bone IK: returns the middle joint between root a and target t for segment lengths
// l1/l2; the bend goes toward `pole` (projected perpendicular to the a->t axis).
static v3 ik_solve(v3 a, v3 t, float l1, float l2, v3 pole) {
  v3 d = v3_sub(t, a);
  float dl = f_clamp(v3_len(d), fabsf(l1 - l2) + 0.01f, l1 + l2 - 0.008f);
  d = v3_norm(d);
  v3 side = v3_sub(pole, v3_scale(d, v3_dot(pole, d)));
  if (v3_dot(side, side) < 1e-8f) side = v3_cross(d, (v3){{0, 1, 0}});
  if (v3_dot(side, side) < 1e-8f) side = (v3){{1, 0, 0}};
  side = v3_norm(side);
  float cosa = f_clamp((l1 * l1 + dl * dl - l2 * l2) / (2.0f * l1 * dl), -1.0f, 1.0f);
  float proj = l1 * cosa;
  float h = sqrtf(fmaxf(0.0f, l1 * l1 - proj * proj));
  return v3_add(a, v3_add(v3_scale(d, proj), v3_scale(side, h)));
}

// Off hand reaching for the ground (slide brace / hard-landing catch). The caller
// supplies the same solved shoulder the arm renderer consumes; using a second rolled
// approximation here made the wrist reachable from one shoulder and clamped from a
// different one.
static v3 brace_hand(v3 shoulder, v3 want, v3 wri, float k) {
  v3 bd = v3_sub(want, shoulder);
  float bl = v3_len(bd), reach = (ANIM_UARM + ANIM_FARM) * 0.97f;
  if (bl > reach) want = v3_add(shoulder, v3_scale(bd, reach / bl));
  return v3_lerp(wri, want, k);
}

// Duty = the fraction of the gait cycle a foot spends on the ground; it rises with
// speed past 0.5, which is where the FLIGHT time of a run comes from.
static float anim_hz(const anim_t *a) {
  float hz = a->spd <= 5.0f
                 ? 0.95f * powf(fmaxf(a->spd, 0.5f) * (1.0f / 1.4f), 0.33f)
                 : 1.45f * powf(a->spd * 0.2f, 0.50f);
  // Backpedalling steps quicker than walking, but only slightly.
  return f_clamp(hz, 0.95f, 2.30f) *
         (1.0f + 0.22f * a->bwd + 0.22f * a->crouch_s);
}
// Horizontal reach budget for a plant: how far from the hip a foot may be placed and
// still be reachable when the hip bobs to its highest.
static float anim_runT(const anim_t *a) {
  return f_clamp(a->spd / ANIM_RUN_REF, 0.0f, 1.0f);
}
static float anim_hmax(const anim_t *a) {
  float runT = anim_runT(a);
  float hipy = ANIM_HIP_Y + (ANIM_HIP_CR - ANIM_HIP_Y) * a->crouch_s -
               0.06f * runT * (1.0f - 0.65f * a->bwd) * (1.0f - a->crouch_s) -
               ANIM_HIP_BOB * 0.5f;
  float mxr = ANIM_LEG * ANIM_REACH_K;
  float drop = hipy - ANIM_ANKLE_Y;
  return sqrtf(fmaxf(0.04f, mxr * mxr - drop * drop));
}
static float anim_duty(const anim_t *a) {
  float d = ANIM_DUTY + (ANIM_DUTY_RUN - ANIM_DUTY) *
            f_clamp(a->spd / ANIM_DUTY_REF, 0.0f, 1.0f);
  // A STANCE CAN ONLY LAST AS LONG AS THE LEG CAN SPAN. The foot is frozen in the world
  // for (1-duty)/hz seconds, so the hip travels spd*(1-duty)/hz over it and the leg can
  // only cover 2*hmax.
  d = d + (fminf(d, 0.50f) - d) * a->bwd;
  float hz = anim_hz(a);
  if (a->spd > 0.5f && hz > 1e-3f) {
    // THE PLANT DOES NOT STRADDLE THE BODY EVENLY: `ahead_t` puts it just over half a
    // stance ahead, so the REAR half is the long one, and it — not the mean — has to
    // fit inside the leg.
    float rear = 1.0f - 0.5f * (1.0f - 0.18f * a->bwd) * (1.0f - 0.18f * a->crouch_s);
    float need = 1.0f - (anim_hmax(a) * ANIM_SPAN_K * hz) / (rear * a->spd);
    if (need > d) d = fminf(need, ANIM_DUTY_MAX);
  }
  return d;
}
enum { SKEL_POSE_ONLY, SKEL_ADVANCE_STATE };

// Solve the full pose from the current animation state. base = feet-center
// world position (the entity's physics origin), yaw/pitch = aim. Only the one
// tick/reset owner may advance persistent followers; event anchors, renderer
// diagnostics and counterfactual proofs re-solve the existing instant without
// integrating sink, stance progress or ankle pitch a second time.
static void skel_solve(anim_t *a, v3 base, float yaw, float pitch,
                       int wpn, int idle_ok, int advance_state) {
  const gun_pose_desc_t *erg = gun_pose_desc(wpn);
  const float pose_scale = GUN_GIRTH_3P;
  pose_t *P = &a->pose;
  float ly = a->leg_yaw;
  v3 R = {{cosf(ly), 0, sinf(ly)}};
  v3 F = {{sinf(ly), 0, -cosf(ly)}};
  v3 U = {{0, 1, 0}};
  float runT = anim_runT(a);
  float ground = (1.0f - a->air_s) * (1.0f - a->slide_s);

  // Pelvis: height blends stand/crouch/slide, bobs twice per gait cycle,
  // dips on landings, shifts laterally over the stance foot.
  float movT = f_clamp(a->spd / 0.6f, 0.0f, 1.0f);
  // Airborne, crouch means TUCK: the legs come up, the torso stays on its ballistic
  // arc.
  float hip_h = ANIM_HIP_Y + (ANIM_HIP_CR - ANIM_HIP_Y) * a->crouch_s *
                (1.0f - 0.90f * a->air_s);
  // Standing still the knees nearly straighten (the lowered gait hip reads as
  // sitting on an invisible stool when idle); moving takes the athletic flex.
  hip_h += 0.024f * (1.0f - movT) * ground * (1.0f - a->crouch_s) *
           (1.0f - a->slide_s);
  hip_h -= 0.06f * runT * (1.0f - 0.65f * a->bwd) * (1.0f - a->crouch_s);  // spring-mass
  hip_h -= 0.016f * a->bwd * (1.0f - a->crouch_s);  // backpedal stays coiled
  // Pelvis rise/fall, phased to the GAIT and not to an arbitrary offset: highest as the
  // body passes over the planted foot (knee straightens, cheapest posture to hold),
  // lowest at the stride extremes (where the leg needs the horizontal reach).
  float bph = a->phase - a->bob_lag;
  bph -= floorf(bph);
  float bob_drop = 0.0f;  // how far the gait bob has pulled the pelvis DOWN
  {
    float duty = anim_duty(a);
    float phm = duty + 0.5f * (1.0f - duty);  // phase of left-foot mid-stance
    // Human CoM travel is ~2.7-4.8 cm peak-to-peak walking and 6-9 cm running,
    // so the amplitude scales with speed rather than being one constant.
    float latk = a->spd > 0.5f
                     ? fabsf(a->vel_s.x * R.x + a->vel_s.z * R.z) / a->spd
                     : 0.0f;
    // Backward there is no heel-strike vault to ride over — the contact is a ball
    // strike and the body rolls back across the foot — so the CoM stays flatter than
    // walking forward at the same speed.
    float bobA = (0.014f + 0.016f * runT) * movT * ground *
                 (1.0f - 0.55f * a->crouch_s) * (1.0f - 0.40f * latk) *
                 (1.0f - 0.35f * a->bwd);
    // WALKING IS A PENDULUM, RUNNING IS A SPRING — nearly opposite phase.
    bob_drop = bobA * (1.0f - cosf(12.566371f * (bph - phm - 0.25f * runT)));
    hip_h -= bob_drop;
  }
  hip_h += (ANIM_HIP_SL - hip_h) * a->slide_s;
  hip_h -= expf(-9.0f * a->slide_t) * 0.14f * a->slide_s;  // violent 100-150 ms drop-in
  // Exit beats 2+3: as the skid bleeds out the pelvis rises over the
  // gathering trail leg — mid and late slide must not photograph alike.
  float sl_exit = a->slide_s * (1.0f - f_clamp(a->slide_v, 0.0f, 1.0f));
  hip_h += 0.09f * sl_exit;
  // A landing is absorbed THROUGH THE KNEES, so a body that is still folded from an air
  // tuck has none of that travel left to spend: stacking the full dip on an unpaid
  // root_off is what put the pelvis 24-76 mm BELOW the ankles when the touchdown clamp
  // above was replaced by a decay.
  { float knee = 1.0f - f_clamp(a->root_off * (1.0f / (HEIGHT_STAND - HEIGHT_CROUCH)),
                                0.0f, 1.0f);
    hip_h -= a->land * 0.16f * knee;
    hip_h -= a->hdown * 0.26f * knee; }  // hard landing: absorb through the knees
  // Idle life: a standing soldier is never still.
  float still = (1.0f - movT) * ground * (1.0f - a->slide_s);
  hip_h += sinf(a->idle_t * 1.60f) * 0.0055f * still;
  // Measured pelvis lateral travel was 72 mm peak-to-peak at a 1.4 m/s walk and 38 mm
  // at 8 m/s, against a human ~40 and ~20 — roughly 1.8x at every speed, which reads as
  // a waddle.
  float shift = sinf(bph * F_TAU) * 0.024f * (1.0f - 0.55f * runT) *
                ground * movT;
  shift += sinf(a->idle_t * 0.55f) * 0.016f * still;  // idle weight shift
  // ...and the weight goes over the foot that is actually carrying it.
  shift += a->wshift * 0.055f * still;
  // ...and the other half of that gate: the sim ALSO folds the air tuck into base.y
  // (anim_t.root_off), so the untucked plane the body actually hangs off is base.y -
  // root_off.
  v3 root = base;
  root.y -= a->root_off;
  float hitf = a->hit_d.x * F.x + a->hit_d.z * F.z;
  float hitr = a->hit_d.x * R.x + a->hit_d.z * R.z;
  // WHERE the round landed, applied on top of which way it was going.
  //   hs_   the fold, RINGING. A pure decay reads as a shove; a body that is hit
  //         jolts, overshoots once and rings out. A clock rather than a second
  //         spring, so the ring has the same shape whatever the impulse was.
  //   hlev  the LEVER the round had about the waist. A helmet is 1.0 m above the hips
  //         and a thigh 0.3 m below, so the same round pitches the trunk one way high
  //         on the body and the other way low — "he rocks back" against "his legs go".
  //   hrol  the impact's offset off the centre line, rolling the shoulder line away
  //         from it. Without it a left shoulder, a right shoulder and a sternum were
  //         one animation to the millimetre.
  float hs_ = a->hit_s * (1.0f + 0.38f * sinf(a->hit_w * 0.62f) *
                                 expf(-a->hit_w * 0.075f));
  // 0.55, not 0.42: the pivot is the WAIST, and on a 1.79 m figure the waist is at 1.00
  // m, i.e. 0.56 of stature.
  float hlev = f_clamp((a->hit_y - 0.55f) * 2.70f, -1.0f, 1.10f);
  float hrol = a->hit_x;
  v3 pelvis = v3_add(root, v3_add(v3_scale(U, hip_h + 0.07f), v3_scale(R, shift)));
  // Squatting sends the hips BACK, not straight down — that is what keeps a real
  // crouch balanced over the feet instead of tipping forward off them.
  pelvis = v3_add(pelvis, v3_scale(F, -0.105f * a->crouch_s));
  // Running, the mass has to lead the contact.
  pelvis = v3_add(pelvis, v3_scale(F, 0.062f * runT * ground * (1.0f - a->bwd)
                                    - 0.038f * a->bwd * movT * ground));
  // Hips trail the legs — which means they LEAD into the travel on a
  // backward butt-slide (slide_pd is the mirrored leg direction).
  pelvis = v3_add(pelvis, v3_scale(a->slide_pd, (-0.082f - 0.052f * a->slide_v) * a->slide_s));
  // Lean, part 1 of 2: the hips push OUT against the bend.
  if (fabsf(a->lean_s) > 1e-4f) {
    v3 Ra = {{cosf(yaw), 0, sinf(yaw)}};   // aim-relative: you lean where you look
    pelvis = v3_add(pelvis, v3_scale(Ra, -LEAN_HIP * a->lean_s));
  }

  // Keep stance feet PLANTED by sinking the body, not by lifting the foot: when a
  // stance leg cannot reach its plant — hard acceleration strands a foot behind the hip
  // — a real body drops its hip and stays on the ground.
  {
    float mx = ANIM_LEG * ANIM_REACH_K, want = 0.0f;
    for (int i = 0; i < 2; i++) {
      if (a->foot[i].t < 1.0f) continue;  // stance legs only
      float s = i ? 1.0f : -1.0f;
      v3 hip = v3_add(pelvis, v3_add(v3_scale(R, s * ANIM_HIP_HALF),
                                     v3_scale(U, -0.07f)));
      v3 d = v3_sub(a->foot[i].pos, hip);
      float h = sqrtf(d.x * d.x + d.z * d.z);
      float need = sqrtf(fmaxf(0.0f, mx * mx - h * h));
      float have = hip.y - a->foot[i].pos.y;
      if (have > need) want = fmaxf(want, have - need);
    }
    if (advance_state)
      a->sink += (fminf(want, 0.13f) - a->sink) * ease_k(18.0f);
    pelvis.y -= a->sink;
  }
  // A ROUND LOW ON THE BODY PUSHES THE HIPS, NOT THE CHEST. The lever about the waist
  // is the wrong way round there for a trunk fold, and what a gut or a thigh hit
  // actually does is take the stance out from under the man. `hlev` is negative below
  // the waist, so one number serves both ends of the body and the pelvis stops being
  // the one joint that measured the same 2.9 mm whatever the round hit.
  {
    float low = fmaxf(0.0f, -hlev);
    pelvis = v3_add(pelvis,
        v3_add(v3_scale(F, 0.088f * hs_ * hitf * low),
               v3_scale(R, 0.070f * hs_ * hitr * low - 0.045f * hs_ * hrol)));
    pelvis.y -= 0.056f * hs_ * low;
  }

  // Spine: leans into acceleration and speed, hunches when crouched.
  float lean_f = a->lean_f + runT * 0.42f * (1.0f - a->bwd) + 0.46f * a->crouch_s +
                 0.14f * a->ads_s +
                 0.23f * hs_ * hitf * (0.35f + 0.85f * hlev) +
                 0.50f * a->hdown +      // fold over the knees on a hard landing
                 0.10f * a->bwd;         // backpedal: trunk pitches FORWARD —
                 // reclining behind the hips while both feet land ahead of the
                 // CoM is the geometry of a fall, held every stride
  float rec_mag = (0.45f + 0.55f * a->slide_v) * (1.0f - 0.45f * a->slide_bk)
                  - 0.55f * (1.0f - f_clamp(a->slide_v, 0.0f, 1.0f));
  // (negative recline late in the slide = the forward pitch over the lead foot that
  // reads as "standing up out of it") The slide recline acts on the FORE/AFT component
  // of the travel only.
  float sfd = a->slide_dir.x * F.x + a->slide_dir.z * F.z;
  // Gait list: the trunk tips 2-4 deg toward the stance side in phase with
  // the pelvis shift — 4 cm of pure pelvis slide under a plumb trunk is
  // sub-pixel at 10 m, the list is what actually reads.
  float glist = sinf(bph * F_TAU) * 0.055f * movT * ground *
                (1.0f - 0.55f * a->crouch_s) * (1.0f - a->slide_s);
  v3 spine = v3_norm(v3_add(U,
      v3_add(v3_scale(F, lean_f * (1.0f - a->slide_s) - rec_mag * a->slide_s * sfd),
             v3_scale(R, a->lean_r * (1.0f - a->slide_s) + glist
                         + 0.11f * hs_ * hitr
                         - 0.40f * hs_ * hrol   // rolls AWAY from the wound
                         - 0.30f * a->slide_s * (0.35f + 0.65f * fabsf(sfd))))));
  v3 chest = v3_add(pelvis, v3_scale(spine, 0.40f));
  // ...and everything above the waist rides the compliance (see anim_tick): neck, head,
  // both shoulders, the gun anchor and both wrists are all built FROM chest, so one
  // addition here squashes the abdomen and leaves every limb's shape, and therefore
  // every figcheck invariant, untouched.
  chest.y += a->spc;
  // Torso twists most of the way toward the aim, plus a gait-phase counter-rotation
  // against the hips: shoulder swing is what makes a run read as a run once the figure
  // is 50 px tall and the legs are a blur. ...and the gait term is NEGATIVE, because
  // thorax and pelvis counter-rotate.
  float cyaw = ly + wrap_pi(yaw - ly) * 0.78f -
               sinf(bph * F_TAU) * 0.16f * runT * ground;
  v3 Rc = {{cosf(cyaw), 0, sinf(cyaw)}};
  v3 neck = v3_add(chest, v3_scale(spine, 0.10f));
  // The head keeps looking where the eyes are pointed: it follows the spine only
  // partly, and in a deep slide recline it stays almost upright rather than
  // lying back with the torso.
  v3 head_up = v3_norm(v3_add(v3_scale(spine, 0.45f * (1.0f - 0.7f * a->slide_s)),
                              v3_scale(U, 0.55f + 0.40f * a->slide_s)));
  // The head only follows the aim so far: at a full 90 degree look-up, 0.65 of the
  // pitch would lay the skull on the spine and slide the helmet off the back of it.
  float hp = f_clamp(pitch * 0.62f, -0.62f, 0.62f);
  // ...and it LEADS a turn: eyes -> head -> trunk -> pelvis is the human ordering, so
  // the head runs ~0.2 s of yaw rate ahead of the aim (capped ~14 deg).
  float hyaw = yaw + f_clamp(a->yaw_vel * 0.20f, -0.25f, 0.25f);
  v3 head_f = {{sinf(hyaw) * cosf(hp), sinf(hp), -cosf(hyaw) * cosf(hp)}};
  // Keep the skull close to the collar while the torso reclines into a slide.
  v3 head = v3_add(v3_add(neck, v3_scale(head_up, 0.16f + 0.006f * a->slide_s)),
                   v3_scale(head_f, 0.02f));
  // ...AND IT COMES FORWARD OUT OVER THE CHEST, which is the half lifting it could
  // never fix.
  head = v3_add(head, v3_scale(F, 0.50f * rec_mag * a->slide_s * sfd * 0.45f));
  // CHEEK WELD, three components rather than one, because an eye that is not ON the
  // sight line is a man holding a rifle up in front of his face.
  float weld_dn = -0.090f * a->ads_s + 0.030f * a->ads_s * a->crouch_s;
  // ...AND THE WELD'S DROP IS TAKEN BACK IN A SLIDE, for the same geometric reason the
  // crouch tuck is, only worse: a slide reclines the trunk, so the chest and the neck
  // 100 mm along that reclined spine are already low and already behind the head, and
  // the weld's 90 mm then has nowhere to go but into the collar.
  weld_dn -= weld_dn * (0.85f * a->slide_s);
  // Lateral cheek placement is expressed in the chest frame. A former pair of
  // nominally cancelling fore/aft offsets used the hip direction for one half and the
  // aim direction for the other; whenever hips and weapon differed, their residue
  // pushed the eye sideways off the sight line.
  head = v3_add(head, v3_add(v3_scale(U, weld_dn),
                              v3_scale(Rc, 0.068f * a->ads_s)));
  // Crouching, the head TUCKS onto the shoulders — the cheap half of putting the
  // rendered head back inside the crouched HEAD HITBOX (which tops out at eye + 0.18).
  head = v3_add(head, v3_scale(U, -0.075f * a->crouch_s * (1.0f - 0.80f * a->slide_s)
                                                        * (1.0f - 0.85f * a->ads_s) -
                                  0.048f * hs_ * (0.30f + 0.90f * hlev)));
  head = v3_add(head, v3_scale(Rc, -0.052f * hs_ * hrol));
  // Slide: the head TUCKS with speed instead of riding serenely on the recline.
  head = v3_add(head, v3_scale(U, -0.016f * a->slide_s *
                                  f_clamp(a->slide_v, 0.0f, 1.0f)));
  // Gaze stabilisation: a walking human's head barely rises and falls, because the
  // ankles, knees and spine eat the pelvis bob — a head that bobs the full amount reads
  // as a puppet on a stick.
  head.y += bob_drop * 0.40f;
  neck.y += bob_drop * 0.20f;

  // Legs: ankle comes from the foot controller, knee from IK. In a slide the lead leg
  // rides heel-first (toe pitched up, knee soft), the trail leg folds tight with the
  // knee pointing up.
  for (int i = 0; i < 2; i++) {
    float s = i ? 1.0f : -1.0f;
    // Pelvis rolls with the gait — the hip drops on the SWING side (Trendelenburg in
    // reverse). ~5 degrees; without it the hip line is a rigid bar and the whole walk
    // reads mechanical however good the feet are.
    float proll = fminf(s * sinf(bph * F_TAU), 0.0f) * 0.055f *
                  movT * ground * (1.0f - a->slide_s);
    // Pelvic transverse rotation (+-6 deg with the gait): the shoulders are
    // welded to the rifle, so the counter-rotation that sells a run lives in
    // the HIPS — visual only, the foot homes never see it.
    float pyaw2 = sinf(bph * F_TAU) * 0.11f * movT * ground *
                  (1.0f - a->slide_s) * (0.35f + 0.65f * runT);
    v3 Rp = {{cosf(ly + pyaw2), 0, sinf(ly + pyaw2)}};
    v3 hoff = v3_add(v3_scale(Rp, s * ANIM_HIP_HALF), v3_scale(U, -0.07f + proll));
    // The pelvis takes its share of the lean (see LEAN_PELV_K): the hip on the
    // side the trunk went drops, the one carrying the weight hikes.
    if (fabsf(a->lean_s) > 1e-4f) {
      v3 la = {{sinf(yaw), 0, -cosf(yaw)}};
      hoff = v3_rot_axis(hoff, la, a->lean_s * LEAN_ANGLE * LEAN_PELV_K);
    }
    v3 hip = v3_add(pelvis, hoff);
    v3 ank = a->foot[i].pos;
    // First solve the ordinary gait ankle against the ordinary hip.  The Lean offset
    // is presentation DELTA from this baseline, not a relabelling of whatever amount
    // the existing leg-reach clamp had already moved a difficult stance ankle.
    float preflex = (a->foot[i].t >= 1.0f)
                        ? 0.045f * (1.0f - a->stance[i]) * runT * ground
                        : 0.0f;
    float mx = ANIM_LEG * (ANIM_REACH_K - preflex);
    v3 dd = v3_sub(ank, hip);
    float dl = v3_len(dd);
    if (dl > mx) ank = v3_add(hip, v3_scale(dd, mx / dl));
    // The global stance solve above has already chosen and applied `sink`. The lean
    // step is deliberately local after that point: one ankle goes out, its hip gives
    // the authored amount, and the unchanged IK below supplies the knee flex. The
    // controller's foot, stance and phase bytes never see this presentation offset.
    int lean_side = a->lean_step_k > 1e-4f ? 1
                  : a->lean_step_k < -1e-4f ? 0 : -1;
    if (i == lean_side) {
      ank = v3_add(ank, a->lean_step_off);
      float lean_k = fabsf(a->lean_step_k);
      v3 follow = a->lean_step_off;
      follow.y = 0.0f;
      float follow_len = v3_len(follow);
      if (follow_len > 1e-6f)
        hip = v3_add(hip, v3_scale(follow,
            LEAN_STEP_HIP_FOLLOW_M * lean_k / follow_len));
      hip.y -= LEAN_STEP_HIP_DROP_M * lean_k;
    }
    dd = v3_sub(ank, hip);  // never let a segment stretch past its bones
    dl = v3_len(dd);
    if (dl > mx) ank = v3_add(hip, v3_scale(dd, mx / dl));
    float fy = a->foot[i].yaw;
    v3 footF = {{sinf(fy), 0, -cosf(fy)}};
    // Trail-leg knee points up when folded; on a butt-slide it flattens
    // toward the lead leg's soft bend. Backward, both heels dig — toes up.
    float trail_u = 0.68f - 0.34f * a->slide_bk;
    float sld = i ? a->slide_ld : 1.0f - a->slide_ld;
    float uw = 0.22f + 0.5f * a->air_s +
               (sld * 0.45f + (1.0f - sld) * trail_u) * a->slide_s;
    v3 pole = v3_norm(v3_add(v3_scale(footF, 1.0f - 0.55f * a->slide_s),
                             v3_scale(U, uw)));
    // --- ankle pitch -------------------------------------------------------
    // Swinging: toe pointed just after toe-off, toe up for the heel strike.
    float want = 0.0f;  // target ankle pitch in radians, positive = toe UP
    if (a->foot[i].t < 1.0f) {
      float st = 2.0f * f_clamp(a->foot[i].t, 0.0f, 1.0f) - 1.0f;
      if (st < 0.0f)
        st *= f_clamp((a->foot[i].pos.y - base.y - ANIM_ANKLE_Y) / 0.045f, 0.0f, 1.0f);
      want = st * 0.55f * (0.45f + 0.55f * runT);
      // Backpedalling the foot stays pointed through the whole swing so it
      // arrives forefoot-first — that plant is the whole mincing-steps read.
      want = want * (1.0f - a->bwd) - 0.26f * a->bwd;
    } else if (a->air_s > 0.5f) {
      // Airborne: plantarflexed off the push (toe down), DORSIflexed on the
      // fall — the lead foot visibly reaches for the landing.
      want = -0.24f * f_clamp(a->vy / 4.0f, 0.0f, 1.0f) +
             0.30f * f_clamp(-a->vy / 4.0f, 0.0f, 1.0f);
    } else if (ground > 0.5f && movT > 0.05f) {
      // PLANTED: roll the foot through the contact instead of holding it flat.
      float duty = anim_duty(a);
      float ws = i ? 0.5f : 0.0f;
      // PRESENTATION READS bph, NEVER THE RAW CLOCK. Stance progress drives the
      // ankle roll and the strike pre-flex (i.e. the leg-length clamp), and when
      // the PARTNER foot strands and adopts the cycle mid-window the raw phase
      // teleports: this foot's progress jumped 0.77 -> 0.00 in one tick, which
      // is 38 degrees of ankle-pitch target on a boot standing still on the
      // ground. `bob_lag` already carries that discontinuity for everything else.
      float wp = bph - ws;
      if (wp < 0) wp += 1.0f;
      float sp = f_clamp((wp - duty) / fmaxf(1.0f - duty, 0.05f), 0.0f, 1.0f);
      if (advance_state) a->stance[i] = sp;
      float g = movT * ground;
      // Forward: heel strike, roll flat, heel off.
      float fw = 0.0f;
      if (sp < 0.28f) fw = (1.0f - sp / 0.28f) * 0.30f * g;                // strike
      else if (sp > 0.42f) fw = -((sp - 0.42f) / 0.58f) * 0.60f * g;       // heel off
      // Backpedal is a ROCKER, not a held pointe: ball strike (toe down),
      // then the body rolls back over the foot and the ankle DORSIFLEXES
      // through stance — holding plantarflexion for the whole contact was
      // the one thing real backward gait does not do.
      float bk = -(0.18f - 0.30f * sp) * g;
      want = fw * (1.0f - a->bwd) + bk * a->bwd;
      // Crouch-walking rides the balls of the feet — the heels barely settle.
      want -= 0.16f * a->crouch_s * g;
    }
    // Sliding: the lead foot rides heel-first with the toe well up.
    want += (sld * 0.50f + (1.0f - sld) * (0.06f + 0.42f * a->slide_bk)) *
            a->slide_s;
    // Ease, never snap: the foot does not reach its plant at a fixed point of the cycle
    // (measured sp ~0.17 at 2 m/s), so switching from the swing formula to the stance
    // one on contact would jump the pitch by ~11 degrees in one tick.
    if (advance_state)
      a->fpitch[i] += (want - a->fpitch[i]) * ease_k(22.0f);
    float pitch_f = a->fpitch[i];
    // Rotating the foot about the ANKLE would drive the loaded end into the
    // ground; rotate it about the contact point that carries the weight — the
    // heel while the toe is up, the toe tip while the heel is up — so the part
    // touching the floor stays exactly where the foot controller planted it.
    if (fabsf(pitch_f) > 1e-4f) {
      v3 fr = v3_cross(U, footF);
      if (v3_dot(fr, fr) > 1e-6f) {
        fr = v3_norm(fr);
        // The outsole's OWN extremes (see the boot chain in scene_draw_figure): heel
        // rand at -0.086 along the foot axis, toe end at +0.194.
        float d = pitch_f > 0.0f ? -0.086f : 0.194f;  // heel : toe tip
        // ...but a pivot is a CONTACT point, so it only exists while the boot is ON the
        // floor.
        float con = 1.0f - f_clamp((ank.y - base.y - ANIM_ANKLE_Y) *
                                   (1.0f / 0.060f), 0.0f, 1.0f);
        v3 piv = v3_add(ank, v3_scale(footF, d * con));
        piv.y = ank.y + (base.y - ank.y) * con;
        ank = v3_add(piv, v3_rot_axis(v3_sub(ank, piv), fr, -pitch_f));
        footF = v3_norm(v3_rot_axis(footF, fr, -pitch_f));
      }
    }
    P->hip[i] = hip;
    P->ankle[i] = ank;
    P->knee[i] = ik_solve(hip, ank, ANIM_THIGH, ANIM_SHIN, pole);
    // NO -10 mm DROP ON THE TOE JOINT. It was a cosmetic tilt and it cost the sole
    // contract: the boot is swept along normalize(toe - ankle), so a toe joint 10 mm
    // below the ankle tilts the whole sole plane 3.3 degrees nose-down at NEUTRAL —
    // measured with `kine`, a standing boot's toe end sat 8.5 mm underground.
    P->toe[i] = v3_add(ank, v3_scale(footF, 0.175f));
  }

  // Arms hold the rifle two-handed on the aim axis; recoil pulls it back.
  v3 aimd = {{sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)}};
  // CARRY: a soldier who is not aiming does not walk around shouldered.
  v3 gd = aimd;
  v3 carry_pin = {{0, 0, 0}};
  // `ready_s` owns the soft lower/raise transition. The REAL state gate owns
  // whether the SR's idle lobe may exist at all: interpolation cannot resurrect
  // low-ready for one frame in the middle of ADS, reload, bolt, swap, slide or
  // fire. AR uses its standard carriage; the relative idle pose applies only to SR.
  float ready_pose = wpn == WPN_SR && !idle_ok ? 1.0f : a->ready_s;
  float rl = 1.0f - ready_pose;
  // Solve the shoulder line once. Stock seating, brace reach and arm IK all consume
  // these exact anchors; three nearby approximations cannot describe one torso.
  float troll = 0.80f * a->slide_s + 0.24f * a->hdown;
  float trc = cosf(troll), trs = sinf(troll);
  float blade = ANIM_BLADE * ready_pose + 0.20f * runT * (1.0f - ready_pose);
  float blc = cosf(blade), bls = sinf(blade);
  v3 fwdh = v3_norm((v3){{aimd.x, 0.0f, aimd.z}});
  v3 sho_pose[2];
  for (int i = 0; i < 2; i++) {
    float s = i ? 1.0f : -1.0f;
    float shoulder_clear = 0.012f *
                           (1.0f - f_smoothstep(0.20f, 0.85f, ready_pose));
    sho_pose[i] = v3_add(
        chest,
        v3_add(v3_add(v3_scale(Rc, s * (ANIM_SHO_HALF + shoulder_clear) *
                                           trc * blc),
                      v3_add(v3_scale(U, s * ANIM_SHO_HALF * trs),
                             v3_scale(fwdh, -s * ANIM_SHO_HALF * bls))),
               v3_scale(spine, 0.05f)));
  }
  if (rl > 0.003f) {
    v3 gr0 = v3_cross(aimd, U);
    gr0 = v3_dot(gr0, gr0) > 1e-6f ? v3_norm(gr0) : Rc;
    float down = (erg->carry_down - erg->carry_down_run * runT) * rl;
    // Crossed enough that the fore-end actually arrives at the weak hand: at
    // the old 4.6 deg the guard stayed on the strong side and the support
    // wrist target sat at 0.60 m from a 0.58 m arm — a straight rod of an arm
    // pointing at a rifle it never touches.
    float yin = (erg->carry_cross - erg->carry_cross_run * runT) * rl;
    gd = v3_rot_axis(aimd, gr0, -down);
    gd = v3_norm(v3_rot_axis(gd, U, yin));
    // PIN THE BUTTSTOCK THROUGH THE ROTATION. Both rotations above go through the
    // ANCHOR, which sits at the grip — so dropping the muzzle 34 degrees and crossing
    // it 29 swings the buttstock up and rearward into the ribs (`figbury`: the AR's
    // butt pad 71 mm INSIDE the trunk, 14.6% of the weapon buried, a rifle sawn off
    // behind the ejection port from outside).
    v3 up0 = v3_norm(v3_cross(gr0, aimd));
    v3 b0 = gun_pose_offset(gr0, up0, aimd, erg->butt_pin, pose_scale);
    v3 b1 = v3_rot_axis(v3_rot_axis(b0, gr0, -down), U, yin);
    carry_pin = v3_sub(b0, b1);
  }
  // A SLIDE IS A ONE-HANDED CARRY, MUZZLE UP. The support hand is already on the ground
  // behind the hip — that contact is most of what sells a slide, and it is what leaves
  // the weapon in one hand — but the weapon itself kept the low-ready angle, so it lay
  // along a reclined belly and vanished: filmed from the tracking camera the figure was
  // a crouched mass with no rifle anywhere in it.
  if (a->slide_s > 0.001f) {
    v3 gr1 = v3_cross(gd, U);
    gr1 = v3_dot(gr1, gr1) > 1e-6f ? v3_norm(gr1) : Rc;
    gd = v3_norm(v3_rot_axis(gd, gr1, 1.15f * a->slide_s));
  }
  // Sliding, the torso reclines and its belly swings forward into the space the rifle
  // occupies, so the hold is pushed out and up while the recline lasts. Crouching
  // brings the weapon UP and out rather than tucking it against the chest: with the
  // forearm resting flat on the plate carrier the two surfaces become coplanar, which
  // is precisely what z-fights.
  // CARRIAGE. On the sagittal midline with both wrists on the bore axis, the two hands
  // measured the same x to four decimals on every tick — a symmetric two-handed clamp
  // in front of the sternum, which nobody holds a rifle as. The buttstock goes in the
  // STRONG-SIDE shoulder pocket, so the weapon lives 115 mm off the midline; the firing
  // hand is at the grip with the elbow up and out, the support hand reaches across and
  // forward under the handguard with its elbow down and inboard.
  // THE STOCK LIVES IN THE SHOULDER POCKET, and the anchor math has to put it there:
  // the butt pad sits ~0.24 m behind the grip and ~0.10 m below the anchor, so at ready
  // the anchor must ride near the pocket height and only slightly below the shoulder
  // line. ADS pulls it inboard onto the cheek and up to the eye line. The carry drops
  // and crosses the weapon but must NOT pull it closer, or the rotated butt goes into
  // the chest.
  v3 gun_o = v3_add(v3_add(chest, carry_pin),
             v3_add(v3_scale(gd, 0.185f + erg->carry_axial * pose_scale * ready_pose
                                                + 0.000f * a->ads_s
                                                + 0.105f * a->slide_s
                                                + 0.045f * a->crouch_s
                                                - 0.05f * runT),    // tucked in at sprint
             // The carry rides 165 mm FURTHER OUTBOARD and 30 mm higher than the ready,
             // where it used to ride 65 mm inboard and 110 mm lower.
             v3_add(v3_scale(Rc, 0.150f - 0.072f * a->ads_s
                                       + erg->carry_right * pose_scale * rl
                                       + 0.070f * a->slide_s),
             // ...and 65 mm STRAIGHT OUT OF THE CHEST at the low ready, along the
             // trunk's own forward rather than along the (dropped, crossed) bore.
             v3_add(v3_scale(F, erg->carry_body_forward * pose_scale * rl),
                    // THE STOCK STAYS IN THE POCKET WHEN THE RIFLE COMES UP. Going from
                    // ready to ADS used to LIFT the whole weapon 170 mm and pull it 95
                    // mm inboard onto the cheek, which took the butt pad clean off the
                    // shoulder: measured with `puppet ads`, it sat 43 mm clear of the
                    // body in ADS against an 11 mm bite at the ready — a rifle held in
                    // front of a man rather than against him.
                    v3_scale(U, -0.020f + 0.205f * a->ads_s + 0.20f * a->slide_s
                                + erg->carry_up * pose_scale * rl)))));
             // ...and 70 mm of carry LIFT,
             // not 30: the shortened magazine still dipped 49 mm into the strong-side
             // thigh at the low ready, which is the one part of a carried rifle that
             // hangs into a leg. 22 mm now — a magazine touching a trouser, which is
             // what a carried rifle does.
  gun_o.y += bob_drop * 0.55f;
  // ADS is seated from the stock, not guessed from the grip. The descriptor's butt pin
  // is the shared model point; the target is the high firing-shoulder pocket used with
  // armour, slightly forward on the plate. Move the complete eye/weapon complex by the
  // same correction so stock contact cannot trade away sight alignment.
  if (a->ads_s > 0.001f && a->slide_s < 0.999f) {
    v3 seat_r = v3_cross(gd, U);
    seat_r = v3_dot(seat_r, seat_r) > 1e-6f ? v3_norm(seat_r) : Rc;
    v3 seat_u = v3_norm(v3_cross(seat_r, gd));
    v3 butt = v3_add(gun_o,
                     gun_pose_offset(seat_r, seat_u, gd, erg->butt_pin,
                                     pose_scale));
    v3 pocket = v3_add(v3_lerp(chest, sho_pose[1], 0.60f),
                       v3_add(v3_scale(spine, 0.090f), v3_scale(gd, 0.055f)));
    float seat_k = a->ads_s * (1.0f - a->slide_s);
    v3 seat_delta = v3_scale(v3_sub(pocket, butt), seat_k);
    gun_o = v3_add(gun_o, seat_delta);
    head = v3_add(head, seat_delta);
  }
  // Sliding at a deep sideways recline the push-out ran up the tilted chest axis and
  // parked the rifle beside the helmet.
  if (a->slide_s > 0.05f) {
    float gy_max = head.y - 0.22f;
    if (gun_o.y > gy_max) gun_o.y += (gy_max - gun_o.y) * a->slide_s;
  }
  // Small lateral wander with the stride: a carried weapon is never perfectly still
  // relative to the body.
  gun_o = v3_add(gun_o, v3_scale(Rc, sinf(bph * F_TAU) * 0.014f *
                                     movT * ground));
  gun_o.y -= 0.11f * a->hdown + 0.05f * a->land;  // muzzle dips with the landing
  // Air: the arms are ALIVE in a jump — the rifle rides up with the push-off,
  // presses down as the fall builds (the body reaching for balance), and
  // tucks slightly closer the whole flight.
  {
    float rK = f_clamp(a->vy / 4.0f, 0.0f, 1.0f);
    float fK = f_clamp(-a->vy / 3.0f, 0.0f, 1.0f);
    gun_o = v3_add(gun_o, v3_scale(U, (0.060f * rK - 0.045f * fK) * a->air_s));
    gun_o = v3_add(gun_o, v3_scale(gd, -0.030f * a->air_s));
  }
  // Breathing shows up in the weapon too — a standing figure is mostly what you
  // see of a distant enemy, and a perfectly rigid one reads as scenery.
  gun_o = v3_add(gun_o, v3_add(v3_scale(U, sinf(a->idle_t * 1.60f) * 0.0045f * still),
                               v3_scale(Rc, sinf(a->idle_t * 0.92f) * 0.0035f * still)));
  // SHOULDER RESPONSE. fire_k is solely the carry-floor latch. Event-fed springs move
  // one coherent weapon frame, and the wrist/elbow solve makes the arms compress while
  // the shoulder anchors remain on the torso. Lift follows back; signed side adds only
  // millimetres and a few tenths of a degree.
  {
    v3 gun_r = v3_cross(gd, U);
    if (v3_dot(gun_r, gun_r) < 1e-6f) gun_r = Rc;
    else gun_r = v3_norm(gun_r);
    if (a->recoil_lift != 0.0f)
      gd = v3_norm(v3_rot_axis(gd, gun_r, a->recoil_lift));
    if (a->recoil_side != 0.0f)
      gd = v3_norm(v3_rot_axis(gd, U, a->recoil_side));
    gun_r = v3_cross(gd, U);
    if (v3_dot(gun_r, gun_r) < 1e-6f) gun_r = Rc;
    else gun_r = v3_norm(gun_r);
    gun_o = v3_add(gun_o, v3_add(v3_scale(gd, -a->recoil_back),
                                  v3_scale(gun_r, a->recoil_side)));
  }
  // Wrist targets track the WEAPON's own stations, not the anchor plane: the bore is
  // ~0.10 below gun_o and the grip another 0.05 under that, so a trigger wrist at
  // -0.050 U hovered 10 cm above the grip and the support wrist at -0.030 U sat ON TOP
  // of the handguard — both forearms then came down onto the rifle from above, the
  // tray-carry look.
  // These descriptors are authored in the weapon's frame. At low ready the bore is
  // both depressed and crossed, so body-right/world-up would leave the wrists behind
  // while the rifle rotated through them. Build the same orthonormal frame that the
  // renderer later derives from gun_f/gun_u and keep both hands rigidly related to it.
  v3 gun_r = v3_cross(gd, U);
  if (v3_dot(gun_r, gun_r) < 1e-6f) gun_r = Rc;
  else gun_r = v3_norm(gun_r);
  v3 gun_u = v3_norm(v3_cross(gun_r, gd));
  // State corrections stay in the weapon frame. Zero-initialized descriptors retain
  // their base pose exactly; SR's busy-ready gate also keeps rl at zero in a slide.
  v3 trigger_wrist = v3_add(
      erg->trigger_wrist,
      v3_add(v3_scale(erg->trigger_wrist_idle_delta, rl),
             v3_scale(erg->trigger_wrist_slide_delta, a->slide_s)));
  v3 trigger_pole = v3_add(
      erg->trigger_pole,
      v3_add(v3_scale(erg->trigger_pole_ads_delta, a->ads_s),
             v3_add(v3_scale(erg->trigger_pole_idle_delta, rl),
                    v3_scale(erg->trigger_pole_slide_delta, a->slide_s))));
  v3 wriR = v3_add(gun_o,
                   gun_pose_offset(gun_r, gun_u, gd, trigger_wrist, pose_scale));
  // In the CARRY the support hand slides BACK to the magwell end of the guard: with the
  // muzzle rotated 34 degrees down, the far fore-end station sits ~0.76 m from the weak
  // shoulder against a 0.58 m arm — the IK then points a straight arm at a target it
  // can never reach and the hand dangles in space beside the rifle. ...AND IT HAS TO BE
  // INSIDE THE ARM. Measured with figm's elbow columns: at the old 0.250 the support
  // wrist sat 602 mm from the weak shoulder against a 580 mm arm — the IK was OVER-
  // EXTENDED, so it pointed a dead-straight arm at a handguard it could not touch and
  // fig_hand stretched the palm across the gap.
  v3 sw = {{erg->support_wrist_ready.x * pose_scale
               + erg->support_wrist_delta.x * pose_scale * rl,
            erg->support_wrist_ready.y * pose_scale
               + erg->support_wrist_delta.y * pose_scale * rl,
            erg->support_wrist_ready.z * pose_scale
               + erg->support_wrist_delta.z * pose_scale * rl}};
  v3 wriL = v3_add(gun_o, gun_pose_offset(gun_r, gun_u, gd, sw, 1.0f));
  if (a->slide_s > 0.001f) {
    // Off hand braces ON THE GROUND behind the hip — that contact is most of what sells
    // a slide.
    v3 want = v3_add(base, v3_add(v3_scale(R, -0.29f), v3_scale(F, -0.13f)));
    // The wrist rides a fist-height over the floor, so the KNUCKLES are the
    // contact — this is the hand the player reads as dragging on the ground
    // (at 0.004 the old flat-palm wrap wound the fingers under the floor).
    want.y = base.y + 0.068f;  // an override, not an offset: R and F are horizontal
    // The hand plants hard while the slide is fast and LIFTS as it bleeds out
    // (first beat of the exit: hand up, then rock over the lead foot, then
    // rise) — a brace held at walking speed reads as glued.
    float plant = f_smoothstep(0.08f, 0.58f, a->slide_s);
    float bk2 = plant * f_clamp(0.65f + 0.70f * a->slide_v, 0.0f, 1.0f);
    wriL = brace_hand(sho_pose[0], want, wriL, bk2);
  }
  // Hard landing: a hand goes down. `land` is the impact impulse (1.0 ~ 10 m/s),
  // so this only fires on a real drop, not on every step.
  if (a->hdown > 0.02f && a->slide_s < 0.2f) {
    v3 want = v3_add(base, v3_add(v3_scale(R, -0.30f), v3_scale(F, 0.26f)));
    want.y = base.y + 0.045f;
    wriL = brace_hand(sho_pose[0], want, wriL, a->hdown);
  }
  for (int i = 0; i < 2; i++) {
    v3 sho = sho_pose[i];
    v3 wri = i ? wriR : wriL;
    // Elbows are driven wide and down: a rifle stance reads as a triangle, and an elbow
    // tucked against the ribs is exactly where an upper arm starts ploughing through
    // the chest.
    v3 pole = i ? v3_norm(v3_add(v3_scale(Rc, trigger_pole.x
                                                   + 0.30f * a->crouch_s),
                          v3_add(v3_scale(U, trigger_pole.y
                                            + 0.55f * a->air_s),
                                 v3_scale(aimd, trigger_pole.z))))
                : v3_norm(v3_add(v3_scale(Rc, erg->support_pole.x
                                              - 0.20f * a->crouch_s
                                              - 0.85f * a->slide_s
                                              - 0.40f * a->air_s),
                          v3_add(v3_scale(U, erg->support_pole.y
                                              + 0.75f * a->slide_s
                                              + 0.50f * a->air_s),
                                 v3_scale(aimd, erg->support_pole.z
                                                - 0.30f * a->slide_s))));
    // (airborne both elbows flare up and out — the braced-in-flight look) A WRIST NEVER
    // GETS FURTHER FROM ITS SHOULDER THAN THE ARM CAN REACH, and the guard is set at
    // 96% rather than 100 for the reason this file already records: near full extension
    // a two-bone solve is straight whatever the pole vector says, so the elbow stops
    // being steerable before it stops being solvable.
    {
      v3 aw = v3_sub(wri, sho);
      float awl = v3_len(aw), amax = (ANIM_UARM + ANIM_FARM) * ANIM_ARM_REACH_K;
      if (awl > amax) wri = v3_add(sho, v3_scale(aw, amax / awl));
    }
    P->sho[i] = sho;
    P->wri[i] = wri;
    P->elb[i] = ik_solve(sho, wri, ANIM_UARM, ANIM_FARM, pole);
  }

  P->ant_sway = a->ant_p;
  P->pelvis = pelvis;
  P->chest = chest;
  P->neck = neck;
  P->head = head;
  P->gun_o = gun_o;
  P->right = Rc;
  P->head_f = head_f;
  P->gun_f = gd;
  v3 gr = v3_cross(gd, U);
  if (v3_dot(gr, gr) < 1e-6f) gr = Rc;
  P->gun_u = v3_norm(v3_cross(v3_norm(gr), gd));
  if (a->recoil_side != 0.0f)
    P->gun_u = v3_norm(v3_rot_axis(P->gun_u, gd, -1.4f * a->recoil_side));

  // Lean, part 2 of 2: ONE rigid rotation of everything above the waist, about the
  // aim's horizontal forward axis.
  if (fabsf(a->lean_s) > 1e-4f) {
    float ang = a->lean_s * LEAN_ANGLE;
    v3 axis = {{sinf(yaw), 0, -cosf(yaw)}};
    v3 piv = {{P->head.x, P->head.y - LEAN_ARM, P->head.z}};
    #define FIG_LEAN(p) ((p) = v3_add(piv, v3_rot_axis(v3_sub((p), piv), axis, ang)))
    for (int i = 0; i < 2; i++) {
      FIG_LEAN(P->sho[i]);
      FIG_LEAN(P->elb[i]);
      FIG_LEAN(P->wri[i]);
    }
    FIG_LEAN(P->chest);
    FIG_LEAN(P->neck);
    FIG_LEAN(P->head);
    FIG_LEAN(P->gun_o);
    #undef FIG_LEAN
    P->right = v3_rot_axis(P->right, axis, ang);
    P->head_f = v3_rot_axis(P->head_f, axis, ang);
    // The weapon travels and CANTS with the shooter but keeps pointing where the
    // entity is aiming — the round leaves along gun_f, and a leaning bot that
    // shoots 31 degrees off its own sight line is a bug, not a flourish.
    v3 gu = v3_rot_axis(P->gun_u, axis, ang);
    gu = v3_sub(gu, v3_scale(P->gun_f, v3_dot(gu, P->gun_f)));
    if (v3_dot(gu, gu) > 1e-6f) P->gun_u = v3_norm(gu);
  }
}

// A foot that is not STEPPING rides WITH the body: feed the velocity forward FIRST,
// then ease the local offset toward `tgt` — easing a world position toward a target
// that itself moves at body speed lags a constant v/k metres and drags the legs behind
// the figure.
static void foot_ride(afoot_t *f, v3 base, v3 tgt, v3 vel, float yaw, float kf) {
  tgt = foot_clamp_world(base, tgt);
  f->pos.x += vel.x * TICK_DT;
  f->pos.y += vel.y * TICK_DT;
  f->pos.z += vel.z * TICK_DT;
  f->pos = v3_lerp(f->pos, tgt, kf);
  f->pos = foot_clamp_world(base, f->pos);  // never through geometry
  f->from = f->to = f->pos;
  f->t = 1.0f;
  f->yaw += wrap_pi(yaw - f->yaw) * kf;
}

// A WEAPON THAT JUST FIRED IS UP, and this is the ONE copy of that rule. `fire_k` is
// a carry latch — 1.0 at the shot, ~60 ms to decay — and solely the floor under the
// carry blend. Physical recoil lives in the three springs above. The round leaves
// along the aim axis on the tick the
// trigger breaks, and a blend still ramping toward the shoulder draws the muzzle
// somewhere the round did not come from (measured on a foreign player hip-firing: the
// bore 43 degrees off, the muzzle 39 cm below and 35 cm inboard of the rifle the round
// actually left). Nothing is pinned — after the last shot fire_k is long gone before
// the 4.5/s lower even starts.
//
// TWO CALLERS, because the pose is read at two different moments in a tick: anim_tick
// applies it where the blend is solved, and the shot's own event applies it the instant
// the shot happens, because the tracer and the muzzle flash are anchored from there.
// Returns 1 when the blend actually moved, which is what tells that caller its pose is
// now stale.
static int anim_carry_floor(anim_t *a) {
  if (a->ready_s >= a->fire_k) return 0;
  a->ready_s = a->fire_k;
  return 1;
}

// Advance the animation one 120 Hz tick from the entity's physics state.
static void anim_tick(anim_t *a, v3 base, v3 vel, float yaw, float pitch,
                      int grounded, int crouch, float tuck, int slide, float ads,
                      float lean, int ready, int ent, int wpn, int idle_ok) {
  a->live_t = g_tick;   // this pose is solved THIS tick — see anim_t.live_t
  a->prev_pose = a->pose;
  a->prev_ready_s = a->ready_s;
  // The lean arrives already eased by the sim, so authoritative/offline poses assign
  // it directly. A foreign online figure instead sees a snapshot-held level; a very
  // short presentation follower removes that sampling edge (including one lost 60 Hz
  // snapshot) and keeps its complete solved ankles within 5 mm of the 120 Hz pose.
  // This state is cosmetic only; eye exposure, origin, hitbox and wire keep `lean`.
  if (g_online && ent != g_local_ent)
    a->lean_s += (lean - a->lean_s) * ease_k(LEAN_PRESENT_HZ);
  else
    a->lean_s = lean;
  // A third-person figure that is AIMING has to look like it: the stock comes into the
  // shoulder, the weapon rises onto the eye line and the head drops onto the comb.
  a->ads_s += (ads - a->ads_s) * ease_k(14.0f);
  // Carry state: the raise snaps (~110 ms), the lower is deliberate (~0.7 s after the
  // fight ends).
  {
    float want = (ready || ads > 0.05f || (wpn == WPN_SR && !idle_ok))
                   ? 1.0f : 0.0f;
    a->ready_s += (want - a->ready_s) * ease_k(want > 0.5f ? 20.0f : 4.5f);
    // A SHOT MUST NEVER LEAVE A LOWERED RIFLE — see anim_carry_floor for the rule.
    anim_carry_floor(a);
  }

  // Smoothed motion signals; the raw-vs-smoothed velocity gap drives lean.
  v3 dv = v3_sub(vel, a->vel_s);
  float kv = ease_k(14.0f);
  a->vel_s = v3_add(a->vel_s, v3_scale(dv, kv));
  float hspd = sqrtf(a->vel_s.x * a->vel_s.x + a->vel_s.z * a->vel_s.z);
  // ASYMMETRIC, and only on the way DOWN. `a->spd` is the pose's idea of how fast the
  // body is going and it is doubly smoothed (vel_s at 14/s, then this at 14/s again),
  // which costs 225 ms on a stop: braking from 12 m/s the sim is under 1.2 m/s by tick
  // 346 and this still reads 6.5, so the figure holds a full sprint pose — runT
  // saturated, 12.6 deg of forward run lean, running cadence — for 23 ticks after the
  // body has physically halted, and the stop has no beat in it at all.
  a->spd += (hspd - a->spd) * (hspd > a->spd ? kv : ease_k(30.0f));

  a->idle_t += TICK_DT;
  // WHICH FOOT IS CARRYING THE BODY. A stationary figure with one foot in the air is in
  // single support and its weight is over the other one; the pose had no term for that
  // at all, so a 180 turn in place moved the pelvis 5.8 mm laterally and 7.6 mm
  // vertically over 39 ticks of stepping — a rig rotating about its own axis, with no
  // weight in it.
  {
    float w = 0.0f;
    if (a->foot[0].t < 1.0f && a->foot[1].t >= 1.0f) w =  1.0f;
    else if (a->foot[1].t < 1.0f && a->foot[0].t >= 1.0f) w = -1.0f;
    a->wshift += (w - a->wshift) * ease_k(6.5f);
  }
  a->vy += (vel.y - a->vy) * ease_k(16.0f);
  {
    float yv = wrap_pi(yaw - a->yaw_prev) / TICK_DT;
    a->yaw_vel += (yv - a->yaw_vel) * ease_k(12.0f);
    a->yaw_prev = yaw;
  }
  // THE THREE BIG POSE BLENDS ARE SMOOTHSTEPPED, NOT RAW FIRST-ORDER EASES. An
  // exponential ease is at its fastest on its FIRST tick, so a blend that moves the
  // pelvis 0.68 m (stand -> slide) put 90 mm of it into that one tick: a 10.3 m/s
  // vertical velocity step out of nothing, and the same shape on every crouch.
  float kb = ease_k(9.0f);
  a->crouch_lin += (((crouch && !slide) ? 1.0f : 0.0f) - a->crouch_lin) * kb;
  a->crouch_s = smooth01(a->crouch_lin);
  // Slide blend is asymmetric: the drop-in snaps (~60 ms), standing back up
  // out of the skid is deliberately softer.
  float ksl = ease_k(slide ? 17.0f : 8.0f);
  a->slide_lin += ((slide ? 1.0f : 0.0f) - a->slide_lin) * ksl;
  a->slide_s = smooth01(a->slide_lin);
  float ks = ease_k(11.0f);
  a->air_lin += ((grounded ? 0.0f : 1.0f) - a->air_lin) * ks;
  a->air_s = smooth01(a->air_lin);
  // How much of base.y is leg tuck rather than ground.
  if (!grounded) {
    // ...and the one airborne tick where the origin does NOT step with the tuck is the
    // TAKEOFF: on the ground tuck is already 0, so assigning it here collapses whatever
    // the previous landing's unwind still owes in ONE tick with nothing to cancel it —
    // measured on the buffered jump the game explicitly supports, a +503 mm one-tick
    // body step at the very next takeoff.
    if (tuck > 0.0f)           a->root_off = tuck;   // baked: the origin stepped with it
    else if (a->tuck_p > 0.0f) a->root_off = 0.0f;   // released: the origin stepped back
    else                       a->root_off -= a->root_off * ease_k(18.0f);  // took off mid-unwind
  } else {
    a->root_off -= a->root_off * ease_k(18.0f);
    // The landing has happened; let go.
    a->brace -= a->brace * ease_k(16.0f);
  }
  a->tuck_p = tuck;

  float fx = sinf(a->leg_yaw), fz = -cosf(a->leg_yaw);
  float rx = cosf(a->leg_yaw), rz = sinf(a->leg_yaw);
  // How much of the travel is BACKWARD in the hip frame.
  {
    float fc = a->spd > 0.5f ? (a->vel_s.x * fx + a->vel_s.z * fz) / a->spd : 0.0f;
    a->bwd += (f_clamp(-fc, 0.0f, 1.0f) - a->bwd) * ease_k(7.0f);
  }
  float kl = ease_k(8.0f);
  a->lean_f += (f_clamp((dv.x * fx + dv.z * fz) * 0.075f, -0.30f, 0.30f) - a->lean_f) * kl;
  {
    // Accel term decays at constant speed, so a held strafe stood plumb: add
    // a steady lean INTO lateral travel (~7 deg at 6 m/s of lateral speed).
    float latv = a->vel_s.x * rx + a->vel_s.z * rz;
    float want_r = f_clamp((dv.x * rx + dv.z * rz) * 0.065f, -0.18f, 0.18f) +
                   f_clamp(latv * 0.020f, -0.14f, 0.14f);
    a->lean_r += (want_r - a->lean_r) * kl;
  }
  a->fire_k *= expf(-16.0f * TICK_DT);
  recoil_spring(&a->recoil_back, &a->recoil_back_v, RECOIL_BACK_K, RECOIL_BACK_C);
  recoil_spring(&a->recoil_lift, &a->recoil_lift_v, RECOIL_LIFT_K, RECOIL_LIFT_C);
  recoil_spring(&a->recoil_side, &a->recoil_side_v, RECOIL_SIDE_K, RECOIL_SIDE_C);
  // ...and the other half of a firefight.
  a->hit_k *= expf(-11.0f * TICK_DT);
  // ...and the POSE chases the impulse, exactly as `land` chases `land_i` a few lines
  // below and for the same reason: a body does not fold in one tick.
  a->hit_s += (a->hit_k - a->hit_s) * ease_k(45.0f);
  // The wobble clock. A fold that only decays reads as a push; what a hit actually
  // looks like on a standing man is a jolt that overshoots once and rings out.
  a->hit_w = a->hit_k > 0.002f ? a->hit_w + 1.0f : 0.0f;
  // A LANDING HAS TO ARRIVE. Both stages were pure exponentials, so the body never
  // finished standing up: the pelvis reached its low 9 ticks in (right — that part is a
  // real absorb) and then took 1933 ms to give back 99%.
  a->land_i = fmaxf(0.0f, a->land_i * expf(-7.0f * TICK_DT) - 0.0045f);
  a->hdown_i = fmaxf(0.0f, a->hdown_i * expf(-3.5f * TICK_DT) - 0.0060f);  // outlives `land`: a hand reaching for
                                        // the ground has to be readable, not a blink
                                        // The impulse decays exactly as before; what
                                        // the POSE sees CHASES it, so the dip has an
                                        // onset instead of a step.
  a->land += (a->land_i - a->land) * ease_k(20.0f);
  a->hdown += (a->hdown_i - a->hdown) * ease_k(14.0f);
  // SPINE COMPLIANCE: everything above the waist LAGS the hips. Every vertical event
  // — take-off, touchdown, the slide drop-in, standing out of a skid — was
  // transmitted to the skull 1:1, the loudest "rig, not body" cue left in the figure
  // (head-to-pelvis distance moved 3.98 mm over a whole jump, i.e. the torso is a rod
  // bolted to the pelvis). A damped 1.9 Hz follower on the pelvis's OWN vertical
  // acceleration buys the overlap and follow-through for one float and no geometry.
  // DEAD-BANDED at 90 m/s^2, and that is what makes it safe: a 12 m/s run peaks at
  // 70.8 m/s^2 of pelvis acceleration and a 4 m/s walk at 31.1, so no gait can reach
  // the band and every steady-state locomotion measurement stays byte-identical.
  // Driven off LAST tick's solved pelvis (`a->pose` is still tick N-1 here), which
  // keeps the filter integrating exactly once per tick and leaves skel_solve a pure
  // function of the state. Not tidiness: the `lean` proof RE-SOLVES a copy of the
  // player's anim, and a filter stepped inside skel_solve would be stepped twice.
  {
    float pv = a->pose.pelvis.y;
    float vv = (pv - a->spc_py) * (1.0f / TICK_DT);
    float acc = (vv - a->spc_pv) * (1.0f / TICK_DT);
    a->spc_py = pv;
    a->spc_pv = vv;
    float ex = acc - f_clamp(acc, -90.0f, 90.0f);   // impacts only, never gait
    const float w0 = F_TAU * 1.9f, zt = 0.50f;
    a->spc_v += (-w0 * w0 * a->spc - 2.0f * zt * w0 * a->spc_v - 0.090f * ex) * TICK_DT;
    a->spc += a->spc_v * TICK_DT;
    // Hard stops, with the velocity killed at them: an impact bigger than any measured
    // here has to squash the trunk, never pull the chest through the pelvis or the neck
    // out of its collar.
    if (a->spc < -0.055f) { a->spc = -0.055f; if (a->spc_v < 0) a->spc_v = 0; }
    if (a->spc >  0.030f) { a->spc =  0.030f; if (a->spc_v > 0) a->spc_v = 0; }
  }

  if (slide && !a->was_slide) a->slide_t = 0;  // fresh slide: entry envelope
  a->was_slide = slide;
  if (slide) {
    a->slide_t += TICK_DT;
    float hl = sqrtf(vel.x * vel.x + vel.z * vel.z);
    if (hl > 0.5f) a->slide_dir = (v3){{vel.x / hl, 0, vel.z / hl}};
    a->slide_v += (f_clamp(hl / 8.0f, 0.0f, 1.2f) - a->slide_v) * ease_k(10.0f);
  } else {
    a->slide_v *= expf(-6.0f * TICK_DT);
  }
  if (v3_dot(a->slide_dir, a->slide_dir) < 1e-6f) a->slide_dir = (v3){{fx, 0, fz}};
  if (slide) {
    // Knees only bend one way: mirror the travel direction into the front hemisphere
    // for everything the LEGS do.
    float fdot = a->slide_dir.x * fx + a->slide_dir.z * fz;
    a->slide_bk = f_clamp(-fdot, 0.0f, 1.0f);
    a->slide_pd = a->slide_dir;
    if (fdot < 0) {
      a->slide_pd.x -= 2.0f * fdot * fx;
      a->slide_pd.z -= 2.0f * fdot * fz;
    }
  }
  if (v3_dot(a->slide_pd, a->slide_pd) < 1e-6f) a->slide_pd = (v3){{fx, 0, fz}};
  // WHICH LEG LEADS IS CONTINUOUS. It is decided by the sign of the skid's LATERAL
  // component, and a slide aimed straight ahead sits on that sign change, so a player
  // steering through dead-ahead mid-slide swapped the two legs' targets outright — 0.43
  // m of `along` and 60 mm of foot height in one step.
  {
    float lat = a->slide_pd.x * rx + a->slide_pd.z * rz;
    float w = smooth01(f_clamp(lat * (1.0f / 0.36f) + 0.5f, 0.0f, 1.0f));
    a->slide_ld += (w - a->slide_ld) * ease_k(12.0f);
  }

  // Landing: remember the fall speed for the dip, plant both feet where the
  // body came down — the next steps recenter them naturally.
  if (!grounded) a->fall = fminf(a->fall, vel.y);
  if (grounded && !a->was_grounded) {
    // Scaled up by the attack's own peak loss (see the ease rates above), so the
    // rendered dip depth is unchanged: an attack that starts at zero never reaches the
    // impulse's initial value while the impulse is already decaying.
    if (-a->fall > 1.6f)
      ev_push((event_t){.type = EV_LAND, .a = base, .f = -a->fall, .src = ent});
    a->land_i = fminf(1.0f, -a->fall * 0.10f) * 1.75f;
    // Only a real drop, not every hop: 6 m/s is about a 1.8 m fall.
    a->hdown_i = f_clamp((-a->fall - 6.0f) * 0.22f, 0.0f, 1.0f) * 1.60f;
    for (int i = 0; i < 2; i++) {
      afoot_t *f = &a->foot[i];
      f->pos.y = base.y + ANIM_ANKLE_Y;
      f->from = f->to = f->pos;
      f->t = 1.0f;
      f->yaw = a->leg_yaw + (i ? 1.0f : -1.0f) * ANIM_TOE_OUT;
    }
  }
  if (grounded) a->fall = 0;   // covers the landing edge above too
  a->was_grounded = grounded;

  // Hip heading.
  float want_yaw = yaw;
  if (!slide && a->spd > 1.0f) {
    float mvy = atan2f(a->vel_s.x, -a->vel_s.z);
    float dm = wrap_pi(mvy - yaw), ad = fabsf(dm);
    float w = f_clamp((ad - 0.18f) / 0.40f, 0.0f, 1.0f) *
              (1.0f - f_clamp((ad - 2.45f) / 0.55f, 0.0f, 1.0f)) *
              f_clamp((a->spd - 1.0f) / 2.5f, 0.0f, 1.0f);
    want_yaw = yaw + f_clamp(dm, -ANIM_TWIST_MAX, ANIM_TWIST_MAX) * w;
  }
  float dyaw = wrap_pi(want_yaw - a->leg_yaw);
  if (!slide && a->spd > 0.6f)
    a->leg_yaw += dyaw * ease_k(9.0f);
  else if (fabsf(dyaw) > 0.45f)
    a->leg_yaw += (dyaw - (dyaw > 0 ? 0.28f : -0.28f)) * ease_k(10.0f);

  // Gait phase.
  int moving = grounded && !slide && a->spd > 0.35f;
  // One cadence number, shared with the plant-ahead budget and the duty's reach bound
  // (anim_hz).
  float hz = anim_hz(a);
  if (moving) {
    a->phase += hz * TICK_DT;
    if (a->phase >= 1.0f) a->phase -= 1.0f;
    a->bob_lag *= expf(-13.0f * TICK_DT);   // ~0.2 s to pay the jump back
  }

  if (!grounded) {
    // Airborne: legs tuck while rising, reach down as the fall builds.
    float tti = 9.9f;
    if (vel.y < -0.5f) tti = ground_drop(base, 4.0f) / -vel.y;
    a->brace += (f_clamp(1.0f - tti * (1.0f / 0.24f), 0.0f, 1.0f) - a->brace) *
                ease_k(22.0f);
    float atuck = f_clamp(1.0f - fabsf(vel.y) / 5.0f, 0.05f, 1.00f);
    // Rise / apex / fall are DIFFERENT poses, staged on vertical velocity: legs trail
    // behind after the push-off, tuck peaks at the apex (the existing envelope), and on
    // the way down they re-extend with the lead foot reaching FORWARD for the contact.
    float riseK = f_clamp(vel.y / 4.0f, 0.0f, 1.0f);
    float fallK = f_clamp(-vel.y / 1.6f, 0.0f, 1.0f);
    atuck *= 1.0f - 0.90f * fallK;  // falling UNTUCKS: feet lead the body down
    float kf = ease_k(17.0f);       // the tuck SNAPS up — an energetic jump
                                    // pulls the knees, it does not float them
    for (int i = 0; i < 2; i++) {
      float s = i ? 1.0f : -1.0f;
      float lead = i ? 0.10f : -0.06f;  // slight scissor reads as a real jump
      float along = lead - 0.10f * atuck - 0.15f * riseK +
                    (i ? 0.30f : 0.10f) * fallK +
                    (i ? 0.10f : -0.09f) * a->brace;   // scissor into the plant
      // Height above the collider's FLOOR. The tuck the pose wants is the same however
      // it was produced, but an air crouch has already paid part of it in base.y
      // (root_off), so subtract what is already in there — stacking the two put the
      // ankles level with the hips. root_off is 0 for a bot, a plain jump AND a jump
      // with crouch held from the ground, so all three keep their exact expression.
      float lift = atuck * (i ? 0.46f : 0.38f) + 0.06f * riseK +
                   0.22f * a->crouch_s - 0.055f * a->brace - a->root_off;
      if (lift < 0.0f) lift = 0.0f;
      v3 tgt = {{base.x + rx * s * 0.13f + fx * along,
                 base.y + ANIM_ANKLE_Y + lift,
                 base.z + rz * s * 0.13f + fz * along}};
      // Y matters as much as XZ here, which is why the full `vel` goes in: leaving
      // vel.y out lagged the foot ~v/k = 0.45 m below a rising body, which cancelled
      // the whole tuck, pushed hip-to-ankle past the bone budget and left the reach
      // clamp holding the legs dead straight through the entire jump.
      foot_ride(&a->foot[i], base, tgt, vel, a->leg_yaw, kf);
    }
  } else if (slide) {
    // Slide: heel-first lead leg extends along the pose direction (further the faster
    // we go, pulling in as the slide bleeds off), trail leg folds tight under the hips;
    // both skid with the body — that IS the contact.
    v3 pd = a->slide_pd;
    float kf = ease_k(22.0f);
    float syaw = atan2f(pd.x, -pd.z);
    for (int i = 0; i < 2; i++) {
      float s = i ? 1.0f : -1.0f;
      float ld = i ? a->slide_ld : 1.0f - a->slide_ld;  // how much THIS leg leads
      float along = ld * (0.44f + 0.22f * a->slide_v) +
                    (1.0f - ld) * (0.01f + 0.42f * a->slide_bk);
      float footy = ld * 0.075f +
                    (1.0f - ld) * (0.135f - 0.060f * a->slide_bk);
      v3 tgt = {{base.x + pd.x * along + rx * s * 0.17f,
                 base.y + footy,
                 base.z + pd.z * along + rz * s * 0.17f}};
      // Skid HORIZONTALLY only.
      foot_ride(&a->foot[i], base, tgt, (v3){{vel.x, 0.0f, vel.z}}, syaw, kf);
    }
  } else {
    // Grounded stepping. Swing targets land ahead along the velocity so the
    // body passes over the planted foot; backpedaling shortens the reach.
    float runT = anim_runT(a);
    // A planted foot is frozen in the world, so the leg has to span the whole distance
    // the body travels while it is down: stance_time * speed.
    float duty = anim_duty(a);
    // Land the foot EXACTLY half a stance ahead.
    float ahead_t = 0.5f * (1.0f - duty) / hz *
                    (1.0f - 0.18f * a->bwd) * (1.0f - 0.18f * a->crouch_s);
    // Horizontal reach budget for a plant: how far from the hip a foot may be placed
    // and still be reachable when the hip bobs to its highest.
    float hmax = anim_hmax(a);
    for (int i = 0; i < 2; i++) {
      afoot_t *f = &a->foot[i], *o = &a->foot[i ^ 1];
      float s = i ? 1.0f : -1.0f;
      // Stance WIDENS with whatever lateral drift the hip twist could not absorb: a
      // strafing player stands wider, and the extra width is the room the feet need
      // before they would cross.
      float lath = fabsf(a->vel_s.x * rx + a->vel_s.z * rz);
      // Crouched, the stance narrows: a wide-legged squat waddles.
      float half = (0.14f + 0.11f * f_clamp(lath / 3.0f, 0.0f, 1.0f)) *
                   (1.0f + 0.14f * a->crouch_s);
      // Idle stagger: nobody stands with their boots on a line.
      float idle_k = 1.0f - f_clamp(a->spd / 0.7f, 0.0f, 1.0f);
      float stag = (i ? -0.050f : 0.070f) * idle_k * (1.0f - a->crouch_s * 0.4f);
      v3 home = {{base.x + rx * s * (half + 0.012f * idle_k) + fx * stag,
                  base.y + ANIM_ANKLE_Y,
                  base.z + rz * s * (half + 0.012f * idle_k) + fz * stag}};
      float ws = i ? 0.5f : 0.0f;
      float wp = a->phase - ws;
      if (wp < 0) wp += 1.0f;
      int in_win = moving && wp < duty;
      if (f->t >= 1.0f) {
        float ddx = f->pos.x - home.x, ddz = f->pos.z - home.z;
        float d = sqrtf(ddx * ddx + ddz * ddz);
        float ye = wrap_pi(a->leg_yaw - f->yaw);
        int idle_fix = !moving && o->t >= 1.0f && (d > 0.10f || fabsf(ye) > 0.45f);
        // A planted foot also has to re-step once the body has slid far enough sideways
        // that the foot would end up across the midline — the clamp above only
        // constrains where a step LANDS, not where the body goes afterwards.
        float plat = (f->pos.x - base.x) * rx + (f->pos.z - base.z) * rz;
        int crossing = moving && s * plat < 0.02f;
        // An imminent crossing overrides the "don't lift both feet" guard: a shuffle
        // step with both feet briefly light is how a real player deals with sideways
        // drift, and it beats dragging one leg through the other.
        float eth = hmax * ANIM_SPAN_K;
        float oth = 0.25f + 0.25f * a->bwd;
        int emerg = moving && !in_win && (crossing || (d > eth && o->t >= oth));
        if (emerg || (in_win && wp > 0.05f)) {  // adopt the cycle mid-window
          // Carry the discontinuity instead of inflicting it on the body.
          float dph = (ws + 0.002f) - a->phase;
          while (dph > 0.5f) dph -= 1.0f;
          while (dph < -0.5f) dph += 1.0f;
          a->bob_lag += dph;
          a->phase = ws + 0.002f;
          wp = 0.002f;
          in_win = moving;
        }
        if (in_win || idle_fix) {
          f->from = f->pos;
          f->t = 0.0001f;
        }
      }
      if (f->t < 1.0f) {
        float tprev = f->t;
        v3 toprev = f->to;
        // Cap the plant's ahead DISTANCE in metres, not only in time: at 6 m/s the time
        // budget put the boot 0.55 m ahead of the pelvis with a straight knee — a
        // braking lunge every stride, 2x the human ~0.25 m.
        float amax = hmax * ANIM_SPAN_K, aspd = sqrtf(v3_dot(a->vel_s, a->vel_s));
        float at2 = aspd > 1e-4f ? fminf(ahead_t, amax / aspd) : ahead_t;
        v3 to = {{home.x + a->vel_s.x * at2, home.y, home.z + a->vel_s.z * at2}};
        float ox = to.x - base.x, oz = to.z - base.z;
        float ol = sqrtf(ox * ox + oz * oz);
        // Never aim a plant outside the leg's horizontal reach at the HIGHEST the hip
        // will bob to during that stance — sqrt(reach^2 - drop^2).
        if (ol > hmax * 0.97f) {
          to.x = base.x + ox / ol * hmax * 0.97f;
          to.z = base.z + oz / ol * hmax * 0.97f;
        }
        // A foot may never step across the body's midline.
        float lat = (to.x - base.x) * rx + (to.z - base.z) * rz;
        if (s * lat < ANIM_STANCE_MIN) {
          float d2 = s * ANIM_STANCE_MIN - lat;
          to.x += d2 * rx;
          to.z += d2 * rz;
        } else if (s * lat > 0.30f) {
          // Cap the OUTWARD component too: uncapped lateral plants make every fast
          // strafe a ~1 m near-straight split with the pelvis parked midway — weight
          // committed to neither leg.
          float d2 = s * 0.30f - lat;
          to.x += d2 * rx;
          to.z += d2 * rz;
        }
        f->to = foot_clamp_world(base, to);
        // A committed swing's target may WANDER but never jump: once the foot is a
        // quarter through its flight, cap target motion per tick.
        if (tprev > 0.25f) {
          // The cap is on the target's WANDER, not on its travel. `to` is built from
          // `home`, which is body-relative, so a committed swing's target legitimately
          // translates with the body at v*dt every tick.
          float allow = 0.055f + sqrtf(a->vel_s.x * a->vel_s.x +
                                       a->vel_s.z * a->vel_s.z) * TICK_DT;
          v3 dto = v3_sub(f->to, toprev);
          float dl2 = v3_len(dto);
          if (dl2 > allow)
            f->to = v3_add(toprev, v3_scale(dto, allow / dl2));
        }
        if (in_win) f->t = fmaxf(f->t, wp / duty);  // phase-locked swing
        else f->t += TICK_DT / 0.18f;                    // idle/finish-up step
        // The swing is the CLOCK's. The backpedal used to force the progress forward
        // here so the planted partner could not drift out front, which capped the
        // swing at ~14 ticks against the clock's ~33 and collapsed the foot cycle: the
        // gait ran at 2.4x its own cadence and every step ended in `emerg`
        // teleporting `phase`. The drift it was aiming at was really the alternation
        // gate holding the stance foot past its reach.
        // ...and the clock may never SLAM the progress forward: when the partner's
        // emerg adoption teleports the phase, the phase-locked term above would jump
        // this foot most of a swing in ONE tick. Capped at ~2.2x the nominal swing
        // rate, so the clock catches the foot up over a few ticks.
        {
          float nom = TICK_DT * hz / fmaxf(duty, 0.20f);
          f->t = fminf(f->t, tprev + 2.2f * nom);
        }
        f->yaw += wrap_pi(a->leg_yaw + s * ANIM_TOE_OUT - f->yaw) * ease_k(14.0f);
        if (f->t >= 1.0f) {
          f->t = 1.0f;
          f->pos = f->to;
          f->yaw = a->leg_yaw + s * ANIM_TOE_OUT;
          // A FOOT LANDED. This is the one moment in the whole animation that makes a
          // sound, and it is worth having: after the weapon, footsteps are what a
          // shooter is actually listening to — they are the only continuous information
          // channel about someone you cannot see.
          if (v3_dot(a->vel_s, a->vel_s) > 0.6f) {
            float step_speed = sqrtf(v3_dot(a->vel_s, a->vel_s));
            ev_push((event_t){.type = EV_STEP, .a = f->pos, .src = ent,
                              .f = step_speed, .g = a->crouch_s});
            bot_noise_emit(ent, BOT_NOISE_STEP, f->pos,
                           bot_step_noise_radius(step_speed, a->crouch_s));
          }
        } else {
          float tc = f_clamp(f->t, 0.0f, 1.0f);
          // Forward swings ease OUT (fast off the toe — the pendulum);
          // the backpedal's short quick steps read twitchy with that same
          // fast start, so they blend back toward the symmetric curve.
          float eo = 1.0f - (1.0f - tc) * (1.0f - tc);
          float e = eo + (smooth01(tc) - eo) * 0.7f * a->bwd;
          // Accelerating hard drives the knee higher — the same speed reached by
          // coasting and by sprinting out of a standstill should not look alike.
          float lift = moving ? (0.035f + 0.205f * runT +
                                 0.24f * f_clamp(a->lean_f, 0.0f, 0.26f)) *
                                (1.0f - 0.60f * a->bwd)
                              : 0.028f;
          // A SPRINT HAS HEEL RECOVERY, and a straight line from plant to plant cannot
          // express it: travelling x and z linearly on the same `e` the sine lift uses,
          // the swing foot could never come back UNDER the hip, so the leg scissored on
          // horizontal shins and no value of `lift` bought the tuck — the tuck is a
          // PATH property.
          float sprT = f_clamp((a->spd - 4.0f) * (1.0f / 8.0f), 0.0f, 1.0f);
          lift += 0.11f * sprT * moving;
          float pull = sinf(F_PI * e) * 0.46f * sprT * (1.0f - a->bwd);
          f->pos.x = f->from.x + (f->to.x - f->from.x) * e;
          f->pos.z = f->from.z + (f->to.z - f->from.z) * e;
          f->pos.x += (base.x - f->pos.x) * pull;
          f->pos.z += (base.z - f->pos.z) * pull;
          f->pos.y = base.y + ANIM_ANKLE_Y + sinf(F_PI * e) * lift;
        }
      }
    }
  }

  // Presentation-only lean step. It is intentionally downstream of every afoot/phase
  // mutation above: the controller has finished, then one absolute ankle target is
  // constrained and scanned once. `lean_s` is already the production-smoothed signal;
  // the small follower here belongs to the FOOT, not to the torso lean. It limits the
  // physical grow/retract rate so full-rate and held snapshot samples cannot disagree
  // by an entire footstep. On a sign change the old side retracts to sub-millimetre
  // zero before the other side grows, and extension is bounded by the same follower.
  {
    g_lean_step_want_off = (v3){{0, 0, 0}};
    g_lean_step_got_off = (v3){{0, 0, 0}};
    g_lean_step_ratio = 0.0f;
    g_lean_step_side = -1;

    int want_side = a->lean_s > 1e-4f ? 1 : a->lean_s < -1e-4f ? 0 : -1;
    int have_side = a->lean_step_k > 1e-4f ? 1
                  : a->lean_step_k < -1e-4f ? 0 : -1;
    v3 old_off = a->lean_step_off;
    float run = g_sim_mv[MV_RUN] > 0.1f ? g_sim_mv[MV_RUN] : 0.1f;
    float raw_spd = sqrtf(vel.x * vel.x + vel.z * vel.z);
    float speed_k = 1.0f - f_clamp((raw_spd / run - LEAN_SPD_LO) /
                                    (LEAN_SPD_HI - LEAN_SPD_LO), 0.0f, 1.0f);
    int hard_clear = !grounded || slide;
    int active = !hard_clear && want_side >= 0 && speed_k > 1e-4f;

    if (hard_clear) {
      a->lean_step_off = (v3){{0, 0, 0}};
      a->lean_step_k = 0.0f;
    } else {
      int solve_side = -1;
      v3 want_off = {{0, 0, 0}};
      float crouch_k = 1.0f + (LEAN_STEP_CROUCH_K - 1.0f) * a->crouch_s;

      // A sign change, speed fade or released key retracts the old side first. It is
      // still a rendered moving ankle, so it gets the same swept query as extension;
      // changing foot home or yaw never reuses a stale radial clearance segment.
      if (have_side >= 0 && (!active || have_side != want_side)) {
        solve_side = have_side;
      } else if (active) {
        solve_side = want_side;
        float side_sign = want_side ? 1.0f : -1.0f;
        float amount = fabsf(a->lean_s) * crouch_k * speed_k;
        v3 aim_r = {{cosf(yaw), 0, sinf(yaw)}};
        v3 aim_f = {{sinf(yaw), 0, -cosf(yaw)}};
        v3 authored = v3_add(v3_scale(aim_r, side_sign * LEAN_STEP_OUT_M),
                             v3_scale(aim_f, LEAN_STEP_FWD_M));
        v3 authored_dir = v3_norm(authored);
        v3 start = a->foot[want_side].pos;
        v3 desired = v3_add(start, v3_scale(authored, amount));

        // Clamp `desired` to the intersection of the reach disk and stance-side slab
        // in the orthonormal leg frame. Only an outward delta may become an active
        // Lean target.
        float hmax = anim_hmax(a) * 0.97f;
        float bx = desired.x - base.x, bz = desired.z - base.z;
        float lat = bx * rx + bz * rz;
        float fwd = bx * fx + bz * fz;
        float lat_mag = f_clamp(side_sign * lat, ANIM_STANCE_MIN,
                                 fminf(0.30f, hmax));
        lat = side_sign * lat_mag;
        float fmax = sqrtf(fmaxf(0.0f, hmax * hmax - lat * lat));
        fwd = f_clamp(fwd, -fmax, fmax);
        desired.x = base.x + rx * lat + fx * fwd;
        desired.z = base.z + rz * lat + fz * fwd;
        want_off = v3_sub(desired, start);

        // No capacity in the authored direction means no Lean target.  Keep an old
        // presentation foot only long enough to retract; never turn an inward reach
        // correction into a new active lunge.
        if (v3_dot(want_off, authored_dir) <= 1e-6f) {
          want_off = (v3){{0, 0, 0}};
          if (have_side < 0) solve_side = -1;
        }
      }

      if (solve_side >= 0) {
        v3 start = a->foot[solve_side].pos;
        v3 desired = v3_add(start, want_off);
        v3 previous = a->prev_pose.ankle[solve_side];
        lean_foot_sweep_t sw = lean_foot_sweep(previous, desired,
                                               LEAN_STEP_MARGIN_M);
        // The solid result is a hard cap; the presentation DELTA has its own follower.
        // Keeping these two quantities separate prevents an ordinary IK reach
        // correction (or a moving afoot home) from becoming self-sustaining Lean state.
        // If a synthetic/new obstacle surrounds the previous endpoint, geometry wins
        // immediately at the first safe exit; there is no valid eased point inside it.
        v3 safe_off = v3_sub(sw.p, start);
        a->lean_step_off = sw.escaped ? safe_off
                                      : v3_lerp(old_off, safe_off,
                                                ease_k(LEAN_STEP_RESPONSE_HZ));
        a->lean_step_off.y = 0.0f;
        float got = v3_len(a->lean_step_off);
        if (got < 0.0005f) {
          a->lean_step_off = (v3){{0, 0, 0}};
          got = 0.0f;
        }

        // `lean_step_off`, `lean_step_k`, and hip/knee weight derive from the same
        // eased presentation vector. The achieved ratio is got/want; `sw.p` remains
        // only the hard clearance endpoint.
        float want = v3_len(want_off);
        float got_along = got;
        if (want > 1e-6f) {
          v3 step_dir = v3_scale(want_off, 1.0f / want);
          got_along = f_clamp(v3_dot(a->lean_step_off, step_dir), 0.0f, want);
          g_lean_step_ratio = got_along / want;
        }
        float full_len = sqrtf(LEAN_STEP_OUT_M * LEAN_STEP_OUT_M +
                               LEAN_STEP_FWD_M * LEAN_STEP_FWD_M);
        // Crouch deliberately shortens the horizontal step, not the weight transfer.
        float pose_len = full_len * crouch_k;
        float achieved = pose_len > 1e-6f
                           ? f_clamp(got / pose_len, 0.0f, 1.0f) : 0.0f;
        float side_sign = solve_side ? 1.0f : -1.0f;
        a->lean_step_k = side_sign * achieved;
        g_lean_step_want_off = want_off;
        g_lean_step_got_off = a->lean_step_off;
        g_lean_step_side = achieved > 0.0f ? solve_side : -1;
      }
    }
  }

  {
    // Antenna whip: a 3 Hz, zeta 0.45 follower on the chest's acceleration.
    const float w0 = F_TAU * 3.0f;
    v3 drive = v3_scale(dv, -0.030f / TICK_DT * 0.01f);
    drive.y -= (vel.y - a->vy) * 0.020f;
    drive.x += -sinf(a->leg_yaw) * a->yaw_vel * 0.012f;
    drive.z += cosf(a->leg_yaw) * a->yaw_vel * 0.012f;
    a->ant_v = v3_add(a->ant_v,
        v3_scale(v3_add(v3_add(v3_scale(a->ant_p, -w0 * w0),
                               v3_scale(a->ant_v, -2.0f * 0.45f * w0)),
                        v3_scale(drive, w0 * w0)), TICK_DT));
    a->ant_p = v3_add(a->ant_p, v3_scale(a->ant_v, TICK_DT));
    float m = v3_len(a->ant_p);
    if (m > 0.11f) a->ant_p = v3_scale(a->ant_p, 0.11f / m);
  }
  skel_solve(a, base, yaw, pitch, wpn, idle_ok, SKEL_ADVANCE_STATE);
}

// Fresh spawn: feet planted at their home spots, pose solved once so the
// very first rendered frame already stands correctly.
static void anim_reset(anim_t *a, v3 pos, float yaw, int wpn) {
  memset(a, 0, sizeof *a);
  // Reset solves a combat-safe shouldered pose (`idle_ok = 0`). Initialize the SR
  // follower to that pose; the first real idle tick lowers it softly, keeping follower
  // and pose consistent at spawn or teleport.
  if (wpn == WPN_SR) a->ready_s = a->prev_ready_s = 1.0f;
  a->leg_yaw = yaw;
  a->was_grounded = 1;
  for (int i = 0; i < 2; i++) {
    float s = i ? 1.0f : -1.0f;
    v3 home = {{pos.x + cosf(yaw) * s * 0.13f, pos.y + ANIM_ANKLE_Y,
                pos.z + sinf(yaw) * s * 0.13f}};
    a->foot[i].pos = a->foot[i].from = a->foot[i].to = home;
    a->foot[i].yaw = yaw;
    a->foot[i].t = 1.0f;
  }
  skel_solve(a, pos, yaw, 0.0f, wpn, 0, SKEL_ADVANCE_STATE);
  // Prime BOTH histories that get differentiated, or the first tick after a spawn
  // measures a step out of nothing: the pelvis against zero (a 116 m/s trunk step) and
  // the aim yaw against zero — a body spawning at yaw = pi registered ~377 rad/s of
  // turn rate, i.e. the shoulders led a turn nobody made, once per respawn.
  a->spc_py = a->pose.pelvis.y;
  a->yaw_prev = yaw;
  a->prev_pose = a->pose;
}

// The humans' own third-person bodies — same skeleton as the bots, ticked from player
// physics at the end of the world tick, human 0's drawn only when the camera is not
// first-person.
static anim_t g_player_anim_all[SRV_LOBBIES][MAX_HUMANS];
#define g_player_anim (g_player_anim_all[g_lobby])
// THE PLAYER'S OWN CORPSE. A bot ragdolls as an ENTITY for the second between its death
// and its respawn and only then hands the body to the pool; the player cannot, because
// every camera in the game — first person, third person, the audio listener — is
// derived from `p->pos`, and flinging that 6 m across the arena is a death cam nobody
// asked for.
static rag_t g_player_rag_all[SRV_LOBBIES][MAX_HUMANS];
#define g_player_rag (g_player_rag_all[g_lobby])
static int   g_player_corpse_q_all[SRV_LOBBIES][MAX_HUMANS];  // launch awaits handover
#define g_player_corpse_q (g_player_corpse_q_all[g_lobby])

// The humans themselves: a human IS a player, so he gets the player's code path — that
// is the one-copy rule. ~1.5 KB per human with the anim/rag arrays above.
static player_t g_players_all[SRV_LOBBIES][MAX_HUMANS];
#define g_players (g_players_all[g_lobby])
