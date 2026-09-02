// =============================================================================
// sim/player.inc  —  player sim at the fixed 120 Hz tick: movement, lean,
// slide entry, crouch.
// Proof: pos, lean, parity. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: lean_eye_off/lean_eye_drop are the one derivation of the lean's
// two components (lateral + the 91 mm drop), shared with the camera, the
// skeleton and ent_trace_box; both are exactly 0 at lean 0.
// =============================================================================
// ---------------------------------------------------------------------------
// Player sim — fixed 120 Hz ticks, deterministic. Quake-style ground
// friction + acceleration, limited air control, AABB collision resolved per axis
// against the solids plus the y=0 floor plane.
// ---------------------------------------------------------------------------

// TICK_HZ / TICK_DT are defined above the audio section — the synth needs the
// tick rate to time the reloads, and one number cannot live in two places.
#define PLAYER_RADIUS  0.35f
// How far out from a solid's face a bot starts steering ALONG it rather than
// into it, and the inner band where the figure is wider than PLAYER_RADIUS and
// an elbow would otherwise be inside the wall. See the projection in bot_tick.
#define BOT_WALL_BAND  0.42f
#define BOT_WALL_ELBOW 0.06f
#define HEIGHT_STAND   1.80f
#define HEIGHT_CROUCH  1.20f
#define EYE_STAND      1.62f
#define EYE_CROUCH     1.00f
#define JUMP_BUFFER_TICKS 8   // ~66 ms: a jump pressed just before landing still fires
#define EYE_LERP_RATE  12.0f
#define SENSE_RAD_PER_COUNT 3.8397e-4f  // 0.022 deg/count * pi/180
#define PITCH_MAX      1.55f  // rad (~88.8 deg): view/aim pitch limit, every clamp in the look/aim pipeline
#define ENT_CHEST_OFF  0.28f  // aim point sits this far below the eye (upper chest)
#define GRAVITY        22.0f  // m/s^2; jump feel is tuned via JUMP HEIGHT instead
#define SLIDE_START_SPEED    0.80f  // x run speed: min speed to enter a slide
#define SLIDE_END_SPEED      1.15f  // x crouch speed: below this a slide settles into a crouch
#define SLIDE_STEER_RATE     2.6f   // /s: slide + air steering redirect toward the wish dir
#define SLIDE_COOLDOWN_TICKS 96    // 0.8 s between slide boosts: no perpetual hop-boost chains
#define FOV_KICK_RATE        9.0f   // /s: smoothing of the slide FOV kick
#define LAND_DIP_SCALE   0.015f // m of eye dip per m/s of landing impact speed
#define LAND_DIP_MAX     0.12f  // hardest landings bottom out here
#define LAND_DIP_RECOVER 15.0f  // /s: spring back, ~95% recovered in 200 ms

// --- Lean ------------------------------------------------------------------
// A lean is ONE geometric fact — the torso bends sideways about the waist by LEAN_ANGLE
// while the hips counter-shift LEAN_HIP the other way — and everything else is derived
// from it: how far the first-person eye travels, how far it sinks, how much the view
// rolls, and how far the third-person head has to end up if the two views are to show
// the same man.
#define LEAN_ANGLE   0.54f    // rad (31 deg) of lateral spine bend at full lean
#define LEAN_ARM     0.64f    // m from the bend (waist, ~L3) up to the eye
#define LEAN_HIP     0.05f    // m the hips push out the OTHER way as a counterweight
// The neck gives back half the torso's roll: a leaning shooter keeps their eyes
// nearer level than their chest, and a full 31 degrees of camera roll is
// nauseating rather than tactical.
#define LEAN_VIEW_K  0.50f
// How much of the head's travel the TORSO hitbox takes with it (the boots take
// none, the shoulders nearly all of it, and one box has to average that).
#define LEAN_BODY_K  0.55f
// Fraction of the bend the PELVIS takes.
#define LEAN_PELV_K  0.30f
#define LEAN_IN_RATE  11.0f   // /s toward the held command (~0.27 s to full)
#define LEAN_OUT_RATE 14.0f   // /s back to upright — recovering is faster than committing
#define LEAN_CLR_RATE 16.0f   // /s the wall clearance eases, so a corner cannot pop the head
#define LEAN_MARGIN  0.20f    // m of air the eye keeps between itself and a wall
#define LEAN_LOOK    0.15f    // s of velocity the clearance probe looks ahead
#define LEAN_LOOK_MAX 0.60f   // ...capped, so speed cannot anticipate a wall a room early
// Leaning is a stationary/walking move.
#define LEAN_SPD_LO  0.55f
#define LEAN_SPD_HI  0.90f

