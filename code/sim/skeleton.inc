// =============================================================================
// sim/skeleton.inc  —  anim_tick, skel_solve, adaptive limb/carry solvers. Do NOT
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
// Once the get-up has passed this blend, ordinary gait owns the contacts again. Waiting
// for the slide pose to become numerically zero kept both feet body-relative for ~0.7 s:
// a standing, accelerating figure visibly skated before its first running step.
#define ANIM_SLIDE_GAIT_HANDOFF 0.25f
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
  // Angular rise belongs to weapon_t's physical barrel spring. These event-fed
  // channels retain only stock compression and tiny lateral/cant response, so a
  // remote body cannot display a second muzzle direction over the authoritative one.
  [WPN_AR] = {1.25f, 0.00f, 0.20f, 3.10f, 0.00f, 0.24f, 0.72f},
  [WPN_SR] = {2.25f, 0.00f, 0.32f, 5.30f, 0.00f, 0.36f, 0.65f},
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
  float sign = gun_pattern_sign(wpn, shot, (int)(h & 1u));
  float amp = 0.85f + (float)((h >> 8) & 255u) * (0.30f / 255.0f);
  return sign * amp;
}

static float recoil_brace(const recoil_pose_t *r, float ads) {
  return 1.0f + (r->ads_k - 1.0f) * f_clamp(ads, 0.0f, 1.0f);
}

// A SHOULDER ALREADY ABSORBING A BURST ABSORBS THE NEXT ROUND BETTER, and the round's
// own NUMBER is what says how braced it is: the round that opens a burst throws its
// whole authored impulse (shot 0 is 1.0 by construction — the tap and the sniper's
// single shot are never taxed), and the crown's rounds throw exactly half of it from
// round ten on. The index rather than the accumulated heat, because every viewer
// already has it — the shooter as w->idx, a watcher as the fire event's own e->i — so
// the damping is identical on every screen without a byte of wire or a second clock.
// PRESENTATION ONLY: this scales stock compression, lateral response and the view's
// roll. THE AXIS IS NEVER TOUCHED — where the barrel points is the pattern's alone.
static float recoil_kick_damp(int shot) {
  // Clamped BOTH ways: the index reaching here off a fire event is the wire's int16 and
  // hostile, so the damp is bounded to [0.5, 1] whatever arrives.
  float s = fminf(fmaxf((float)shot, 0.0f) / 10.0f, 1.0f);
  return 1.0f - 0.5f * s;
}

// The local view and the event-fed body deliberately share the same source values,
// bracing rule, burst damping and stateless side sample. Proofs call these two entry
// points too, so they can exercise production impulses without copying presentation
// constants.
static void recoil_vm_impulse(vm_t *vm, int src, int shot, int wpn, float ads) {
  const recoil_pose_t *rp = recoil_pose(wpn);
  float brace = recoil_brace(rp, ads) * recoil_kick_damp(shot);
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
    // Keep every firing-arm state on the anatomical right side. The former hip pole
    // pointed hard left (-3.4) while ADS and idle pointed right; their blends crossed
    // the arm axis and periodically selected the opposite two-bone solution.
    .trigger_pole = {{1.25f, -1.55f, 0.35f}},
    .trigger_pole_ads_delta = {{0.05f, 1.25f, -0.53f}},
    .trigger_pole_idle_delta = {{0.65f, -0.30f, -0.75f}},
    .trigger_pole_slide_delta = {{3.35f, 2.05f, -1.05f}},
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

// Defined with the one shared rifle model later in the translation unit. Combat uses
// the model's published barrel tip; it must never restate weapon-local muzzle offsets.
static v3 pose_muzzle(const pose_t *P, int wpn);
// Also defined at the muzzle table: how far ahead of the EYE the bore must be clear
// for the weapon to be carried up and fired (the wall-tuck reach).
static float gun_wall_reach(int wpn);

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
  float   kneel_s, kneel_lin; // idle crouch: firing knee down; fades before gait
  float   slide_t;           // seconds since the slide began (entry envelope)
  float   slide_v;           // eased slide speed factor 0..1.2 (recline depth)
  float   slide_knee_s;      // collision-safe knee-pole ownership on exit
  int     was_slide;
  int     was_slide_pose;    // previous eased-pose ownership, for gait handoff
  float   land;              // landing dip impulse, decays
  float   hdown;             // hard-landing hand-to-ground reach, decays slower
  // ...and the IMPULSES those two chase.
  float   land_i, hdown_i;
  float   fall;              // most negative vel.y of the current air time
  float   brace;             // 0..1 preparation for a landing that is coming
  float   lean_f, lean_r;    // fwd/right acceleration lean (rad)
  float   fire_k;            // shot-triggered eased carry intent; not recoil distance
  // THE WALL AT THE MUZZLE. weapon_anim_tick publishes the crisp predicate (the one
  // copy is gun_wall_blocked); the follower eases the carriage tuck the arena sees.
  // Derivable from state + solids on every viewer, so it never touches the wire.
  unsigned char gunwall_want;
  float   gunwall_s;
  // Third-person shoulder response. prev_pose, rather than duplicate prev scalars,
  // interpolates the solved gun and hands for rendering.
  float   recoil_back, recoil_back_v;  // m toward the shoulder
  float   recoil_lift, recoil_lift_v;  // spring only — no pose consumer (see the
                                       // shoulder-response note in skel_solve)
  float   recoil_side, recoil_side_v;  // m, signed in gun-right (also rolls the
                                       // frame about the bore at -1.4 rad/m)
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
                             // Latched once on entry; it cannot swap mid-slide.
  v3      slide_knee_pole[2]; // persistent bend planes survive a straight lead leg
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

// A fire event needs the exact live muzzle for its trace and VFX while the visible body
// retains its eased carry transition. ev_push is synchronous, so one event-local pose
// slot is sufficient and cannot leak into a later shot.
static pose_t g_fire_anchor_pose;
static int g_fire_anchor_src = -1;
static long g_fire_anchor_tick = -1;

static void recoil_body_impulse(anim_t *a, int src, int shot, int wpn, float ads) {
  const recoil_pose_t *rp = recoil_pose(wpn);
  float brace = recoil_brace(rp, ads) * recoil_kick_damp(shot);
  float side = recoil_side_sample(src, shot, wpn);
  a->recoil_back_v += rp->body_back_v * brace;
  a->recoil_lift_v += rp->body_lift_v * brace;
  a->recoil_side_v += rp->body_side_v * brace * side;
}

// A low-ready lobe is allowed only by real state, never by a transient gun-animation
// curve. The helper is pure so server, prediction, remote presentation and proofs make
// the same decision. A plain ready intent is deliberately NOT a hard gate: it is the
// target of ready_s and must remain visible as the fast raise transition. Fire and
// cooldown are also intents, not hard pose gates: forcing them to ready in one solve
// made idle hip-fire teleport both arms.
enum { GUN_POSE_BLOCKED = -1, GUN_POSE_LOCOMOTION, GUN_POSE_IDLE };

// Three states matter here. Idle may lower the SR, ordinary locomotion, firing and the
// weapon cycle raise it through ready_s's follower, and a spatial manipulation gate
// must be shouldered immediately so first- and third-person cannot disagree on
// reload/ADS/swap/slide. The cycle clock is intentionally not such a gate: using it
// here made every hip-fired shot teleport both arms.
static int gun_pose_state(const weapon_t *w, const anim_t *a, v3 vel,
                          int grounded, int slide) {
  (void)a;  // presentation history never hard-gates a manipulation pose
  float hspd2 = vel.x * vel.x + vel.z * vel.z;
  if (!grounded || slide || w->ads_want || w->ads_s > 0.01f ||
      w->reload_t != 0 || w->switch_t != 0)
    return GUN_POSE_BLOCKED;
  return hspd2 <= 0.20f * 0.20f ? GUN_POSE_IDLE : GUN_POSE_LOCOMOTION;
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

// How far below the collider's plane a foot may reach for its own ground before it
// simply dangles: past this the leg cannot cover the drop even with the sink
// solver's hip drop paid in.
#define ANIM_FOOT_DROP 0.30f

// THE GROUND THIS FOOT STANDS ON, which is not always the collider's floor: a body
// on a platform edge has one boot over air. Highest solid top under the point that
// is not above the collider's own plane (a wall beside a boot is not a floor), else
// the arena plane. Pure over the solids, so every viewer solves the same boot, and
// exactly base.y wherever the ground is flat — the flat-ground gait is bit-identical
// by construction.
// ...but only where the world MODEL explains the collider's own plane: a solid top
// at base.y under the body's footprint, or the arena floor itself. A pose staged on
// a synthetic "flat ground" at an arbitrary height (armcheck runs its gait at
// y = 5) has no such support, and adapting its boots to the real arena far below
// would turn every staged proof into a permanent dangle. Where support is missing
// the feet keep the collider's plane, exactly as before this solver existed.
static int foot_ground_supported(v3 base) {
  if (base.y < 0.04f) return 1;  // the arena floor
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (fabsf(s->max.y - base.y) > 0.04f) continue;
    if (base.x < s->min.x - 0.35f || base.x > s->max.x + 0.35f ||
        base.z < s->min.z - 0.35f || base.z > s->max.z + 0.35f) continue;
    return 1;
  }
  return 0;
}

static float foot_ground_y(v3 base, float x, float z) {
  if (!foot_ground_supported(base)) return base.y;
  float top = 0.0f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (x < s->min.x || x > s->max.x || z < s->min.z || z > s->max.z) continue;
    if (s->max.y > base.y + 0.04f) continue;
    if (s->max.y > top) top = s->max.y;
  }
  return top;
}

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