// Eye travel and drop at lean amount l, both DERIVED from the bend above so the camera
// and the skeleton can never disagree: 0.28 m out and 0.09 m down at full lean.
static float lean_eye_off(float l)  { return LEAN_ARM * sinf(l * LEAN_ANGLE) - LEAN_HIP * l; }
static float lean_eye_drop(float l) { return LEAN_ARM * (1.0f - cosf(l * LEAN_ANGLE)); }

// ACCEL/STOP TIME sliders are "ms to cover 95% of the speed gap" — three time
// constants of the exponential approach the ground move performs each tick.
static float mv_alpha(float ms) {
  return 1.0f - expf(-3.0f * TICK_DT * 1000.0f / ms);
}

// The same first-order filter parameterized as a RATE per second instead of a settling
// time — the form 20-odd presentation springs want.
static float ease_k(float rate) { return 1.0f - expf(-rate * TICK_DT); }

// Weapon behavior constants shared by every weapon — per-weapon structure is
// in WPN_DEF, per-weapon feel in g_cfg.wp.
#define GUN_SWITCH_TICKS 36      // 0.3 s weapon raise on swap
// HEAT IS A BURST'S PRICE ON THE SHOOTER AND ON THE ROUND: gun_shoot adds a weapon's
// HEAT/SHOT per round up to this ceiling and gun_recover bleeds it between bursts. Two
// readers: the shoulder's grip correction below (gun_sway_tick), fully loaded — and
// stopped growing — at a quarter of it, and gun_spread's cone, which takes the raw
// accumulation straight into the round's spread in degrees. Both belong to the process
// that simulates the shooter; the burst's VISUAL fade is the round's index, not this
// (recoil_kick_damp, sim/skeleton.inc).
#define GUN_HEAT_CAP     0.6f    // ceiling on a burst's accumulated heat
#define GUN_GRIP_LOAD_HEAT 0.22f // heat where the shoulder control loop is fully loaded
#define GUN_GRIP_P_HIP    0.48f  // deg: loose-shoulder correction amplitude
#define GUN_GRIP_P_ADS    0.27f  // deg: cheek weld braces, but cannot freeze, the rifle
// THE CONE IS WHAT A STANCE COSTS THE ROUND, and only a stance: standing, grounded,
// aimed and cold it is exactly zero, so the first ADS round lands on the dot. Airborne
// is one flat penalty because nothing about a jump is aimable, and heat is the burst's
// own price, paid on top of the pattern's climb rather than instead of it. The two
// per-weapon halves (HIP SPREAD, MOVE SPREAD) are config keys; these two are not,
// because there is no weapon whose jump or whose barrel heat is a different sport.
#define GUN_AIR_SPREAD   2.5f    // deg of cone while both feet are off the ground
#define GUN_HEAT_SPREAD  0.6f    // deg of cone per unit of accumulated heat
// THE SHOULDER'S OWN TIME CONSTANT: how fast the barrel reaches a pattern row it has
// just been given. Fast enough that a round leaves along ~87% of the row the previous
// one set at 750 rpm, slow enough that the biggest authored step arrives as a fifth of
// a degree in the tick of the shot instead of the whole of it. The way BACK is not
// this number — that is RECOVERY, which the player tunes.
#define GUN_REC_RISE_TAU 0.040f  // s to 63% of a newly set pattern row
#define GUN_REC_HOLD_PAD 4       // ticks of slack the standing target keeps past a round
#define GUN_PUNCH_SHOT   0.4f    // deg of visual punch per shot (x VIEW PUNCH)
// THE PUNCH IS A SPRING AND THE AUTHORED NUMBER IS ITS PEAK. These constants were
// solved on the DISCRETE recurrence, not the continuous one, and that is not academic:
// at TICK_DT = 1/120 a continuous zeta of 0.55 needs c = 69, and stepping that
// explicitly multiplies velocity by 0.43 per tick — 0.113 deg of peak where 0.400 was
// asked for.
#define GUN_PUNCH_K      1200.0f
#define GUN_PUNCH_C      32.0f
#define GUN_PUNCH_V      79.0f   // deg/s of spring velocity per deg of peak
// A shot ROLLS the view, and roll is the one punch axis that is free: it pivots about
// the view axis, so the crosshair pixel does not move and the point-of-aim contract
// holds BY CONSTRUCTION. Measured before this, the entire on-screen event of an AR
// round was an 8 px vertical translation of which 5 px was permanent recoil climb — 3
// px of transient for the loudest thing the player does.
#define GUN_ROLL_K       0.85f   // deg of roll per deg of punch
#define DEG2RAD          0.017453293f

// ABSOLUTE per-round bore offsets {yaw, pitch}, normalized to the climb's authored
// 1.0: round n of a burst asks for base aim + kick_side*yaw, base aim + kick_up*pitch.
// Not a spring and not a place either — the shot writes this row as the shoulder's
// TARGET and the barrel follows it (gun_recoil_set / gun_recoil_step, sim/combat.inc),
// so the offset a round actually leaves along is the follower's position, a fraction
// of the previous row. Rounds 0-2 are tight (tap and burst fire are precise), 3-9
// climb, and 10+ CREEP: the crown keeps rising about a hundredth and a half per round
// instead of sitting on a ceiling, so a magazine ends about a quarter above the
// authored top and the player who wants the last ten rounds keeps paying for them.
// Learnable like a Counter-Strike pattern, and controlled the same way: pull down.
static const float GUN_PATTERN[WPN_COUNT][32][2] = {
  [WPN_AR] = {
    { 0.00f, 0.00f}, { 0.03f, 0.05f}, {-0.06f, 0.12f}, { 0.10f, 0.22f},
    { 0.22f, 0.33f}, { 0.33f, 0.45f}, { 0.42f, 0.56f}, { 0.32f, 0.67f},
    { 0.15f, 0.76f}, {-0.08f, 0.84f}, {-0.35f, 0.93f}, {-0.60f, 0.95f},
    {-0.85f, 0.96f}, {-1.00f, 0.98f}, {-0.95f, 0.99f}, {-0.75f, 1.01f},
    {-0.45f, 1.02f}, {-0.10f, 1.04f}, { 0.25f, 1.05f}, { 0.55f, 1.07f},
    { 0.78f, 1.08f}, { 0.88f, 1.10f}, { 0.80f, 1.11f}, { 0.58f, 1.13f},
    { 0.28f, 1.14f}, {-0.05f, 1.16f}, {-0.38f, 1.17f}, {-0.62f, 1.19f},
    {-0.70f, 1.20f}, {-0.58f, 1.22f}, {-0.35f, 1.23f}, {-0.10f, 1.25f},
  },
  [WPN_SR] = {  // the bolt outlasts recovery: every round is round 0 unless spammed
    { 0.15f, 1.00f}, { 0.25f, 1.00f}, {-0.20f, 1.00f}, { 0.10f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
    { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f}, { 0.15f, 1.00f},
  },
};

// ONE bounds rule for a weapon id, because the echo path carries one off the wire and
// it reaches BOTH the pattern table and the tuning row beside it. Every reader of
// either asks here first; nobody spells the comparison a second time.
static int wpn_clamp(int wpn) { return wpn < 0 || wpn >= WPN_COUNT ? WPN_AR : wpn; }

// ...and the shot index is a wire quantity too, so the row it selects wraps rather
// than trusts. The shot's own set, the roll sign and the presentation side sample all
// come through here.
static const float *gun_pattern(int wpn, int shot) {
  return GUN_PATTERN[wpn_clamp(wpn)][(uint32_t)shot & 31u];
}

// The pattern step's SIGN, and the ONE spelling of its tie-break: a step authored at
// the table's zero pulls the muzzle nowhere, so it has no side to roll or throw
// toward and the caller's own coin decides. Both the view roll and the presentation
// side sample read the sign of the same yaw column, and they may not disagree.
static float gun_pattern_sign(int wpn, int shot, int tie) {
  float p = gun_pattern(wpn, shot)[0];
  return p > 0.001f ? 1.0f : p < -0.001f ? -1.0f : (tie ? 1.0f : -1.0f);
}

typedef struct {
  int   cur;             // WPN_AR / WPN_SR — which weapon is in the hands
  int   ammo[WPN_COUNT]; // per-weapon rounds; transient state below is shared
  // PER WEAPON, like ammo: the cooldown is the weapon's mechanism cycling (the SR's 1.2
  // s bolt), not the shooter's readiness.
  int   cool[WPN_COUNT]; // ticks until that weapon can fire again
  int   idx;             // spray pattern index: the round's number in the burst
  int   trig_off;        // ticks the trigger has been released, re-arms idx
  int   reload_t;        // ticks of reload remaining, 0 = not reloading
  int   reload_tac;      // ...and whether it started with a round still up
  int   switch_t;        // ticks of weapon raise remaining after a swap
  float prev_switch;     // last tick's raise fraction, for interpolation
  // Angular offset of the physical barrel from the calm aim line, in degrees; camera,
  // rendered weapon and hitscan all consume the same two angles. THE PATTERN IS THE
  // TARGET AND THE BARREL IS THE FOLLOWER: the shot writes rec_tp/rec_ty (the row's
  // degrees) and gun_recoil_step walks rec_p/rec_y toward them every tick, so the
  // offset is continuous and there is still no velocity to carry — a shoulder, not a
  // spring. rec_hold is how many ticks the target stands without another round behind
  // it; it is what releases the crown for a viewer who only ever sees the echoes.
  float rec_p, rec_y, prev_rec_p, prev_rec_y;
  float rec_tp, rec_ty;
  int   rec_hold;
  // The other half of the physical barrel: the hands trail a look change and carry
  // breathing/stride motion. Degrees, like recoil. This belongs to weapon_t rather
  // than the local-only vm_t because the server must trace the same moving barrel the
  // shooter sees, and prediction must restore it across rollback.
  float sway_p, sway_y, prev_sway_p, prev_sway_y;
  float sway_phase, sway_clock;
  float sway_last_yaw, sway_last_pitch;
  unsigned char sway_primed;
  float heat;            // burst's price on grip correction AND round spread, decays between bursts
  // THE DRY CLICK IS LATCHED, NOT EDGE-GATED. It used to be pushed only on a press
  // edge, so on a FULL-AUTO weapon — where prev_held is 1 on every tick of the burst —
  // the branch was unreachable and the single commonest way to run out of ammo made no
  // sound at all (measured: `+fire; wait 320` over a whole magazine printed zero
  // dryfire events).
  int   dry_done;
  float punch, prev_punch;                     // deg, visual kick only
  // ...and its VELOCITY, because the punch is a spring rather than a decay. One-pole
  // decay ran in gun_recover, i.e.
  float punch_v;
  // The ROLL half of the same impulse, on its own spring so it can be signed
  // off the spray pattern independently of the pitch. Render-only, like punch.
  float punch_roll, prev_punch_roll, punch_roll_v;
  float ads_lin, ads_s, prev_ads_s;            // raw ramp + smoothstepped
  // THE TWO CARRIAGE INTENTS, published where each is DECIDED so that one copy serves
  // both readers — the pose solver and the wire. ads_want is what gun_ads was last
  // asked for (the button AFTER its busy gate, not the raw press); ready_want is
  // "this soldier is fighting rather than carrying", written by each producer at its
  // own anim_tick call from its own predicate.
  // They are INTENTS, never blends: the eased values (ads_s here, ready_s in anim_t)
  // are presentation, and presentation does not cross the wire — a client re-solves
  // it from exactly these, at its own tick rate, through the same gun_ads and the
  // same ease. See net_ent_from_live.
  unsigned char ads_want, ready_want;
} weapon_t;

// FULL MAGAZINES, EVERY WEAPON. There is no reserve pool, so a spawn (and every
// proof that stages one) fills both mags — seventeen copies of this loop is
// seventeen chances to forget the second weapon when a third arrives.
static void wpn_fill_mags(weapon_t *w) {
  for (int i = 0; i < WPN_COUNT; i++) w->ammo[i] = WPN_DEF[i].mag;
}

// Viewmodel animation springs — all ticked at 120 Hz with prev_ twins for render
// interpolation, so the gun's motion is deterministic and screenshot-stable.
typedef struct {
  float sway_p, sway_y, prev_sway_p, prev_sway_y;  // rad: look-velocity lag
  float lag_x, lag_y, lag_z;                       // m: inertia vs acceleration
  float prev_lag_x, prev_lag_y, prev_lag_z;
  float bob_x, bob_y, prev_bob_x, prev_bob_y;      // m: walk figure-8
  float bob_phase, bob_amp;
  // A shot is three presentation springs, not one scalar doing three jobs. Back is
  // metres along the bore toward the shoulder, lift is radians of delayed muzzle
  // rise, and side is metres of tiny signed lateral travel. Each has a prev twin so
  // the viewmodel remains sub-tick interpolated.
  float kick_back, kick_back_v, prev_kick_back;
  float kick_lift, kick_lift_v, prev_kick_lift;
  float kick_side, kick_side_v, prev_kick_side;
  float roll, prev_roll;                           // rad: slide/strafe lean
  float lean_lag, prev_lean_lag;                   // trails player_t.lean: the
                                                   // weapon is on an arm, not
                                                   // bolted to the helmet
  float slide_s, prev_slide_s;                     // eased slide 0..1: the FP
                                                   // tuck-and-cant pose blend
  float gunwall, prev_gunwall;                     // eased wall tuck 0..1: the FP
                                                   // half of anim_t.gunwall_s
  float last_yaw, last_pitch;
  float last_eye;                                  // for the crouch/stand settle
  v3    last_vel;
} vm_t;

typedef struct {
  v3    pos, vel;        // pos = feet center
  v3    prev_pos;
  float yaw, pitch;
  float eye, prev_eye;   // eye height above feet, smoothed
  int   crouched, grounded, prev_grounded;
  int   sliding;
  int   slide_cd;        // ticks until the next slide may boost again
  int   slide_queued;    // crouch was pressed airborne: boost the landing slide
  int   jump_buf;        // ticks a buffered jump press stays valid
  // Metres of LEG TUCK currently folded into pos.y.
  float tuck_y;
  float fov_kick, prev_fov_kick;  // 0..1 slide FOV widening, smoothed
  float land_dip, prev_land_dip;  // m the eye is compressed by a landing
  // Lean: the eased COMMAND (-1 left .. +1 right), the eased wall CLEARANCE
  // (0..1, how much of that command the geometry actually allows) and their
  // product, which is the only one presentation and hit detection may read.
  float lean_in, lean_clr;
  float lean, prev_lean;
  int   prev_held[IN_COUNT];
  int   shots;           // total bullets fired
  int   alive, hp;       // deathmatch state
  int   respawn_t;       // dead: ticks until respawn
  // THE PLAYER'S OWN BODY FLASHES TOO. Every bot got a hit flash from the day
  // ent_damage was written and the player's third-person figure was drawn with a
  // hard 0.0 — the identical parity gap the file already closed for anim_hit
  // ("the player does not flinch" was a parity gap, invisible in exactly the
  // view where a player would see it). Same two fields, same helper, same decay.
  int   flash, flash_head;
  weapon_t wp;
  vm_t  vm;
} player_t;