// Exact centre-line distance for two finite limb segments. Slide poses use this on
// four thigh/shin pairs; it is closed-form, allocation-free and only active while a
// figure slides.
static float limb_segment_distance(v3 p0, v3 p1, v3 q0, v3 q1) {
  v3 u = v3_sub(p1, p0), v = v3_sub(q1, q0), w = v3_sub(p0, q0);
  float a = v3_dot(u, u), b = v3_dot(u, v), c = v3_dot(v, v);
  float d = v3_dot(u, w), e = v3_dot(v, w);
  float den = a * c - b * b;
  float s = den > 1e-8f ? f_clamp((b * e - c * d) / den, 0.0f, 1.0f) : 0.0f;
  float t = c > 1e-8f ? f_clamp((b * s + e) / c, 0.0f, 1.0f) : 0.0f;
  if (a > 1e-8f) s = f_clamp((b * t - d) / a, 0.0f, 1.0f);
  t = c > 1e-8f ? f_clamp((b * s + e) / c, 0.0f, 1.0f) : 0.0f;
  return v3_len(v3_sub(v3_add(p0, v3_scale(u, s)),
                       v3_add(q0, v3_scale(v, t))));
}

// [[maybe_unused]]: the callers are the harness's slide proofs, and the
// Windows build compiles the harness out.
[[maybe_unused]] static float slide_leg_clearance(const pose_t *P, v3 left_knee, v3 right_knee) {
  float best = limb_segment_distance(P->hip[0], left_knee,
                                     P->hip[1], right_knee);
  best = fminf(best, limb_segment_distance(P->hip[0], left_knee,
                                           right_knee, P->ankle[1]));
  best = fminf(best, limb_segment_distance(left_knee, P->ankle[0],
                                           P->hip[1], right_knee));
  return fminf(best, limb_segment_distance(left_knee, P->ankle[0],
                                           right_knee, P->ankle[1]));
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

// Smallest torso-ellipse clearance along an arm segment. The shoulder itself grows out
// of the torso, so the upper-arm samples begin beyond that joint; everything below is
// the actual visible tube that must stay outside the carrier. Points above the shoulder
// or below the belt are not torso collisions.
static float arm_torso_segment_clearance(v3 shoulder, v3 elbow, v3 wrist,
                                         v3 chest, v3 body_right, v3 body_fwd) {
  const float upper_t[] = {0.30f, 0.48f, 0.66f, 0.84f, 1.0f};
  // A steep-down support forearm crosses the vest near its wrist, not near its elbow;
  // sampling only 12/28% certified precisely the wrong half of that segment.
  const float fore_t[] = {0.12f, 0.28f, 0.45f, 0.62f, 0.78f, 0.92f};
  float best = 9.0f;
  for (int seg = 0; seg < 2; seg++) {
    const float *samples = seg ? fore_t : upper_t;
    int count = seg ? (int)(sizeof fore_t / sizeof fore_t[0])
                    : (int)(sizeof upper_t / sizeof upper_t[0]);
    v3 a = seg ? elbow : shoulder;
    v3 b = seg ? wrist : elbow;
    for (int i = 0; i < count; i++) {
      v3 rel = v3_sub(v3_lerp(a, b, samples[i]), chest);
      if (rel.y < -0.43f || rel.y > 0.11f) continue;
      // Expanded by the arm tube's radius: centre-line clearance alone still lets the
      // rendered upper arm disappear halfway into the vest.
      float across = fabsf(v3_dot(rel, body_right)) / 0.205f;
      float forward = fabsf(v3_dot(rel, body_fwd)) / 0.185f;
      best = fminf(best, sqrtf(across * across + forward * forward));
    }
  }
  return best;
}

// Move one two-bone bend plane toward another without ever crossing the zero vector.
// Normalized lerps are not continuous when the two poles oppose each other: the middle
// sample has no direction, then the elbow/knee reappears on the other side. A bounded
// rotation around the actual root-to-tip axis gives the joint temporal ownership and
// leaves bone lengths to the analytic IK solve.
static v3 ik_pole_step(v3 previous, v3 desired, v3 axis,
                       float max_turn, float handedness) {
  previous = v3_sub(previous, v3_scale(axis, v3_dot(previous, axis)));
  desired = v3_sub(desired, v3_scale(axis, v3_dot(desired, axis)));
  if (v3_dot(desired, desired) < 1e-7f)
    desired = v3_dot(previous, previous) > 1e-7f
                ? previous : (v3){{handedness, 0.25f, 0.0f}};
  if (v3_dot(previous, previous) < 1e-7f) return v3_norm(desired);
  previous = v3_norm(previous);
  desired = v3_norm(desired);
  float angle = acosf(f_clamp(v3_dot(previous, desired), -1.0f, 1.0f));
  if (angle <= max_turn) return desired;
  float orient = v3_dot(v3_cross(previous, desired), axis);
  float sign = fabsf(orient) > 1e-6f ? (orient < 0.0f ? -1.0f : 1.0f)
                                     : (handedness < 0.0f ? -1.0f : 1.0f);
  return v3_norm(v3_rot_axis(previous, axis, sign * max_turn));
}

// Clamp a bend plane on its analytic solution circle so the middle joint cannot pass
// below a contact plane. This is continuous at the boundary: only the pole's component
// toward projected world-up changes, while its existing twist side is retained.
static v3 ik_pole_floor(v3 root, v3 tip, float l1, float l2, v3 pole,
                        float floor_y, float handedness) {
  v3 delta = v3_sub(tip, root);
  float dl = f_clamp(v3_len(delta), fabsf(l1 - l2) + 0.01f,
                     l1 + l2 - 0.008f);
  v3 axis = v3_norm(delta);
  pole = v3_sub(pole, v3_scale(axis, v3_dot(pole, axis)));
  if (v3_dot(pole, pole) < 1e-7f) pole = (v3){{handedness, 0.25f, 0.0f}};
  pole = v3_norm(pole);
  float cosa = f_clamp((l1 * l1 + dl * dl - l2 * l2) /
                       (2.0f * l1 * dl), -1.0f, 1.0f);
  float proj = l1 * cosa;
  float radius = sqrtf(fmaxf(0.0f, l1 * l1 - proj * proj));
  if (radius < 1e-5f) return pole;
  float centre_y = root.y + axis.y * proj;
  float need_y = (floor_y - centre_y) / radius;
  if (pole.y >= need_y) return pole;
  v3 up = v3_sub((v3){{0, 1, 0}}, v3_scale(axis, axis.y));
  float up_len = v3_len(up);
  if (up_len < 1e-5f) return pole;
  up = v3_scale(up, 1.0f / up_len);
  float up_k = f_clamp(need_y / up.y, -1.0f, 1.0f);
  v3 tangent = v3_norm(v3_cross(axis, up));
  float tangent_sign = v3_dot(pole, tangent);
  if (fabsf(tangent_sign) < 1e-6f) tangent_sign = handedness;
  float side_k = sqrtf(fmaxf(0.0f, 1.0f - up_k * up_k));
  return v3_norm(v3_add(v3_scale(up, up_k),
                        v3_scale(tangent, tangent_sign < 0.0f ? -side_k : side_k)));
}

// Solve an elbow, then react when either visible arm segment enters the torso
// footprint. The fallback bends toward the wrist's side of the body (front for a
// rifle, rear for a planted slide hand) while retaining the authored up/down
// preference. This is one collision response shared by every weapon, motion and
// pitch, not a stop-animation special case.
static v3 arm_elbow_solve(v3 shoulder, v3 wrist, v3 pole,
                          v3 previous_shoulder, v3 previous_elbow,
                          v3 previous_wrist, v3 chest, v3 body_right,
                          float side) {
  v3 axis = v3_norm(v3_sub(wrist, shoulder));
  v3 body_fwd = {{body_right.z, 0.0f, -body_right.x}};
  v3 authored = v3_sub(pole, v3_scale(axis, v3_dot(pole, axis)));
  v3 previous = v3_sub(v3_sub(previous_elbow, shoulder),
                       v3_scale(axis, v3_dot(v3_sub(previous_elbow, shoulder), axis)));
  float previous_ok = v3_dot(previous, previous) > 1e-7f &&
                      v3_len(v3_sub(previous_shoulder, shoulder)) < 0.10f &&
                      v3_len(v3_sub(previous_wrist, wrist)) < 0.12f;
  if (v3_dot(authored, authored) < 1e-7f)
    authored = previous_ok ? previous : v3_scale(body_right, side);
  authored = v3_norm(authored);
  if (previous_ok) {
    previous = v3_norm(previous);
    // An almost straight arm has a vanishing elbow circle: a tiny target change can
    // otherwise select its opposite side and make the joint snap through the torso.
    // Preserve the preceding anatomical bend plane increasingly near full reach.
    float reach = v3_len(v3_sub(wrist, shoulder)) / (ANIM_UARM + ANIM_FARM);
    float keep = 0.34f + 0.52f * f_smoothstep(0.72f, ANIM_ARM_REACH_K, reach);
    if (v3_dot(authored, previous) < -0.10f) keep = fmaxf(keep, 0.86f);
    authored = v3_norm(v3_lerp(authored, previous, keep));
    authored = ik_pole_step(previous, authored, axis, 0.10f, side);
  }
  v3 elbow = ik_solve(shoulder, wrist, ANIM_UARM, ANIM_FARM, authored);
  float radial = arm_torso_segment_clearance(shoulder, elbow, wrist, chest,
                                              body_right, body_fwd);
  // Start steering before contact and ramp continuously. The old branch jumped from
  // zero correction to at least 72% on the first crossing tick, which was the reported
  // elbow slap when a run stopped or an idle rifle came up.
  float avoid = f_smoothstep(1.18f, 0.72f, radial);
  if (avoid <= 0.001f) return elbow;

  // Shoulder position is the anatomical authority. Rolled/bladed shoulders can make
  // a caller's nominal left/right sign point through the torso for exactly the stop
  // frame this guard handles.
  float wrist_fwd = v3_dot(v3_sub(wrist, chest), body_fwd);
  // Continuous over the sternum. The former +/- branch flipped the trigger elbow's
  // bend plane while a steeply-down rifle hovered around its threshold.
  float fore = f_clamp((wrist_fwd + 0.04f) * (1.0f / 0.18f), -1.0f, 1.0f);
  v3 safe = v3_add(v3_scale(body_right, 1.30f * side),
                   v3_add(v3_scale(body_fwd, 0.28f + 0.44f * fore),
                          v3_scale((v3){{0, 1, 0}},
                                   f_clamp(pole.y, -0.65f, 0.35f))));
  safe = v3_norm(safe);
  v3 corrected = v3_norm(v3_lerp(authored, safe, avoid));
  if (previous_ok) {
    // Collision avoidance is still a target, not permission to choose a new limb
    // hemisphere in one frame. 0.10 rad at 120 Hz is fast enough for a shouldering
    // rifle and bounds the visible elbow arc to a few centimetres.
    corrected = ik_pole_step(previous, corrected, axis, 0.10f, side);
  }
  return ik_solve(shoulder, wrist, ANIM_UARM, ANIM_FARM, corrected);
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
  // In the idle kneel, settle a little weight over the firing-side knee. The offset
  // disappears before crouch gait takes over, so movement keeps the symmetric stance.
  pelvis = v3_add(pelvis, v3_scale(R, 0.030f * a->kneel_s));
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
    // A nominally straight slide leg still needs a visible bend circle. At the reach
    // clamp that circle collapses to a point; millimetres of ankle motion then rotate
    // the shin arbitrarily even with a stable pole vector.
    float mx = ANIM_LEG * (ANIM_REACH_K - preflex - 0.035f * a->slide_s);
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
    // One-knee contacts use a world-space knee target. IK still owns both bone
    // lengths; this only chooses the valid side of its solution circle.
    float kneel_trail = i == 1 ? a->kneel_s : 0.0f;
    if (kneel_trail > 0.001f) {
      v3 knee_want = v3_add(base,
          v3_add(v3_scale(F, 0.20f),
                 v3_add(v3_scale(R, 0.08f), v3_scale(U, 0.066f))));
      v3 kp = v3_norm(v3_sub(knee_want, hip));
      pole = v3_norm(v3_lerp(pole, kp, kneel_trail));
    }
    if (a->slide_s > 0.001f) {
      // The lateral lane belongs to the PELVIS, not to travel. A travel-perpendicular
      // lane turns ninety degrees on a side-slide and makes both bones cross between
      // anatomically left/right hips and direction-relative feet. Both knees stay on
      // their own side of R; only the lead/trail distance follows slide_pd.
      float back_pose = f_smoothstep(0.18f, 0.72f, a->slide_bk);
      // A knee-pad contact is not merely a downward pole: with 43 cm bones the knee's
      // solution circle reaches floor height only when the contact is about 40 cm from
      // the low slide hip. Put the folded knee forward of the pelvis; the folded foot
      // remains behind it. The rear/supine pose does not use this contact.
      float knee_along = 0.24f * sld +
                         (0.29f - 0.07f * back_pose) * (1.0f - sld);
      float knee_side = 0.21f * sld + 0.25f * (1.0f - sld);
      // Forward and lateral: the folded leg skids on its knee pad. Backward: lift that
      // knee into a supine, sole-supported fold; dragging the same knee contact against
      // travel makes the shin look broken and is not the pose BO6 uses for its back
      // slide. The extended leg retains a small, controllable bend in both cases.
      float folded_up = 0.055f + 0.205f * back_pose;
      float knee_up = (0.155f + 0.025f * back_pose) * sld +
                      folded_up * (1.0f - sld);
      float slide_lat = fabsf(a->slide_pd.x * R.x + a->slide_pd.z * R.z);
      float knee_fwd = slide_lat * (0.06f * sld + 0.18f * (1.0f - sld));
      v3 knee_want = v3_add(base,
          v3_add(v3_scale(a->slide_pd, knee_along),
                 v3_add(v3_scale(F, knee_fwd),
                        v3_add(v3_scale(R, s * knee_side),
                               v3_scale(U, knee_up)))));
      v3 kp = v3_norm(v3_sub(knee_want, hip));
      pole = v3_norm(v3_lerp(pole, kp, a->slide_s));
    }
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
    // A PLANTED boot whose own ground is out of reach points its toe: held sole-flat
    // over a drop it read as levitation at platform corners. Planted only — a swing
    // foot is above the plane by design and already owns its pitch. Exactly zero on
    // flat ground (the gap clamp's 60 mm dead band).
    if (a->foot[i].t >= 1.0f && ground > 0.5f) {
      float dg = ank.y - ANIM_ANKLE_Y - foot_ground_y(base, ank.x, ank.z);
      want -= 0.42f * f_clamp((dg - 0.06f) * (1.0f / 0.12f), 0.0f, 1.0f);
    }
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
        // The contact plane is THIS boot's own ground (a foot hanging past a curb
        // pivots on the lower floor it reaches, not on the collider's plane).
        // foot_ground_y == base.y on flat ground, so the ordinary gait is unchanged.
        float gfloor = foot_ground_y(base, ank.x, ank.z);
        float con = 1.0f - f_clamp((ank.y - gfloor - ANIM_ANKLE_Y) *
                                   (1.0f / 0.060f), 0.0f, 1.0f);
        v3 piv = v3_add(ank, v3_scale(footF, d * con));
        piv.y = ank.y + (gfloor - ank.y) * con;
        ank = v3_add(piv, v3_rot_axis(v3_sub(ank, piv), fr, -pitch_f));
        footF = v3_norm(v3_rot_axis(footF, fr, -pitch_f));
      }
    }
    // The contact-pivot rotation above can add a few millimetres of hip distance after
    // the ordinary reach clamp. Pay that back here before IK; otherwise the solver has
    // no exact two-bone solution and the shin visibly stretches during a fast slide.
    dd = v3_sub(ank, hip);
    dl = v3_len(dd);
    if (dl > mx) ank = v3_add(hip, v3_scale(dd, mx / dl));
    P->hip[i] = hip;
    P->ankle[i] = ank;
    P->knee[i] = ik_solve(hip, ank, ANIM_THIGH, ANIM_SHIN, pole);
    // NO -10 mm DROP ON THE TOE JOINT. It was a cosmetic tilt and it cost the sole
    // contract: the boot is swept along normalize(toe - ankle), so a toe joint 10 mm
    // below the ankle tilts the whole sole plane 3.3 degrees nose-down at NEUTRAL —
    // measured with `kine`, a standing boot's toe end sat 8.5 mm underground.
    P->toe[i] = v3_add(ank, v3_scale(footF, 0.175f));
  }

  // The authored directional pose above is the one target. The former collision pass
  // ranked sixteen target pairs and then twenty-five local pairs anew every tick; tiny
  // clearance changes changed the winner and made the folded knee buzz between valid
  // solutions. Keep one persistent bend plane per leg and rotate it continuously.
  if (a->slide_s > 0.001f && a->was_slide) {
    for (int i = 0; i < 2; i++) {
      float s = i ? 1.0f : -1.0f;
      v3 axis = v3_norm(v3_sub(P->ankle[i], P->hip[i]));
      v3 desired = v3_sub(P->knee[i], P->hip[i]);
      v3 previous = a->slide_knee_pole[i];
      // At entry inherit the already visible knee plane. Starting from a generic world
      // pole is a pose cut even if every later sample is smooth.
      if (a->slide_t <= 1.5f * TICK_DT || v3_dot(previous, previous) < 1e-7f)
        previous = v3_sub(a->prev_pose.knee[i], P->hip[i]);
      previous = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                               previous, base.y + 0.045f, s);
      desired = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                              desired, base.y + 0.045f, s);
      v3 pole = ik_pole_step(previous, desired, axis, 0.120f, s);
      pole = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                           pole, base.y + 0.045f, s);
      P->knee[i] = ik_solve(P->hip[i], P->ankle[i],
                            ANIM_THIGH, ANIM_SHIN, pole);
      if (advance_state) {
        a->slide_knee_pole[i] = pole;
      }
    }
  } else if (!a->was_slide && a->slide_knee_s > 0.001f) {
    // During get-up, walk each persistent bend plane toward the ordinary authored pole
    // around its actual hip-to-ankle axis. The shared bounded rotation has no singular
    // midpoint and preserves both bone lengths via IK.
    for (int i = 0; i < 2; i++) {
      v3 axis = v3_norm(v3_sub(P->ankle[i], P->hip[i]));
      // Use the latched bend plane, not the previous knee point: at full extension
      // the knee point sits on the hip-ankle axis and loses all information about
      // which side it should bend toward.
      v3 previous = a->slide_knee_pole[i];
      v3 desired = v3_sub(v3_sub(P->knee[i], P->hip[i]),
                          v3_scale(axis,
                              v3_dot(v3_sub(P->knee[i], P->hip[i]), axis)));
      previous = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                               previous, base.y + 0.045f,
                               i ? 1.0f : -1.0f);
      desired = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                              desired, base.y + 0.045f,
                              i ? 1.0f : -1.0f);
      v3 pole = ik_pole_step(previous, desired, axis, 0.055f,
                             i ? 1.0f : -1.0f);
      pole = ik_pole_floor(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN,
                           pole, base.y + 0.045f, i ? 1.0f : -1.0f);
      P->knee[i] = ik_solve(P->hip[i], P->ankle[i], ANIM_THIGH, ANIM_SHIN, pole);
      if (advance_state) a->slide_knee_pole[i] = pole;
    }
  }

  // Arms hold the rifle two-handed on the aim axis; recoil pulls it back.
  v3 aimd = {{sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)}};
  // CARRY: a soldier who is not aiming does not walk around shouldered.
  v3 gd = aimd;
  v3 carry_pin = {{0, 0, 0}};
  // `ready_s` owns the soft lower/raise transition. The REAL state gate owns
  // whether the SR's idle lobe may exist at all: interpolation cannot resurrect
  // low-ready for one frame in the middle of ADS, reload, swap or slide. Fire and the
  // weapon cycle arrive through ready_s, so their first visible frame stays continuous.
  // AR uses its standard carriage; the relative idle pose applies only to SR.
  // State changes set ready_s's TARGET; they never bypass its follower. runT then
  // carries either weapon continuously from idle sway into its authored run hold.
  // The AR's normal carriage is the firing axis; lowering it between engagements made
  // its first hip round leave a visibly down/crossed barrel. The SR keeps its authored
  // patrol carry, but a fire edge raises it on every viewer before the next solve.
  float ready_pose = (wpn == WPN_AR || idle_ok < GUN_POSE_LOCOMOTION ||
                      a->fire_k > 0.01f) ? 1.0f : a->ready_s;
  // A WALL AT THE MUZZLE OWNS THE CARRIAGE, whatever the carry wanted — both weapons,
  // every state, multiplied AFTER the gates above so a reloading or aiming figure
  // still tucks against a wall. fire_k overrides it continuously: gun_shoot's wall
  // gate guarantees no round leaves while walled, so a firing pose is by definition
  // clear, and the anchor solve zeroes gunwall_s outright for the same reason.
  ready_pose *= 1.0f - a->gunwall_s * (1.0f - a->fire_k);
  float rl = 1.0f - ready_pose;
  // Solve the shoulder line once. Stock seating, brace reach and arm IK all consume
  // these exact anchors; three nearby approximations cannot describe one torso.
  float troll = 0.80f * a->slide_s + 0.24f * a->hdown;
  float trc = cosf(troll), trs = sinf(troll);
  float blade = ANIM_BLADE * ready_pose + 0.20f * runT * (1.0f - ready_pose);
  float blc = cosf(blade), bls = sinf(blade);
  // The shoulder blade direction is a yaw quantity. Normalizing aimd's horizontal
  // residue near PITCH_MAX amplifies cos(pitch)'s last bits and can reverse the whole
  // shoulder line as the sniper reaches the floor. Use the exact yaw frame instead.
  v3 fwdh = {{sinf(yaw), 0.0f, -cosf(yaw)}};
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
  // A SLIDE IS A ONE-HANDED CARRY, MUZZLE UP — while it is only a carry. The support
  // hand is already on the ground behind the hip — that contact is most of what sells a
  // slide, and it is what leaves the weapon in one hand — but the weapon itself kept the
  // low-ready angle, so it lay along a reclined belly and vanished: filmed from the
  // tracking camera the figure was a crouched mass with no rifle anywhere in it.
  // A ROUND RAISES IT LIKE EVERY OTHER CARRY. fire_k is the floor, saturating on the same
  // 0.01 latch the low-ready reads above so both carries let go over one window, and the
  // anchor solve below zeroes it outright: the rifle a tracer leaves points where the sim
  // sent the round. Raw fire_k cannot be that floor — it is decayed once more before this
  // solve, leaving 8.2 deg of muzzle over the round that just left, and at 750 rpm it
  // sits at 0.26 between two rounds, i.e. 48 deg of swing inside every burst. Past the
  // latch the floor ramps rather than steps, so the carry comes back instead of snapping.
  float fire_up = f_clamp(a->fire_k / 0.01f, 0.0f, 1.0f);
  float slide_tilt = 1.15f * a->slide_s * (1.0f - fire_up);
  if (slide_tilt > 0.001f) {
    v3 gr1 = v3_cross(gd, U);
    gr1 = v3_dot(gr1, gr1) > 1e-6f ? v3_norm(gr1) : Rc;
    gd = v3_norm(v3_rot_axis(gd, gr1, slide_tilt));
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
  // Looking steeply down needs working room in front of the carrier. The weapon and
  // both wrist targets move as one frame; the arm solve below then chooses collision-
  // free elbows instead of letting a vertical rifle pull them through the sternum.
  float aim_down = f_smoothstep(0.20f, 0.92f, -aimd.y);
  // The up band starts earlier than the down band (0.12 vs 0.20): at +40 deg the
  // butt is already at the cheek, so the working room has to arrive by then — filmed,
  // the mid-band was the cramped part while +75 read fine.
  float aim_up = f_smoothstep(0.12f, 0.85f, aimd.y);
  gun_o = v3_add(gun_o,
                 v3_add(v3_scale(F, 0.285f * aim_down * (1.0f - a->slide_s)),
                        v3_scale(Rc, -0.070f * aim_down * (1.0f - a->slide_s))));
  // Looking steeply UP needs the same room, on the other side of the face: without
  // it the near-vertical rifle stood ON the head with both forearms crossed over
  // the eyes. Forward and out to the strong side — the real technique drops the
  // stock off the shoulder pocket, so the whole frame also rides down a little.
  gun_o = v3_add(gun_o,
                 v3_add(v3_scale(F, 0.300f * aim_up * (1.0f - a->slide_s)),
                        v3_add(v3_scale(Rc, 0.075f * aim_up * (1.0f - a->slide_s)),
                               v3_scale(U, -0.060f * aim_up))));
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
  // the shoulder anchors remain on the torso. The springs TRANSLATE and (below) ROLL
  // the weapon frame; they never rotate the bore axis: gd stays exactly the caller's
  // yaw/pitch, i.e. weapon_t's physical barrel spring, so the rendered rifle, the
  // fire-anchor tracer and the round share one axis BY CONSTRUCTION. A decaying body
  // spring bending gd was a second muzzle direction over the authoritative one — and
  // anim_stage_fire_anchor re-solves from this live state, so it deflected round 2..n
  // of a burst by the presentation beat (recoil_side, authored in metres, was fed to
  // v3_rot_axis as radians). recoil_lift stays a spring with no pose consumer for the
  // same reason its table row is 0: angular rise belongs to weapon_t alone.
  {
    v3 gun_r = v3_cross(gd, U);
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
    // Place the contact behind the moving support shoulder. A base/hip-frame target
    // drifts across the torso while leg_yaw catches up with a newly entered slide;
    // shoulder-relative placement remains reachable and slide_pd makes it work for
    // every travel direction.
    // The support hand trails in the BODY frame, on its own anatomical side. Using
    // negative travel for "behind" is wrong on a lateral skid: in a left slide that
    // vector points back toward the pelvis and sends the forearm between both knees.
    // Body-back remains behind the hip in every travel direction, while the wider
    // side offset keeps the glove beyond the lead thigh.
    v3 want = v3_add(sho_pose[0],
                     v3_add(v3_scale(Rc, -0.26f), v3_scale(F, -0.14f)));
    // The wrist rides a fist-height over the floor, so the KNUCKLES are the
    // contact — this is the hand the player reads as dragging on the ground
    // (at 0.004 the old flat-palm wrap wound the fingers under the floor).
    want.y = base.y + 0.068f;  // an override, not an offset: R and F are horizontal
    // The hand plants hard while the slide is fast and LIFTS as it bleeds out
    // (first beat of the exit: hand up, then rock over the lead foot, then
    // rise) — a brace held at walking speed reads as glued.
    float plant = f_smoothstep(0.04f, 0.45f, a->slide_s);
    float bk2 = plant * f_clamp(0.90f + 0.40f * a->slide_v, 0.0f, 1.0f);
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
    float s = i ? 1.0f : -1.0f;
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
    // Preferred human bend planes for the near-vertical singularity. Projecting the
    // ordinary aim-relative poles around a downward bore can swap hemispheres from a
    // tiny pitch change; blend both elbows to stable outward/forward hints instead.
    if (aim_down > 0.001f && a->slide_s < 0.2f) {
      // The firing elbow belongs below the shoulder pocket, not straight out at
      // shoulder height. The support elbow stays wider because it must reach across
      // to the fore-end, but both hints retain a forward component so neither arm can
      // choose the torso-facing half of the near-vertical IK solution circle.
      v3 down_hint = i
          ? v3_norm(v3_add(v3_scale(Rc, 0.72f),
                           v3_add(v3_scale(F, 0.82f), v3_scale(U, -0.82f))))
          : v3_norm(v3_add(v3_scale(Rc, -1.18f),
                           v3_add(v3_scale(F, 0.88f), v3_scale(U, -0.30f))));
      pole = v3_norm(v3_lerp(pole, down_hint, aim_down));
    }
    // The same protection AIMING UP — the singularity is symmetric but the old code
    // only guarded the floor, so a skyward bore left both poles to the projection
    // lottery. Shooting up, the elbows drop UNDER the raised rifle (firing side
    // tight to the ribs, support side wider under the fore-end), and the forward
    // component again forbids the torso-facing hemisphere. armcheck's aim sweep
    // continues through +PITCH_MAX to hold this.
    if (aim_up > 0.001f && a->slide_s < 0.2f) {
      v3 up_hint = i
          ? v3_norm(v3_add(v3_scale(Rc, 0.85f),
                           v3_add(v3_scale(F, 0.70f), v3_scale(U, -0.95f))))
          : v3_norm(v3_add(v3_scale(Rc, -1.05f),
                           v3_add(v3_scale(F, 0.75f), v3_scale(U, -0.55f))));
      pole = v3_norm(v3_lerp(pole, up_hint, aim_up));
    }
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
    P->elb[i] = arm_elbow_solve(sho, wri, pole, a->prev_pose.sho[i],
                                a->prev_pose.elb[i], a->prev_pose.wri[i],
                                chest, Rc, s);
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
  // The side beat also rolls the frame ABOUT the bore (-1.4 rad per metre of side
  // travel — one tuned number, deliberately reusing the spring). A roll about gd
  // cannot move the bore axis, so the point-of-aim contract holds by construction.
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

// Solve the event-only live barrel without mutating the eased body pose. The requested
// yaw/pitch already includes gameplay recoil and spread; carry/slide state remains the
// body's real state, so a round cannot leave a magically shouldered rifle while the
// visible weapon is still low. Combat reads this slot before tracing and VFX reads the
// same slot during the event push.
static void anim_stage_fire_anchor(const anim_t *a, v3 pos, float yaw,
                                   float pitch, int wpn, int src) {
  anim_t shot = *a;
  // A shot is a firing pose even if the eased patrol carry was still lowering. The
  // caller already supplied the physical bore angle; BLOCKED selects the aligned
  // carriage without advancing any persistent animation state. The wall tuck is
  // zeroed for the same reason: the ballistic anchor is the ALIGNED rifle, and the
  // wall gate already refused any shot whose aligned muzzle path is not clear — a
  // decaying tuck bending the anchor's bore would be a second muzzle direction.
  shot.gunwall_s = 0.0f;
  // Same reason, the slide's muzzle-up carry: the anchor is the aligned rifle, so the
  // carry floor is taken at full rather than wherever the decaying latch stood.
  shot.fire_k = 1.0f;
  skel_solve(&shot, pos, yaw, pitch, wpn, GUN_POSE_BLOCKED, SKEL_POSE_ONLY);
  g_fire_anchor_pose = shot.pose;
  g_fire_anchor_src = src;
  g_fire_anchor_tick = g_tick;
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
  // Carry state: explicit combat intent raises the weapon; locomotion alone does not.
  // Auto-shouldering the long sniper on the first running tick made an otherwise calm
  // patrol carry traverse the firing-arm bend plane every time movement started.
  {
    float want = (ready || ads > 0.05f || a->fire_k > 0.01f) ? 1.0f : 0.0f;
    a->ready_s += (want - a->ready_s) * ease_k(want > 0.5f ? 20.0f : 4.5f);
  }
  // The wall tuck: fast in (a rifle meeting a wall is a contact), slower out
  // (recovering the carry is a deliberate move). The crisp predicate was published
  // by weapon_anim_tick; the eased value is what skel_solve's carriage consumes.
  {
    float want = a->gunwall_want ? 1.0f : 0.0f;
    a->gunwall_s += (want - a->gunwall_s) * ease_k(a->gunwall_want ? 14.0f : 7.0f);
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
  // A stationary crouch settles onto the firing-side knee. Speed fades that contact
  // before the gait controller starts stepping, so the same input becomes a mobile
  // two-foot crouch without a mode switch or a teleported joint.
  float kneel_speed = f_smoothstep(0.18f, 0.90f, a->spd);
  float kneel_want = crouch && grounded && !slide && a->slide_s < 0.05f
                   ? 1.0f - kneel_speed : 0.0f;
  float kneel_hz = kneel_want > a->kneel_lin ? 6.5f : 16.0f;
  a->kneel_lin += (kneel_want - a->kneel_lin) * ease_k(kneel_hz);
  a->kneel_s = smooth01(a->kneel_lin);
  // Slide blend is asymmetric: the drop-in snaps (~60 ms), standing back up
  // out of the skid is deliberately softer.
  float ksl = ease_k(slide ? 11.0f : 3.8f);
  a->slide_lin += ((slide ? 1.0f : 0.0f) - a->slide_lin) * ksl;
  a->slide_s = smooth01(a->slide_lin);
  // Ownership of the feet follows the get-up far enough to keep the deeply folded pose
  // collision-safe, then hands off while the body is visibly rising. Waiting until the
  // blend was almost zero left both ankles riding with the root for ~82 ticks — the
  // reported post-slide skating. The persistent knee-pole follower below bridges the
  // remaining pose blend while gait starts its explicit single swing.
  int slide_pose = slide || a->slide_s > ANIM_SLIDE_GAIT_HANDOFF;
  int slide_pose_exit = !slide_pose && a->was_slide_pose;
  a->was_slide_pose = slide_pose;
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

  int slide_enter = slide && !a->was_slide;
  if (slide_enter) a->slide_t = 0;  // fresh slide: entry envelope
  if (slide_enter) a->slide_knee_s = 1.0f;
  else a->slide_knee_s += ((slide ? 1.0f : 0.0f) - a->slide_knee_s) *
                          ease_k(slide ? 24.0f : 14.0f);
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
    float back_want = f_clamp(-fdot, 0.0f, 1.0f);
    v3 pose_dir = a->slide_dir;
    if (fdot < 0) {
      pose_dir.x -= 2.0f * fdot * fx;
      pose_dir.z -= 2.0f * fdot * fz;
    }
    pose_dir = v3_norm(pose_dir);
    if (slide_enter || v3_dot(a->slide_pd, a->slide_pd) < 1e-6f) {
      a->slide_pd = pose_dir;
      a->slide_bk = back_want;
    } else {
      a->slide_pd = v3_norm(v3_lerp(a->slide_pd, pose_dir, ease_k(11.0f)));
      a->slide_bk += (back_want - a->slide_bk) * ease_k(11.0f);
    }
  }
  if (v3_dot(a->slide_pd, a->slide_pd) < 1e-6f) a->slide_pd = (v3){{fx, 0, fz}};
  // Latch one lead leg for the WHOLE slide. Straight-ahead used to blend to 0.5, which
  // produced two identical half-extended legs; allowing steering to flip the latch
  // later made both legs pass through each other. A new slide may choose a new side.
  if (slide_enter) {
    float lat = a->slide_pd.x * rx + a->slide_pd.z * rz;
    a->slide_ld = fabsf(lat) > 0.18f ? (lat > 0.0f ? 1.0f : 0.0f)
                                     : (a->phase >= 0.5f ? 1.0f : 0.0f);
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

  // Gait phase. When the eased get-up finally releases its feet, begin from one
  // explicit single swing. Reusing the phase left behind on slide entry can put both
  // feet inside overlapping swing windows, lifting both boots just as the pelvis
  // reaches standing height — the photographed bow-legged, floating first stride.
  if (slide_pose_exit) {
    // At high speed reach protection can expand the swing window beyond half a
    // cycle. 0.38 begins with only the lead foot eligible instead of placing both
    // feet in that overlap on the first post-slide tick.
    a->phase = 0.38f;
    a->bob_lag = 0.0f;
    for (int i = 0; i < 2; i++) {
      a->foot[i].from = a->foot[i].to = a->foot[i].pos;
      a->foot[i].t = 1.0f;
    }
  }
  int moving = grounded && !slide_pose && a->spd > 0.35f;
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
  } else if (slide_pose) {
    // Slide: one heel-first leg extends along the mirrored travel direction while the
    // other foot stays under the hips, letting its knee become the ground contact.
    // Both feet still skid with the body; only their body-relative targets differ.
    v3 pd = a->slide_pd;
    v3 slide_body_right = {{rx, 0.0f, rz}};
    // This axis belongs to the slide direction, not the lagging hip heading. On a
    // newly entered lateral slide that distinction is what keeps the folded knee on
    // its own side of the body.
    float kf = ease_k(22.0f);
    float foot_slide_k = slide ? 1.0f : a->slide_s;
    float syaw = a->leg_yaw +
                 wrap_pi(atan2f(pd.x, -pd.z) - a->leg_yaw) * foot_slide_k;
    for (int i = 0; i < 2; i++) {
      float s = i ? 1.0f : -1.0f;
      float ld = i ? a->slide_ld : 1.0f - a->slide_ld;  // how much THIS leg leads
      float back_pose = f_smoothstep(0.18f, 0.72f, a->slide_bk);
      // In a rear-half slide the folded foot comes in front of the pelvis and carries
      // the pose on its sole. Keeping it under the hip collapses the two-bone axis to
      // a few centimetres, where any pelvis rise spins the knee around a huge circle.
      float folded_along = -0.14f + 0.62f * back_pose;
      float slide_along = ld * (0.48f + 0.20f * a->slide_v) +
                          (1.0f - ld) * folded_along;
      // As the pelvis rises, converge both skid contacts into an ordinary staggered
      // stance while they still ride with the body. Gait receives reachable planted
      // feet instead of two slide-wide endpoints and may start on the following tick.
      float stand_along = i ? -0.050f : 0.070f;
      float along = stand_along + (slide_along - stand_along) * foot_slide_k;
      float half = 0.14f + (0.19f - 0.14f) * foot_slide_k;
      float footy = ANIM_ANKLE_Y;
      v3 tgt = {{base.x + pd.x * along + slide_body_right.x * s * half,
                 base.y + footy,
                 base.z + pd.z * along + slide_body_right.z * s * half}};
      // Skid HORIZONTALLY only.
      // A small anatomical toe-out keeps the long leg from reading as an internally
      // twisted shin while the whole foot frame turns into a lateral slide.
      foot_ride(&a->foot[i], base, tgt, (v3){{vel.x, 0.0f, vel.z}},
                syaw + s * ANIM_TOE_OUT * foot_slide_k, kf);
    }
  } else if (a->kneel_s > 0.001f) {
    // Idle crouch: support foot forward, firing foot folded behind. The ankle targets
    // stay on the floor and IK chooses the firing knee's ground-side solution above.
    // As kneel_s fades these targets converge on the ordinary crouch stance before the
    // step clock takes ownership.
    float kf = ease_k(12.0f);
    for (int i = 0; i < 2; i++) {
      float s = i ? 1.0f : -1.0f;
      float authored = i ? -0.205f : 0.240f;
      float along = authored * a->kneel_s;
      float half = 0.14f + (i ? -0.020f : 0.0f) * a->kneel_s;
      v3 tgt = {{base.x + rx * s * half + fx * along,
                 base.y + ANIM_ANKLE_Y,
                 base.z + rz * s * half + fz * along}};
      foot_ride(&a->foot[i], base, tgt, vel, a->leg_yaw, kf);
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
        // A PLANTED foot rests on the ground under ITSELF. Vertical only, eased: a
        // warp onto an edge, or the body drifting until a boot hangs past a curb,
        // may not step-change the sole. On flat ground want == pos.y exactly, so
        // this adds a literal zero and the ordinary gait cannot drift.
        {
          float gy = foot_ground_y(base, f->pos.x, f->pos.z);
          float wanty = fmaxf(gy + ANIM_ANKLE_Y,
                              base.y + ANIM_ANKLE_Y - ANIM_FOOT_DROP);
          f->pos.y += (wanty - f->pos.y) * ease_k(12.0f);
        }
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
        // THE PLANT LANDS ON ITS OWN GROUND: a step over a curb's edge aims at the
        // floor actually under the landing point, bounded by what the leg can cover
        // (ANIM_FOOT_DROP — past that the boot dangles and the sink solver carries
        // what it can). base.y exactly on flat ground.
        {
          float gy = foot_ground_y(base, to.x, to.z);
          to.y = fmaxf(gy + ANIM_ANKLE_Y,
                       base.y + ANIM_ANKLE_Y - ANIM_FOOT_DROP);
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
          // The arc rides from the lift-off height to the landing height (they can
          // differ across a curb); on flat ground both equal base.y + ankle and the
          // old expression falls out exactly.
          f->pos.y = f->from.y + (f->to.y - f->from.y) * e +
                     sinf(F_PI * e) * lift;
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
