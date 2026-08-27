// =============================================================================
// sim/bots_mind.inc  —  hearing/noise, belief, LOS/arc, spawn, ragdoll sim,
// plus ent_trace/ent_trace_box (the shared hitbox test).
// Proof: bothear, botmemory, rag*. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: two cones and one ear — BOT_FOV_COS acquires, BOT_KEEP_COS
// keeps; bot_belief_aimpoint is the one chokepoint that displaces a
// remembered target, bot_belief_seed the one writer of a fresh sighting.
// THE HITBOX STANDS IN THE BODY'S OWN FRAME (ent_trace_box), not world axes.
// =============================================================================
// ---------------------------------------------------------------------------
// Bots + deathmatch — up to MAX_BOTS AI opponents sharing the player's world
// collision and physics helpers. All bot randomness stays on g_rng_bot, so player-only
// interactions reproduce under a fixed seed.
// ---------------------------------------------------------------------------

#define BOT_PATROL_PACE   0.55f   // x RUN SPEED: a bot that has seen nobody walks
#define BOT_TOPPLE_TICKS  40      // death fall duration
// 12, not 10, AND THE CURVE IS SQUARED. The wash used to be a LINEAR ramp capping at
// 0.16, i.e. a 16% lerp toward white whose mean over its whole life was 0.08 — on a
// target that is 6442 px at 8 m and 112498 px at 1.5 m, that is the largest surface in
// the frame the player is aiming at, drawn with the weakest cue in the file.
#define BOT_FLASH_TICKS   12
#define FLASH_PEAK        0.38f
#define FLASH_HEAD_PEAK   0.52f   // a headshot is hotter as well as redder
static float ent_flash01(int flash, int head) {
  if (flash <= 0) return 0.0f;
  float f = (float)flash / (float)BOT_FLASH_TICKS;
  return f * f * (head ? FLASH_HEAD_PEAK : FLASH_PEAK);
}
#define BOT_FOV_COS       0.50f   // ~120 deg vision cone (ACQUIRING a new target)
// ...and ~210 deg for KEEPING one.
#define BOT_KEEP_COS      (-0.26f)
#define BOT_HEAR_WALL     0.55f   // blocked sound remains useful, but softened
#define BOT_HEAR_STEP_TICKS 72
#define BOT_HEAR_SHOT_TICKS 180
// The startle turn: an ABSOLUTE rate in rad/s, deliberately NOT a multiple of the
// skill's tracking rate.
#define BOT_ALERT_TURN    9.0f
// Sound localisation is a bearing, not a coordinate.
#define BOT_HEAR_ERR      0.16f
#define BOT_HEAR_ERR_MAX  2.6f
// THE DEADLOCK COMMITMENT. `pin` counts ticks of wanting to move and not moving; past
// this many the bot is not manoeuvring, it is leaning on a corner.
#define BOT_DEADLOCK_PIN  20
#define BOT_PUSH_HP       60
#define BOT_PUSH_DIST     18.0f
// AFTER A FIGHT a person does not go straight back to strolling.
#define BOT_ALERT_TICKS   3000
#define BOT_ALERT_PACE    0.78f
#define BOT_ALERT_ARRIVE  2.0f
#define BOT_THREAT_TICKS 150
#define BOT_SWAP_COOLDOWN_TICKS 240
#define BOT_TARGET_EPS 0.001f
#define BOT_SEARCH_EPS 0.001f
#define BOT_TARGET_HOLD_BONUS 450.0f
#define BOT_TARGET_HIDDEN_BREAK_MARGIN 250.0f
#define BOT_SNIPER_ENTER_DIST   24.0f
#define BOT_AR_RETURN_DIST      16.0f
#define BOT_SNIPER_ENTER_SPEED   2.5f
#define BOT_AR_RETURN_SPEED      5.0f

// One side-effect-free source for physical step reach, shared by the real animation
// contact and the hearing proof.
static float bot_step_noise_radius(float speed, float crouch) {
  return 11.0f * f_clamp(speed / g_cfg.mv[MV_RUN], 0.0f, 1.0f) *
         (1.0f - 0.65f * crouch);
}

// --- death kinematics ------------------------------------------------------
// WHERE the round landed used to enter nowhere: a corpse was thrown along the
// shooter->victim horizontal and spun about the perpendicular, scaled by damage alone,
// so a right-shoulder, a left-shoulder, a gut and a head kill produced the same fall to
// three decimals.
#define DEATH_PIV_Y     (HEIGHT_STAND * 0.42f)  // 0.756 m — the hip
#define DEATH_SPIN_BASE  1.55f    // rad/s a dead-centre hit gets: "he just drops"
// THE DIRECTIONAL TERMS ARE PER UNIT FORCE — the lever alone, in metres — and only the
// THROW is scaled by damage.
#define DEATH_SPIN_K     2.45f    // rad/s per metre of vertical lever
#define DEATH_YAW_K     10.00f    // ...and per metre of LATERAL lever. Bigger,
                                  // because a body's longitudinal radius of gyration
                                  // (~0.21 m) is a third of its transverse one (~0.54
                                  // m): the same torque spins it about its own axis far
                                  // more easily than it tips it over.
#define DEATH_ROT_W(IMP) (0.78f + 0.22f * (IMP) / DEATH_IMP_MAX)
#define DEATH_YAW_MAX    3.40f    // rad/s guard, not a knob — the widest lever
                                  // the hitboxes admit is 0.30 m
#define DEATH_YAW_FRIC   4.5f     // 1/s, scaled by how FLAT the body is: 640 ms
                                  // on its boots (it pivots freely), 164 ms once down
                                  // (its whole length is on the floor).
#define DEATH_PITCH_K    0.45f    // how much of the shot's own pitch goes into
                                  // the throw: a round fired down from a crate
                                  // drives the body into the floor
// THE THROW IS NOT PROPORTIONAL TO DAMAGE, and that was the single number that
// kept the feature sniper-only. `dmg * 0.050` gave an AR body shot 1.30 m/s —
// a shove — and every bot in the game carries an AR (`bot_spawn` hard-wires
// it), so the overwhelming majority of kills in a match got no launch at all
// while the one weapon the player rarely fires got all of it. A killing round
// is a killing round: BASE is what any kill is worth and the per-damage term is
// what still separates a 150-damage rifle from a 26-damage one.
#define DEATH_IMP_BASE   6.20f    // m/s every kill is worth. MEASURED AND LEFT
                                  // ALONE: raising it to 6.6/7.1 with the skid sparks
                                  // looked better in flight and took ragsoak's rest_pen
                                  // from 0.093 to 0.219/0.264 — bodies thrown harder
                                  // come to rest INSIDE geometry.
#define DEATH_IMP_K      0.029f   // ...plus this per point of damage
#define DEATH_IMP_MAX   12.0f     // m/s ceiling, and it BINDS: a headshot arrives
                                  // at ent_damage already multiplied (an SR head hit is
                                  // dmg=300), so the range is AR body 6.95 / AR head
                                  // 9.25 / SR body 10.55 / SR head 17.88 clamped to
                                  // 12.0.
#define DEATH_LIFT       2.65f    // m/s of launch straight up...
#define DEATH_LIFT_K     3.40f    // ...plus this at full impulse. GRAVITY is
                                  // 22 here, so an apex is v^2/44 and the old 1.05-1.32
                                  // m/s peaked at 3-4 cm: the body never left the floor
                                  // and the tumble had nowhere to happen. 3.8-4.8 m/s
                                  // buys 0.33-0.52 m and 0.35-0.44 s of air, which is
                                  // the window an airborne tumble needs to be legible
                                  // at all.
#define DEATH_AIR_SPIN   5.20f    // rad/s of extra tumble at full impulse (was
                                  // 4.20).
#define DEATH_SPIN_MAX   8.00f    // rad/s guard: past ~1 rev/s a tumbling body
                                  // stops reading as a body
#define DEATH_AIR_FRIC   1.20f    // 1/s the tumble bleeds in FLIGHT, and the
                                  // reason is presentation, not aerodynamics: this pose
                                  // is rigid — the limbs are a slack sprawl solved once
                                  // and then rotated as a unit — so the body passing
                                  // THROUGH vertical is a tumble and the body sitting
                                  // at vertical for six frames is a pole standing on
                                  // its head.
#define DEATH_KEEP       0.55f    // share of the victim's OWN velocity the
                                  // corpse keeps.
#define DEATH_DRAG       0.35f    // 1/s of air drag, so the flight is bounded
#define DEATH_TILT_MAX   1.52f    // rad: flat on the ground. Derived below as
#define DEATH_FLAT_SHORT (F_PI_2 - DEATH_TILT_MAX)  // ...short of dead
                                  // flat, because a body rests on its shoulder
                                  // blades and its heels, not on a plane.
#define DEATH_TOPPLE_A   5.20f    // rad/s^2 gravity topple — GROUNDED ONLY now
#define DEATH_TILT_FRIC  3.40f    // 1/s the FLOOR takes the tumble out, scaled
                                  // by flatness exactly like the yaw friction.
#define DEATH_LAG_RATE   0.11f    // per tick: how fast the limbs catch the trunk
#define DEATH_LAG_K      0.85f    // ...and how much of the lag arc they travel
#define DEATH_SKID_MU   11.00f    // m/s^2 of COULOMB ground friction (was
                                  // 12.00). A body grinds further before it
                                  // stops, which is what the spark emitter is
                                  // measured against, and still stops inside a
                                  // body length or two. A purely viscous decay
                                  // is an asymptote — it creeps down to the
                                  // cut-off and then snaps to a stop, which is
                                  // seconds of a corpse ice-skating with a
                                  // frozen silhouette. Real friction is a
                                  // constant deceleration.
#define DEATH_LAND_SPIN  0.20f    // ...and the share of the TUMBLE that survives
                                  // first ground contact.
#define DEATH_BOUNCE_MIN 2.40f    // m/s of descent below which a body just lands
#define DEATH_BOUNCE_E   0.42f    // vertical restitution of the skip... (was
                                  // 0.34: the second contact is where the skid sparks
                                  // read best, so the skip earns its keep twice now.)
#define DEATH_BOUNCE_H   0.74f    // ...and the horizontal it keeps through it
#define DEATH_BOUNCE_SPIN 0.42f   // rad/s of tumble per m/s of arrival
#define DEATH_WALL_MIN   2.00f    // m/s into a wall before it counts as a slam
#define DEATH_WALL_E     0.42f    // horizontal restitution off it: a body that
                                  // STOPS at cover is a body that fell over near it.
#define DEATH_WALL_SPIN  0.55f    // rad/s of tumble per m/s the wall took
#define DEATH_SLAM_SPIN  0.30f    // ...of the old rotation that survives it
#define DEATH_REST_VAR   0.30f    // rad of per-kill variation in HOW FAR SHORT
                                  // of dead flat a body comes to rest — 0 to 17 degrees
                                  // on top of DEATH_FLAT_SHORT's own 3.
#define DEATH_PUSH_T     6        // ticks the horizontal launch is spread over
                                  // — 50 ms.
#define DEATH_COM_Y      0.85f    // m up the standing skeleton the trunk's mass
                                  // sits — pelvis to chest.
#define DEATH_LEDGE_A    7.0f     // m/s^2 a body with nothing under its origin
                                  // slides toward the nearest edge of whatever its
                                  // collider box is still catching on.
#define DEATH_SKID_FRIC  2.7f     // 1/s of ground friction on a sliding corpse,
#define DEATH_SKID_STOP  0.32f    // and the speed below which it is at rest.
                                  // NAMED because the corpse pool finishes the skid the
                                  // entity started: PLAYER_RESPAWN_TICKS is 120 (1 s),
                                  // not the 3 s the handover used to claim, and at up
                                  // to 10.4 m/s of throw a body needs 160 ticks to
                                  // reach the cut-off — so it is essentially ALWAYS
                                  // still moving when it is handed over, and freezing
                                  // it there is a visible snap to a stop.

// ONE RAGDOLL, INTEGRATED IN ONE PLACE. The state used to live twice: the entity
// carried ten `d*` fields and integrated them in `bot_tick`'s dead branch, while the
// corpse pool carried a velocity and a grounded flag and finished only the horizontal
// SKID. A body handed over mid-flight therefore stopped tumbling, stopped falling and
// stopped bouncing at the respawn tick — and with a real launch behind it, mid-flight
// is exactly when the handover happens.

// How FLAT the body is, 0 upright to 1 lying. `tilt` is unbounded now, so the
// old `tilt / 1.45` ramp would read a body that had cartwheeled onto its head
// (tilt = pi, upright again) as maximally flat and shrink its collider to a
// crate. |sin| is the actual projection and is right at every wrap.
static float rag_flat(float tilt) { return fabsf(sinf(tilt)); }

// The attitude a body at this tilt comes to rest in: the nearest LYING one.
static float rag_support_eps(v3 p, float eps) {
  float y = 0.0f;
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (p.x < s->min.x || p.x > s->max.x || p.z < s->min.z || p.z > s->max.z)
      continue;
    if (s->max.y <= p.y + eps && s->max.y > y) y = s->max.y;
  }
  return y;
}
// The pose solve's tolerance. `ragx` asks the same question with a tighter one —
// it is MEASURING the answer, so it may not round a surface into existence the
// way the solve is allowed to — but through the same function, because a proof
// and the thing it proves disagreeing about which surfaces exist is how a proof
// passes a pose it should have failed.
static float rag_support(v3 p) { return rag_support_eps(p, 0.02f); }

static float rag_rest(float tilt, float off) {
  float k = floorf((tilt - F_PI_2) / F_PI + 0.5f);
  float t = (2.0f * k + 1.0f) * F_PI_2;
  // The offset is NON-NEGATIVE and takes the side's own sign.
  float d = DEATH_FLAT_SHORT + off;
  return t + (tilt > t ? d : -d);
}

// One tick of the ragdoll, for whoever owns it.
static void rag_step(rag_t *r, v3 *pos) {
  r->prev_tilt = r->tilt;
  r->prev_slam = r->slam;
  r->prev_yaw  = r->yaw;
  r->prev_lag  = r->lag;
  // The collider shrinks as the body goes down: one lying flat must not be held
  // upright by knee-high cover it is draped over.
  float h = HEIGHT_STAND - 1.30f * rag_flat(r->tilt);
  int wasg = r->grounded;
  if (!r->grounded) {
    float k = expf(-DEATH_DRAG * TICK_DT);
    r->vel.x *= k;
    r->vel.z *= k;
    r->spin *= expf(-DEATH_AIR_FRIC * TICK_DT);
  }
  if (r->push_t > 0) {
    r->vel.x += r->push.x / (float)DEATH_PUSH_T;
    r->vel.z += r->push.z / (float)DEATH_PUSH_T;
    r->push_t--;
  }
  r->vel.y -= GRAVITY * TICK_DT;
  v3 v0 = r->vel;
  r->grounded = 0;
  ent_move(pos, &r->vel, &r->grounded, h);
  float wx = (v0.x != 0.0f && r->vel.x == 0.0f) ? v0.x : 0.0f;
  float wz = (v0.z != 0.0f && r->vel.z == 0.0f) ? v0.z : 0.0f;
  // The arena shell stops a body by MOVING it, not by zeroing its velocity, so it needs
  // its own read — without it a corpse thrown at the perimeter presses into the wall at
  // full speed forever and never slams. arena_clamp stays as the positional backstop it
  // has always been, but needs NO rebound read of its own: the perimeter is eight real
  // 3.2 m solids, so move_axis has already stopped the body and zeroed the component at
  // the identical coordinate (s->min.x - PLAYER_RADIUS IS the clamp's own bound).
  arena_clamp(pos);
  float ws = sqrtf(wx * wx + wz * wz);
  if (ws > DEATH_WALL_MIN) {
    r->vel.x -= wx * DEATH_WALL_E;      // the stopped component is 0, so this
    r->vel.z -= wz * DEATH_WALL_E;      // is exactly -e times the arrival
    r->slam = fmaxf(r->slam, fminf(1.0f, ws * 0.22f));
    // The wall does not merely eat the rotation, it CONVERTS the travel into it: a body
    // that hits cover shoulder-first whips over it.
    r->spin = fminf(DEATH_SPIN_MAX,
                    r->spin * DEATH_SLAM_SPIN + ws * DEATH_WALL_SPIN);
    r->yawv *= DEATH_SLAM_SPIN;
  }
  // A BODY DOES NOT BALANCE ON A CORNER. `ent_move` stands the collider on any solid
  // its AABB overlaps, so a body whose origin is up to PLAYER_RADIUS past a platform
  // edge is held at that platform's height with its entire mass over the void —
  // measured with `ragx`, a corpse 2.45 m in the air reporting grounded=1 for 122 of
  // 300 ticks, which is the "schwebende Leiche" exactly.
  if (r->grounded) {
    v3 perp = {{-r->axis.z, 0.0f, r->axis.x}};
    float pl = sqrtf(perp.x * perp.x + perp.z * perp.z);
    float reach = DEATH_COM_Y * sinf(r->tilt);
    v3 com = *pos;
    if (pl > 1e-4f) {
      com.x += perp.x / pl * reach;
      com.z += perp.z / pl * reach;
    }
    if (pos->y > rag_support(com) + 0.03f) {
      r->grounded = 0;
      // Clearing the flag is not enough on its own: next tick's sweep puts the still-
      // overlapping box back on the same ledge and the body hovers with the flag
      // flickering.
      for (int i = 0; i < g_num_solids; i++) {
        const solid_t *sd = &g_solids[i];
        if (fabsf(sd->max.y - pos->y) > 0.05f) continue;
        if (pos->x < sd->min.x - PLAYER_RADIUS || pos->x > sd->max.x + PLAYER_RADIUS ||
            pos->z < sd->min.z - PLAYER_RADIUS || pos->z > sd->max.z + PLAYER_RADIUS)
          continue;
        float dxl = pos->x - sd->min.x, dxh = sd->max.x - pos->x;
        float dzl = pos->z - sd->min.z, dzh = sd->max.z - pos->z;
        float m = fminf(fminf(dxl, dxh), fminf(dzl, dzh));
        if (m == dxl)      r->vel.x -= DEATH_LEDGE_A * TICK_DT;
        else if (m == dxh) r->vel.x += DEATH_LEDGE_A * TICK_DT;
        else if (m == dzl) r->vel.z -= DEATH_LEDGE_A * TICK_DT;
        else               r->vel.z += DEATH_LEDGE_A * TICK_DT;
        break;
      }
    }
  }
  if (!wasg && r->grounded) r->spin *= DEATH_LAND_SPIN;
  if (r->grounded && v0.y < -DEATH_BOUNCE_MIN) {
    // A SKIP. Not because a corpse is bouncy — it is not — but because a body arriving
    // at 6 m/s does not stop on contact, it slaps, lifts a shoulder and comes down
    // again a metre further on.
    r->vel.y = -v0.y * DEATH_BOUNCE_E;
    r->vel.x *= DEATH_BOUNCE_H;
    r->vel.z *= DEATH_BOUNCE_H;
    r->spin = fminf(DEATH_SPIN_MAX, r->spin + (-v0.y) * DEATH_BOUNCE_SPIN);
    r->slam = fmaxf(r->slam, fminf(0.60f, -v0.y * 0.09f));
    r->grounded = 0;
  }
  r->slam *= expf(-4.5f * TICK_DT);
  if (r->grounded) {  // skidding: friction bleeds the travel off
    float hs = sqrtf(r->vel.x * r->vel.x + r->vel.z * r->vel.z);
    if (hs > 0.0f) {
      // Viscous AND Coulomb.
      float nh = hs * expf(-DEATH_SKID_FRIC * TICK_DT) - DEATH_SKID_MU * TICK_DT;
      if (nh < DEATH_SKID_STOP) nh = 0.0f;
      r->vel.x *= nh / hs;
      r->vel.z *= nh / hs;
    }
  }
  // TOPPLE. In the air the body is FREE — whatever the shot and the wall put into it
  // keeps turning.
  if (r->grounded) {
    float rest = rag_rest(r->tilt, r->rest_off);
    float d = rest - r->tilt;
    r->spin += (d >= 0.0f ? DEATH_TOPPLE_A : -DEATH_TOPPLE_A) * TICK_DT;
    r->spin *= expf(-DEATH_TILT_FRIC * (0.30f + rag_flat(r->tilt)) * TICK_DT);
    float nt = r->tilt + r->spin * TICK_DT;
    if ((d >= 0.0f && nt >= rest) || (d < 0.0f && nt <= rest)) {
      // Arrived.
      r->tilt = rest;
      r->spin = 0.0f;
    } else {
      r->tilt = nt;
    }
  } else {
    r->tilt += r->spin * TICK_DT;
  }
  // The limb lag is a one-pole chase of the trunk angle: it opens while the
  // body is turning fast and closes to zero the moment it stops, so it costs
  // nothing on a settled corpse and needs no separate decay.
  r->lag += (r->tilt - r->lag) * DEATH_LAG_RATE;
  // The spin about the body's own vertical bleeds only on ground contact, and
  // faster the FLATTER the body is — that is floor friction and nothing
  // subtler: a body still on its boots pivots on two small contacts, one on its
  // back is dragging its whole length. 640 ms upright, 164 ms flat.
  if (r->grounded)
    r->yawv *= expf(-DEATH_YAW_FRIC * (0.35f + rag_flat(r->tilt)) * TICK_DT);
  r->yaw += r->yawv * TICK_DT;
}

// Advance a DEAD body one tick: limb slack ramp, the one-copy integrator, and the
// frozen world-space pose travelling with the origin.
static void rag_advance(rag_t *r, v3 *pos, v3 prev, float *slack, anim_t *a) {
  *slack += 1.0f / BOT_TOPPLE_TICKS;
  if (*slack > 1.0f) *slack = 1.0f;
  rag_step(r, pos);
  vfx_rag_sparks(r, *pos);   // contact feedback, both owners, one call site
  a->prev_pose = a->pose;
  v3 dp = v3_sub(*pos, prev);
  if (v3_dot(dp, dp) > 1e-12f) {
    v3 *j = (v3 *)&a->pose;
    for (int k = 0; k < POSE_NPOS; k++) j[k] = v3_add(j[k], dp);
  }
}

// Difficulty is one row of numbers — the shape of the behavior never changes,
// only how fast it perceives, settles and shoots.
typedef struct {
  float react_ms;            // first sight -> first shot delay
  float err0, err1;          // aim error deg: on acquire -> fully settled
  float settle_ms;           // 95% of the error decay while tracking
  float turn;                // rad/s max tracking turn rate
  float rec_comp;            // 0..1 how much of its own recoil it pulls back out
  int   burst_lo, burst_hi;  // shots per burst
  float pause_lo, pause_hi;  // ms between bursts
  int   memory_ticks, hold_ticks, search_ticks;
  // (movement is NOT a difficulty knob: every tier moves on the player's numbers,
  //  and there is no aim LEAD either — see the aiming code for why)
  float tac;                 // 0..1 how often it slides / peeks / jumps out
} bot_skill_t;

typedef enum {
  BOT_BELIEF_VISIBLE,
  BOT_BELIEF_OCCLUDED_HOLD,
  BOT_BELIEF_INVESTIGATE,
  BOT_BELIEF_SEARCH_SWEEP
} bot_belief_state_t;

static const bot_skill_t BOT_SKILL[3] = {
  //  react  err0  err1  settle turn  reco  burst   pause      mem hold search tac
  { 480, 9.0f, 4.2f, 1300, 3.5f, 0.20f, 2, 4,  550, 950, 360, 42, 216, 0.25f },
  { 300, 6.5f, 2.6f,  950, 5.5f, 0.60f, 3, 6,  380, 700, 540, 60, 312, 0.60f },
  { 180, 4.0f, 0.7f,  700, 8.5f, 0.92f, 4, 8,  240, 480, 720, 78, 432, 1.00f },
};

struct bot_t {
  v3    pos, prev_pos, vel;
  float yaw, prev_yaw, pitch;
  int   active, hp, alive, grounded;
  int   respawn_t;                     // dead: ticks until respawn
  float topple, prev_topple;           // 0..1 collapse blend (limbs go slack)
  // Death ragdoll: the shot's momentum, then gravity, air drag, ground bounce and wall
  // rebound, integrated by the shared `rag_step` the corpse pool also runs.
  rag_t rag;
  int   flash, flash_head;             // hit flash ticks + was it a headshot
  v3    target;                        // patrol waypoint
  int   stuck;                         // ticks of wanting to move but not moving
  anim_t anim;                         // skeleton locomotion state + solved pose
  // harness puppet: scripted movement intents replace the AI for this bot
  int   puppet, pup_crouch, pup_jump, pup_slide, pup_ready, pup_ads, sliding;
  float pup_mx, pup_mz, pup_speed, pup_face;
  float pup_lean_t, pup_lean;          // scripted lean: target and eased value
  // combat
  int   tgt;                           // current enemy entity id, -1 = none
  int   lose_t;                        // ticks since the target was last seen
  v3    last_seen;                     // where the target was last spotted
  bot_belief_state_t belief;
  v3    last_seen_vel;
  v3    belief_pos;
  float belief_radius;
  float belief_err_a;                  // WHICH WAY it is wrong, frozen at loss
  long  last_seen_tick;
  v3    search_pos;
  int   search_index, search_dir, search_t, reacquire_count;
  unsigned search_visited;
  int   threat_src, threat_t, threat_score;
  int   react_t;                       // ticks until the first shot may leave
  int   scan_t;                        // ticks until the next target rescan
  int   burst, pause_t;                // trigger discipline (a skill trait)
  float err;                           // current aim error magnitude (deg)
  float off_p, off_y, goal_p, goal_y;  // eased aim-error offset + drift goal
  int   drift_t;                       // ticks until the offset goal resamples
  int   strafe_dir, strafe_t;          // -1/0/+1 and ticks until re-roll
  // tactics
  int   hurt_t;                        // ticks left of "was just shot at"
  float fl_y, fl_p;                    // flinch: transient aim kick, decays
  int   crouch;                        // holding low behind cover
  weapon_t wp;                         // the player's weapon state, per bot
  int   crouch_t;                      // >0 hold left, <0 cooldown before the next
  float eye;                           // eased hitbox eye height (see bot_eye)
  v3    cov_p;                         // the cover spot it committed to
  int   cov_valid;                     // a spot is chosen and not yet reached
  int   cov_retry;                     // ticks until a FAILED probe may re-run
  int   cov_phase;                     // ring start, advanced when a spot fails
  int   in_cover;                      // moving to break the line right now
  int   slide_cd, jump_cd;             // tactical cooldowns
  v3    flank_p;                       // the swing anchor it committed to
  int   flank_t;                       // >0 = committed to the flank, in ticks
  int   flank_cd;                      // ticks before the next flank may start
  int   flank_arrived;                 // lifetime arrivals; census only
  v3    dl_p;                          // deadlock: where the decision sends it
  int   dl_t, dl_cd, dl_push;          // commitment / cooldown / which way
  v3    alert_p;                       // the spot a finished fight left behind
  int   alert_t;                       // ticks of post-contact wariness left
  int   prefire_t, prefire_cd;         // pre-fire burst window / its cooldown
  int   prefiring;                     // this tick fires on BELIEF, not sight
  int   peek;                          // this tick started a corner peek move
  int   startled;                      // this tick is steered by the EAR
  int   stare;                         // consecutive engage-ready-no-shot ticks
  int   pin;                           // consecutive wants-to-move-but-cannot ticks
  int   swap_cd;                       // AI-policy cooldown; never a weapon timer
  // Authoritative auditory investigation. A zero hear_tick means no memory.
  v3    hear_pos;
  long  hear_tick, hear_until;
  bot_noise_kind_t hear_kind;
  float hear_score;
  int   hear_src;                      // WHOSE noise is believed (refresh rule)
  float hear_err_a;                    // which way it mislocates, held per contact
};

static bot_t g_bots_all[SRV_LOBBIES][MAX_BOTS];
#define g_bots (g_bots_all[g_lobby])
static int   g_bots_frozen;  // harness switch: no wander, no combat
static int home_calm(void);  // ...and the boot screen's per-shot one
// The boot screen's bots kill each other for the camera's benefit; the match they are
// doing it inside must not END while the player is reading the tiles.
static int sp_frag_limit(void) { return g_home ? 100000 : g_cfg.sp_frag; }

static int sp_bot_count(void) {
  int n = g_home ? HOME_BOTS : g_cfg.sp_bots;
  return n < MAX_BOTS ? n : MAX_BOTS;
}

typedef struct {
  int  nbots;                                            // active bots
  // ENTITY-id indexed — must follow MAX_ENTS, never MAX_BOTS(+1): these sit directly
  // before over/winner, so one out-of-range write here ends the match with a garbage
  // winner.
  int  kills[MAX_ENTS], deaths[MAX_ENTS];
  int  shots[MAX_ENTS], hits[MAX_ENTS];                  // accuracy source
  bot_noise_t noise[BOT_NOISE_COUNT][MAX_ENTS];          // latest per kind/source
  int  over, winner, last_killer, restart_req;
} match_t;

// How long a NETWORKED match holds the final scoreboard before the arena regenerates.
// 10 s at 120 Hz: long enough to read a 20-name board, short enough that a public
// instance is never parked.
#define MATCH_OVER_TICKS 1200

static match_t g_match_all[SRV_LOBBIES];
#define g_match (g_match_all[g_lobby])
// THE OVER COUNTDOWN IS PER LOBBY, and that is the entire point of it living here.
static int g_over_t_all[SRV_LOBBIES];
#define g_over_t (g_over_t_all[g_lobby])
static int g_match_nbots_fwd(void) { return g_match.nbots; }  // for ent_next

// Bounded, simulation-owned writer. tick==0 remains the empty-store marker.
static void bot_noise_emit(int src, bot_noise_kind_t kind, v3 pos, float radius) {
  if (src < 0 || src >= MAX_ENTS || kind < 0 || kind >= BOT_NOISE_COUNT ||
      !(radius > 0.0f) || f_is_nonfinite(radius)) return;
  g_match.noise[kind][src] = (bot_noise_t){pos, g_tick, radius, src};
}

static int bot_hear_life(bot_noise_kind_t kind) {
  return kind == BOT_NOISE_SHOT ? BOT_HEAR_SHOT_TICKS : BOT_HEAR_STEP_TICKS;
}

// Perception reads only a completed world tick. That makes a stimulus equally
// available to every bot on the following tick, independent of entity order.
static void bot_hear_update(bot_t *b, int bi) {
  if (b->hear_tick && g_tick > b->hear_until) {
    b->hear_tick = 0;
    b->hear_until = 0;
    b->hear_score = 0.0f;
  }

  v3 ear = {{b->pos.x, b->pos.y + bot_eye(b), b->pos.z}};
  const bot_noise_t *best = NULL;
  bot_noise_kind_t best_kind = BOT_NOISE_SHOT;
  float best_score = 0.0f;
  int best_blocked = 0;
  for (int kind = 0; kind < BOT_NOISE_COUNT; kind++) {
    for (int src = 0; src < MAX_ENTS; src++) {
      const bot_noise_t *n = &g_match.noise[kind][src];
      if (n->tick == 0 || n->tick >= g_tick || n->src == bi + 1) continue;
      if (g_tick - n->tick > bot_hear_life((bot_noise_kind_t)kind)) continue;
      float dist = v3_len(v3_sub(n->pos, b->pos));
      // RELATIVE loudness — how much of its own reach the sound has left, not how many
      // metres.
      float score = (n->radius - dist) / n->radius;
      if (score <= 0.0f) continue;
      // Exactly result-preserving, and it is what pays for the ray the startle branch
      // now spends: BOT_HEAR_WALL is a constant below 1, so attenuation can only LOWER
      // a score.
      if (score <= best_score) continue;
      // The blocked verdict is KEPT, not re-derived: the localisation error
      // below doubles through a wall and re-tracing the winner for that was one
      // more ray per bot per tick (measured 0.74/bot-tick at 20 bots) to
      // recompute something this loop had already decided.
      int blocked = !los_clear(ear, n->pos);
      if (blocked) score *= BOT_HEAR_WALL;
      if (score > best_score) {
        best = n;
        best_kind = (bot_noise_kind_t)kind;
        best_score = score;
        best_blocked = blocked;
      }
    }
  }
  // The 1.15x hysteresis answers "which SOURCE do I believe" and must not also answer
  // "where is the source I already believe".
  int fresh = !b->hear_tick || g_tick > b->hear_until;
  if (best && (fresh || best->src == b->hear_src ||
               best_score > b->hear_score * 1.15f)) {
    // The error DIRECTION is drawn once per contact and then held for as long as the
    // same source keeps refreshing it.
    if (fresh || best->src != b->hear_src)
      b->hear_err_a = rng_rangef(&g_rng_bot, -F_PI, F_PI);
    float err = BOT_HEAR_ERR * v3_len(v3_sub(best->pos, b->pos));
    if (best_blocked) err *= 2.0f;
    // Clamp AFTER the doubling, or BOT_HEAR_ERR_MAX is not the ceiling it says
    // it is: clamping first let a blocked contact past 16 m reach 5.2 m, which
    // no fixture could see because the wall case is staged at 10 m.
    err = f_clamp(err, 0.0f, BOT_HEAR_ERR_MAX);
    b->hear_pos = best->pos;
    b->hear_pos.x += sinf(b->hear_err_a) * err;
    b->hear_pos.z += cosf(b->hear_err_a) * err;
    b->hear_pos.y = 0.0f;   // a bearing along the floor; height is not heard
    b->hear_src = best->src;
    b->hear_tick = best->tick;
    b->hear_until = best->tick + bot_hear_life(best_kind);
    b->hear_kind = best_kind;
    b->hear_score = best_score;
  }
}

// Server observability: ONE function feeds both the server's 10 s stdout digest (the
// container's entrypoint tees stdout into server.log and docker/report.awk renders it
// into stats.txt — that file is the whole monitoring story) and the `netstat` proof
// command — the one-copy rule applied to observability, and the reason a hanging
// instance is diagnosable at all.
static long g_srv_started;  // unix time at server start, 0 = not a server
static long g_srv_behind;     // worst tick backlog the pacing loop ever saw
static int net_srv_client_count(void);  // defined with the connection layer below
static void net_srv_lat(int *rtt_max, int *qd_max);  // ditto (reads g_conns)
// UNSIGNED, because these are server-LIFETIME totals with no reset: a client holding a
// valid token can add up to NET_IN_MAX per record, which reaches INT_MAX in about a
// quarter of an hour of sustained traffic.
static unsigned g_srv_dropped_inputs;   // input ticks the budget refused
static unsigned g_srv_trim_drops;       // moves discarded by the backlog trim —
                                        // THE rubber-band path; it read as dropped_in=0
                                        // while the live server trimmed a correction
                                        // per hitch TELEMETRY LINE BUDGETS — TWO of
                                        // them, answering DIFFERENT questions.
static int  g_tele_budget;              // accepted this second   (RATE)
static long g_tele_day_budget;          // accepted this day      (TOTAL)
enum { TELE_DAY_MAX = 50000 };
// Optional net diagnostics (OFF by default): `netdebug` in the harness, `--net-debug`
// on a server.
static int g_net_debug;
[[maybe_unused]] static void net_digest(char *out, size_t n) {
  int nh = 0;
  for (int h = 0; h < MAX_HUMANS; h++)
    if (g_humans_on[h]) nh++;
  long up = g_srv_started ? (long)time(NULL) - g_srv_started : 0;
  // The world hash rides the digest so a client<->server arena divergence
  // is diagnosable from server.log with grep and nothing else. clients and
  // dropped-inputs are the numbers an operator asks for; rtt/qd are the worst
  // smoothed ping and input-queue depth across the conns — the two numbers
  // that say "the connection feel is fine" or name the client it is not.
  int rtt, qd;
  net_srv_lat(&rtt, &qd);
  snprintf(out, n,
           "digest up=%ld tick=%ld clients=%d humans=%d bots=%d over=%d "
           "behind=%ld dropped_in=%u trim=%u ev_drops=%ld rtt=%d qd=%d world=%08x",
           up, g_tick, net_srv_client_count(), nh, g_match.nbots, g_match.over,
           g_srv_behind, g_srv_dropped_inputs, g_srv_trim_drops, g_ev_drops,
           rtt, qd, net_world_hash());
}

// Server log line: unix-ts prefix, one flushed stdout line.
static void slog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void slog(const char *fmt, ...) {
  printf("%ld ", (long)time(NULL));
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  putchar('\n');
  fflush(stdout);
}

// Position of ANY entity — the one copy: bot_tick resolved this inline
// twice byte-identically, ent_damage's last_seen a third time, and each was a
// g_bots[e - 1] OOB for a human id above MAX_BOTS.
static v3 ent_pos(int e) {
  int h = human_of_ent(e);
  return h >= 0 ? g_players[h].pos : g_bots[e - 1].pos;
}

// Fill / cap decisions. bot_fill is the company target: bots top a populated arena up
// to it (at most cap_bots of them) so a lonely human has an opponent, and an EMPTY
// arena holds none. The old middle parameter was cap_public — bots padded toward the
// arena cap, which had an idle server simulating one bot for nobody in every lobby.
static int net_bot_target(int humans, int bot_fill, int cap_bots) {
  if (humans <= 0) return 0;
  int want = bot_fill - humans;
  if (want > cap_bots) want = cap_bots;
  return want < 0 ? 0 : want;
}
static int net_join_ok(int humans, int cap_public) {
  return humans < cap_public;
}


// One entity's live physics/state packed into a wire record. Presentation
// (anim_t/vm_t/pose_t) is deliberately absent — the client re-solves it.
static uint8_t g_spawn_seq_all[SRV_LOBBIES][MAX_ENTS];
#define g_spawn_seq (g_spawn_seq_all[g_lobby])

static void net_ent_from_live(net_ent_t *e, int ent) {
  int h = human_of_ent(ent);
  v3 pos, vel; float yaw, pitch, lean; int alive, grounded, crouch, slide, hp, wpn;
  // THE CARRIAGE TRIPLE, read back off the entity rather than recomputed here. Each is
  // published at the one place it is DECIDED (gun_ads for the aim intent; each
  // producer's own anim_tick call for the carry state), so the packet and the server's
  // own pose are the same value and cannot drift into disagreeing about the same man.
  int ads, ready, tuck;
  if (h >= 0) {
    const player_t *p = &g_players[h];
    pos = p->pos; vel = p->vel; yaw = p->yaw; pitch = p->pitch; lean = p->lean;
    alive = p->alive; grounded = p->grounded; crouch = p->crouched; slide = p->sliding;
    hp = p->hp; wpn = p->wp.cur;
    ads = p->wp.ads_want; ready = p->wp.ready_want; tuck = p->tuck_y > 0.0f;
  } else {
    const bot_t *b = &g_bots[ent - 1];
    pos = b->pos; vel = b->vel; yaw = b->yaw; pitch = b->pitch; lean = 0.0f;
    alive = b->alive; grounded = b->grounded; crouch = b->crouch; slide = b->sliding;
    hp = b->hp; wpn = b->wp.cur;
    // Bots never tuck — bot_tick passes anim_tick a literal 0.0f — so this is 0 by
    // construction rather than by omission, exactly as their lean is.
    ads = b->wp.ads_want; ready = b->wp.ready_want; tuck = 0;
  }
  e->id = (uint8_t)ent;
  e->flags = (uint8_t)((alive    ? NEF_ALIVE    : 0) | (grounded ? NEF_GROUNDED : 0) |
                       (crouch   ? NEF_CROUCH   : 0) | (slide    ? NEF_SLIDE    : 0) |
                       (ads      ? NEF_ADS      : 0) | (ready    ? NEF_READY    : 0) |
                       (tuck     ? NEF_TUCK     : 0));
  e->hp = (uint8_t)f_clamp((float)hp, 0.0f, 255.0f);
  e->weapon = (uint8_t)(wpn & 3);
  e->spawn_seq = g_spawn_seq[ent];
  {
    const weapon_t *wp = h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp;
    int mag = wp->ammo[wpn >= 0 && wpn < WPN_COUNT ? wpn : 0];
    e->mag = (uint8_t)(mag < 0 ? 0 : mag > 255 ? 255 : mag);
  }
  e->x = net_q_pos(pos.x); e->y = net_q_pos(pos.y); e->z = net_q_pos(pos.z);
  e->vx = net_q_vel(vel.x); e->vy = net_q_vel(vel.y); e->vz = net_q_vel(vel.z);
  e->yaw = net_q_yaw(yaw); e->pitch = net_q_pitch(pitch);
  e->lean = net_q_wish(lean);
}

// SNAPSHOT: header + one record per live entity. Returns bytes written, or 0 if the
// buffer was too small (a real snapshot at 8 fighters is ~230 B).
#define NET_EVLOG 256
typedef struct { uint32_t id, tick; event_t e; } srv_ev_t;
static srv_ev_t g_srv_evlog_all[SRV_LOBBIES][NET_EVLOG];
#define g_srv_evlog (g_srv_evlog_all[g_lobby])
static int      g_srv_evlog_head_all[SRV_LOBBIES], g_srv_evlog_n_all[SRV_LOBBIES];
#define g_srv_evlog_head (g_srv_evlog_head_all[g_lobby])
#define g_srv_evlog_n    (g_srv_evlog_n_all[g_lobby])
static uint32_t g_srv_ev_next_all[SRV_LOBBIES] = {[0 ... SRV_LOBBIES - 1] = 1};
#define g_srv_ev_next (g_srv_ev_next_all[g_lobby])   // 0 = acked nothing

// Called once per server tick, BEFORE the sim's event list is drained.
static void net_ev_capture(void) {
  for (int i = 0; i < g_num_events; i++) {
    ev_type_t t = g_events[i].type;
    if (t == EV_STEP || t == EV_LAND) continue;   // each client makes its own
    g_srv_evlog[g_srv_evlog_head] =
        (srv_ev_t){g_srv_ev_next++, (uint32_t)g_tick, g_events[i]};
    g_srv_evlog_head = (g_srv_evlog_head + 1) % NET_EVLOG;
    if (g_srv_evlog_n < NET_EVLOG) g_srv_evlog_n++;
  }
}

static size_t net_snapshot_build(unsigned char *buf, size_t cap, uint32_t last_input_tick,
                                 uint32_t ev_ack, uint8_t qdepth) {
  np_wr w = {buf, cap, 0, 0};
  np_w16(&w, NET_PROTO);
  np_w8(&w, NP_SNAPSHOT);
  np_w32(&w, (uint32_t)g_tick);
  np_w32(&w, last_input_tick);
  np_w32(&w, net_world_hash());
  // THE ARENA CAN CHANGE UNDER A LIVE CLIENT. A server regenerates the map on every
  // rematch, and the seed was handed out exactly once, in ACCEPT — so after the first
  // match end the server simulated one world and streamed it to clients still colliding
  // against another.
  np_w32(&w, g_map_seed);
  // This client's input-queue depth at broadcast time: the client's
  // clock nudge holds it at NET_QDEPTH_TARGET instead of letting drift and
  // jitter walk it into the backlog trim (a trim discards moves the client
  // already predicted — every one is a visible correction).
  np_w8(&w, qdepth);
  size_t cnt_at = w.at;
  np_w8(&w, 0);                            // count placeholder
  int cnt = 0;
  ENT_FOREACH(e) {
    net_ent_t ne;
    net_ent_from_live(&ne, e);
    net_ent_encode(&w, &ne);
    cnt++;
  }
  // Events above this client's ack.
  size_t ev_at = w.at;
  np_w8(&w, 0);
  int pick[NET_EV_SNAP_MAX], nev = 0;
  // Both cut-offs are monotone along the newest-first scan (ids descend, age
  // ascends), so the first acked or aged record ends the walk — the loop used
  // to `continue` and rescan all 256 entries per conn per snapshot to pick 0-2.
  for (int k = g_srv_evlog_n - 1; k >= 0 && nev < NET_EV_SNAP_MAX; k--) {
    int idx = (g_srv_evlog_head - g_srv_evlog_n + k + 2 * NET_EVLOG) % NET_EVLOG;
    const srv_ev_t *s = &g_srv_evlog[idx];
    if ((int32_t)(s->id - ev_ack) <= 0) break;           // already acknowledged
    uint32_t age = (uint32_t)g_tick - s->tick;
    if (age > NET_EV_RESEND_TCK) break;                  // outside the window
    pick[nev++] = idx;
  }
  for (int k = nev - 1; k >= 0; k--) {                   // ascending ids again
    const srv_ev_t *s = &g_srv_evlog[pick[k]];
    net_ev_t ne;
    net_ev_from_live(&ne, &s->e, s->id);
    net_ev_encode(&w, &ne);
  }
  if (w.bad) return 0;
  buf[cnt_at] = (uint8_t)cnt;
  buf[ev_at]  = (uint8_t)nev;
  return w.at;
}

// Decode a snapshot into a caller array (the client's interpolation source).
static int net_snapshot_read(const unsigned char *buf, size_t len, uint32_t *server_tick,
                             uint32_t *last_input_tick, uint32_t *world,
                             uint32_t *seed, uint8_t *qdepth,
                             net_ent_t *out, int out_cap, int *complete,
                             net_ev_t *evs, int ev_cap, int *ev_cnt) {
  np_rd r = {buf, len, 0, 0};
  if (np_r16(&r) != NET_PROTO) return -1;
  if (np_r8(&r) != NP_SNAPSHOT) return -1;
  uint32_t st = np_r32(&r), li = np_r32(&r), wh = np_r32(&r), sd = np_r32(&r);
  uint8_t qd = np_r8(&r);
  int cnt = np_r8(&r);
  if (r.bad) return -1;
  if (qdepth) *qdepth = qd;
  if (cnt > out_cap) cnt = out_cap;        // never write past the caller array
  int got = 0;
  for (int i = 0; i < cnt; i++) {
    net_ent_t ne;
    if (!net_ent_decode(&r, &ne)) break;   // truncated: stop, keep what parsed
    out[got++] = ne;
  }
  // A PARTIAL LIST IS NOT A SHORTER ROSTER. Records go out in ENT_FOREACH
  // order, so humans are LAST — a single unparseable record therefore looks
  // exactly like "everyone after this left", and a caller that reads absence
  // as departure would retire them, up to and including the local player.
  if (complete) *complete = (got == cnt);
  // The event section.
  if (ev_cnt) *ev_cnt = 0;
  if (evs && ev_cap > 0) {
    int ec = np_r8(&r);
    if (!r.bad) {
      if (ec > ev_cap) ec = ev_cap;
      int eg = 0;
      for (int i = 0; i < ec; i++) {
        net_ev_t ne;
        if (!net_ev_decode(&r, &ne)) break;
        evs[eg++] = ne;
      }
      if (ev_cnt) *ev_cnt = eg;
    }
  }
  if (server_tick) *server_tick = st;
  if (last_input_tick) *last_input_tick = li;
  if (world) *world = wh;
  if (seed) *seed = sd;
  return got;
}

// SCORES(sibling): the match's score rows as replicated STATE, 4 Hz.
// kills/deaths/shots/hits only ever mutate on the server (ent_damage, match_on_kill,
// gun_shoot), so a connected client's scoreboard read zeros forever — this packet is
// the one source of score truth online.
typedef struct { uint8_t id; uint16_t kills, deaths, shots, hits; uint8_t ping; } net_score_t;
static size_t net_scores_build(unsigned char *buf, size_t cap,
                               const uint8_t ping[MAX_ENTS]) {
  np_wr w = {buf, cap, 0, 0};
  np_w16(&w, NET_PROTO);
  np_w8(&w, NP_SCORES);
  np_w8(&w, (uint8_t)(g_match.over ? 1 : 0));
  np_w8(&w, (uint8_t)(g_match.winner < 0 ? 0xFF : g_match.winner));
  np_w16(&w, (uint16_t)g_cfg.sp_frag);       // "FIRST TO N" is match property
  size_t cnt_at = w.at;
  np_w8(&w, 0);
  int cnt = 0;
  ENT_FOREACH(e) {
    np_w8(&w, (uint8_t)e);
    np_w16(&w, (uint16_t)g_match.kills[e]);
    np_w16(&w, (uint16_t)g_match.deaths[e]);
    np_w16(&w, (uint16_t)g_match.shots[e]);
    np_w16(&w, (uint16_t)g_match.hits[e]);
    np_w8(&w, ping ? ping[e] : 0);
    cnt++;
  }
  // Name section: every present HUMAN's display name (bots derive theirs from BOT_NAMES
  // locally).
  size_t ncnt_at = w.at;
  np_w8(&w, 0);
  int ncnt = 0;
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h]) continue;
    np_w8(&w, (uint8_t)ent_of_human(h));
    // Fixed 11 bytes, zero-filled past the NUL: g_human_name keeps the
    // previous occupant's tail bytes after a shorter name overwrites it, and
    // emitting the raw array leaked them to every client.
    int done = 0;
    for (int k = 0; k < 11; k++) {
      uint8_t ch = done ? 0 : (uint8_t)g_human_name[h][k];
      if (!ch) done = 1;
      np_w8(&w, ch);
    }
    ncnt++;
  }
  if (w.bad) return 0;
  buf[cnt_at] = (uint8_t)cnt;
  buf[ncnt_at] = (uint8_t)ncnt;
  return w.at;
}
// Hostile-input rules as everywhere: bounds-checked cursor, id rejected as an
// index, a truncated row list keeps what parsed. Returns rows, -1 on bad header.
typedef struct { uint8_t id; char name[12]; } net_name_t;
static int net_scores_read(const unsigned char *buf, size_t len,
                           uint8_t *over, uint8_t *winner,
                           uint16_t *frag,
                           net_score_t *out, int out_cap,
                           net_name_t *nout, int nout_cap,
                           int *nout_cnt) {
  np_rd r = {buf, len, 0, 0};
  if (np_r16(&r) != NET_PROTO) return -1;
  if (np_r8(&r) != NP_SCORES) return -1;
  uint8_t ov = np_r8(&r), wi = np_r8(&r);
  uint16_t fr = np_r16(&r);
  int cnt = np_r8(&r);
  if (r.bad) return -1;
  if (cnt > out_cap) cnt = out_cap;
  int got = 0;
  for (int i = 0; i < cnt; i++) {
    net_score_t s;
    s.id = np_r8(&r);
    s.kills = np_r16(&r); s.deaths = np_r16(&r);
    s.shots = np_r16(&r); s.hits = np_r16(&r);
    s.ping = np_r8(&r);
    if (r.bad) break;
    if (s.id >= MAX_ENTS) return -1;         // hostile id: refuse the packet
    out[got++] = s;
  }
  if (nout_cnt) *nout_cnt = 0;
  if (nout && nout_cap > 0) {
    int nc = np_r8(&r);
    int ng = 0;
    for (int i = 0; i < nc && ng < nout_cap; i++) {
      net_name_t nm = {0, {0}};
      nm.id = np_r8(&r);
      for (int k = 0; k < 11; k++) {
        char c = (char)np_r8(&r);
        nm.name[k] = c;                      // force-terminated by the {0} init
      }
      if (r.bad) break;
      if (nm.id >= MAX_ENTS) return -1;      // hostile id: refuse the packet
      nout[ng++] = nm;
    }
    if (nout_cnt) *nout_cnt = ng;
  }
  if (over) *over = ov;
  if (winner) *winner = wi;
  if (frag) *frag = fr;
  return got;
}

// (Lag-comp history moved below ent_trace_box/bot_eye.)

// Random free floor spot far from every living combatant (excluding entity `self`, or
// nobody for self < 0).
static v3 spawn_spot(int self) {
  int sh = human_of_ent(self);
  // Fallback point PER HUMAN: one literal for all eight is a deterministic
  // stack of bodies on a full-arena tick. Human 0 keeps the historical 18.
  v3 best = {{0, 0, sh >= 0 ? 18.0f - 2.0f * (float)sh : -12.0f}};
  float best_d = -1.0f;
  for (int i = 0; i < 40; i++) {
    v3 c = {{rng_rangef(&g_rng_bot, -ARENA_HALF + 2, ARENA_HALF - 2), 0,
             rng_rangef(&g_rng_bot, -ARENA_HALF + 2, ARENA_HALF - 2)}};
    if (!aabb_fits(c, PLAYER_RADIUS, HEIGHT_STAND)) continue;
    float d = 1e9f;
    ENT_FOREACH(e) {
      if (e == self) continue;
      int h = human_of_ent(e);
      if (h >= 0) { if (!g_players[h].alive) continue; }
      else if (!g_bots[e - 1].alive || !g_bots[e - 1].active) continue;
      v3 ep = ent_pos(e);
      float dx = c.x - ep.x, dz = c.z - ep.z;
      float dd = dx * dx + dz * dz;
      if (dd < d) d = dd;
    }
    if (d > best_d) { best_d = d; best = c; }
    if (d >= 64.0f) return c;  // >=8 m from everyone: good enough
  }
  return best;
}

static void bot_spawn(int bi) {
  bot_t *b = &g_bots[bi];
  v3 spot = spawn_spot(bi + 1);   // entity id, not bot index
  // Script control survives death and match restart: a filmed battery that
  // crosses a respawn must not silently hand the testbed back to the AI. The
  // movement intent resets with the body; only the CONTROL MODE persists.
  int pup = b->puppet;
  float pup_speed = b->pup_speed;
  int arrived = b->flank_arrived;   // a CENSUS total, not per-life state: the
  memset(b, 0, sizeof *b);          // memset zeroing it made tacstat's per-tick
  b->puppet = pup;                  // difference go negative on every respawn,
  b->pup_speed = pup_speed;         // which cancelled the arrivals it had
  b->flank_arrived = arrived;       // already counted and printed a flat zero
  b->pos = b->prev_pos = b->target = spot;
  b->hp = PLAYER_HP;
  b->alive = 1;
  b->active = 1;
  b->grounded = 1;
  b->eye = EYE_STAND;
  b->yaw = b->prev_yaw = rng_rangef(&g_rng_bot, -3.14f, 3.14f);
  b->pup_face = b->yaw;  // a surviving puppet faces its spawn, not yaw 0
  b->belief = BOT_BELIEF_VISIBLE;
  b->tgt = -1;
  b->last_seen_vel = (v3){{0, 0, 0}};
  b->belief_pos = (v3){{0, 0, 0}};
  b->belief_radius = 0.0f;
  b->last_seen_tick = 0;
  b->search_pos = (v3){{0, 0, 0}};
  b->search_index = 0;
  b->search_dir = 1;
  b->search_t = 0;
  b->search_visited = 0;
  b->reacquire_count = 0;
  b->threat_src = -1;
  b->threat_t = 0;
  b->threat_score = 0;
  b->wp.cur = WPN_AR;
  wpn_fill_mags(&b->wp);
  b->burst = 3;
  b->strafe_dir = (rng_next(&g_rng_bot) & 1) ? 1 : -1;
  g_spawn_seq[bi + 1]++;       // snapshot readers see a teleport, not motion
  anim_reset(&b->anim, b->pos, b->yaw);
}

static int bot_investigating(const bot_t *b) {
  return b->hear_tick && g_tick <= b->hear_until;
}

// Player-equivalent swap semantics for an AI request. The bot policy owns the
// rate limit; this helper is deliberately just an immediate request/transition.
static int bot_request_weapon(bot_t *b, int bi, int desired) {
  weapon_t *w = &b->wp;
  if (desired < 0 || desired >= WPN_COUNT || desired == w->cur || w->switch_t > 0)
    return 0;
  if (w->reload_t > 0)
    ev_push((event_t){.type = EV_RELOAD_CANCEL, .src = bi + 1, .a = b->pos,
                      .wpn = w->cur});
  w->cur = desired;
  w->switch_t = GUN_SWITCH_TICKS;
  w->reload_t = w->reload_tac = 0;
  w->dry_done = 0;
  w->idx = 0;
  // ammo[] and cool[] remain per weapon: holstering never refills/re-cocks it.
  return 1;
}

// The two distance bands deliberately differ: 24 m enters sniper preference,
// while only sub-16 m combat returns to AR. That gap stops boundary thrashing.
static int bot_desired_weapon(const bot_t *b, int engaging, float target_dist,
                              float horizontal_speed) {
  int cur = b->wp.cur;
  int other = (cur + 1) % WPN_COUNT;
  if (engaging && b->wp.ammo[cur] <= 0 && b->wp.ammo[other] > 0) return other;
  // ...and only when there is nothing to shoot at. bot_investigating() is true
  // whenever a hear memory is live, and a gunshot is audible to 34 m, so in any
  // firefight this test used to run BEFORE the !engaging return and pinned an
  // engaging bot to the AR — the sniper preference was quietly suppressed by an
  // audio memory nothing was steering with.
  int investigating = !engaging && bot_investigating(b);
  if (investigating) {
    if (cur == WPN_SR && b->wp.ammo[WPN_AR] > 0) return WPN_AR;
    return cur;
  }
  if (!engaging) return cur;

  if (cur == WPN_AR) {
    if (b->wp.ammo[WPN_SR] > 0 && target_dist >= BOT_SNIPER_ENTER_DIST &&
        b->grounded && horizontal_speed < BOT_SNIPER_ENTER_SPEED && !b->sliding &&
        b->hurt_t == 0)
      return WPN_SR;
  } else if (b->wp.ammo[WPN_AR] > 0 &&
             (target_dist < BOT_AR_RETURN_DIST ||
              horizontal_speed > BOT_AR_RETURN_SPEED ||
              b->sliding || b->hurt_t > 0)) {
    return WPN_AR;
  }
  return cur;
}

// ONE hitbox for every entity — player, bot and lag-comp history — keyed on the eye
// height: head straddling the neck, body below it. *part is 3 for the head, 2 for the
// body; returns the hit distance or -1.
//
// THE BOX STANDS IN THE BODY'S OWN FRAME, and that is the fairness rule: how wide a
// target you are must be a function of how you are STANDING, not of the compass bearing
// between you and the shooter. World-axis boxes presented 440 mm along x and 600 mm
// along z for the same figure and did not move when it turned 90 degrees. The RAY is
// rotated into the frame rather than the box out of it — one ray_aabb, and the distance
// stays in world metres because a rotation preserves length.
//
// THE LEAN IS TWO COMPONENTS AND THE BOX OWES BOTH. Bending the spine about the waist
// moves the head OUT along the body's own right axis and DOWN — 279 mm and 91 mm at
// full lean — and lean_eye_off/lean_eye_drop are the one derivation of each, shared
// with the camera and the skeleton. The box used to take only the lateral half, so a
// leaning head was drawn, and looked at, 91 mm below the box that could be shot. The
// RAW lean is the argument for exactly that reason: a call site cannot pass one
// component and forget the other. The head takes all of the bend and the torso a bit
// over half, since the boots do not move. That trade is the whole point of leaning:
// a peek that moves the shot origin without moving the target is a free kill.
//
// The extents are the figure's own, measured with `figm` in the MODEL frame: 0.28 across
// is the deltoid span, so the shoulders are inside the box where a world box left up to
// 108 mm of each upper arm outside it, and 0.22 deep covers the trunk front to back.
// The head is ONE number because a head is round. The instrument for any change here is
// a lateral sweep of aimed shots across a frozen puppet in 2 cm steps.
static float ent_trace_box(v3 p, float eye, float yaw, float lean,
                           v3 o, v3 d, int *part) {
  v3 n;
  float c = cosf(yaw), s = sinf(yaw);   // right = (c, 0, s): local +x is right
  v3 ro = v3_sub(o, p);
  v3 lo = {{ro.x * c + ro.z * s, ro.y, ro.z * c - ro.x * s}};
  v3 ld = {{d.x * c + d.z * s, d.y, d.z * c - d.x * s}};
  float lat  = lean_eye_off(lean);    // both exactly 0 at lean 0, so an upright
  float drop = lean_eye_drop(lean);   // body's box is bit-identical to before
  float neck = eye - 0.12f;
  float bl = lat * LEAN_BODY_K, bd = drop * LEAN_BODY_K;
  v3 hmn = {{lat - 0.13f, neck - drop,        -0.13f}};
  v3 hmx = {{lat + 0.13f, eye + 0.18f - drop,  0.13f}};
  // The torso keeps its feet: only its TOP follows the neck down.
  v3 bmn = {{bl - 0.28f, 0.0f, -0.22f}};
  v3 bmx = {{bl + 0.28f, neck - bd,  0.22f}};
  // A muzzle INSIDE the box still hits it. ray_aabb deliberately reports no hit for an
  // origin inside a solid (there is no sensible entry face, and the lean probe and the
  // third-person pivot cast both depend on that), but entities do not collide with each
  // other — move_axis tests only g_solids — so two players can fully interpenetrate,
  // and a point-blank shot then passed straight through the target into the wall behind
  // it.
  #define IN_BOX(P, MN, MX) ((P).x >= (MN).x && (P).x <= (MX).x && \
                             (P).y >= (MN).y && (P).y <= (MX).y && \
                             (P).z >= (MN).z && (P).z <= (MX).z)
  float th = IN_BOX(lo, hmn, hmx) ? 0.001f : ray_aabb(lo, ld, hmn, hmx, &n);
  float tb = IN_BOX(lo, bmn, bmx) ? 0.001f : ray_aabb(lo, ld, bmn, bmx, &n);
  #undef IN_BOX
  if (th > 0 && (tb <= 0 || th <= tb)) { *part = 3; return th; }
  if (tb > 0) { *part = 2; return tb; }
  return -1.0f;
}

// A bot's stance picks its eye exactly like the player's does; the box above then makes
// crouching shrink the target for both, by construction.
static float bot_eye(const bot_t *b) { return b->eye; }

// Lag-compensation history: the server rewinds OTHER entities to the moment the
// shooter's input tick was rendered on his screen.
#define NET_HIST 128                 // ~1 s at 120 Hz, > any sane rewind window
typedef struct { v3 pos; float eye, yaw, lean; unsigned char alive; } hist_t;
static hist_t g_hist_all[SRV_LOBBIES][MAX_ENTS][NET_HIST];
#define g_hist (g_hist_all[g_lobby])
static long   g_hist_tick = -1;      // sim tick of the newest slot

static void net_hist_record(void) {
  long slot = g_tick % NET_HIST;
  for (int e = 0; e < MAX_ENTS; e++) g_hist[e][slot] = (hist_t){0};
  ENT_FOREACH(e) {
    hist_t *h = &g_hist[e][slot];
    int hh = human_of_ent(e);
    if (hh >= 0) {
      const player_t *p = &g_players[hh];
      *h = (hist_t){p->pos, p->eye, p->yaw, p->lean, (unsigned char)p->alive};
    } else {
      const bot_t *b = &g_bots[e - 1];
      *h = (hist_t){b->pos, bot_eye(b), b->yaw, 0.0f, (unsigned char)b->alive};
    }
  }
  g_hist_tick = g_tick;
}

// Reconstruct one entity's box at a fractional past tick (t whole + frac in [0,1)
// toward t+1) and trace o->d against it.
static float net_hist_trace(int e, long t, float frac, v3 o, v3 d, int *part) {
  if (g_hist_tick < 0) return -1.0f;
  long newest = g_hist_tick, oldest = newest - (NET_HIST - 1);
  // A session younger than the ring has no history before tick 0, and without this the
  // window's floor goes NEGATIVE: the clamp below then passes a negative t straight
  // through, and C's % keeps the sign, so the lookup indexes before the array.
  if (oldest < 0) oldest = 0;
  if (t < oldest) { t = oldest; frac = 0.0f; }
  if (t >= newest) { t = newest; frac = 0.0f; }
  const hist_t *a = &g_hist[e][t % NET_HIST];
  const hist_t *b = &g_hist[e][(t + 1) % NET_HIST];
  if (!a->alive) return -1.0f;
  v3 pos = v3_lerp(a->pos, b->alive ? b->pos : a->pos, frac);
  float eye = a->eye + (b->eye - a->eye) * frac;
  float yaw = a->yaw, lean = a->lean + (b->lean - a->lean) * frac;
  int hh = human_of_ent(e);
  return ent_trace_box(pos, eye, yaw, hh >= 0 ? lean : 0.0f, o, d, part);
}

// The lag-comp REWIND GATE. While a network human's move tick runs on the server, this
// holds the server tick his input was aimed at; ent_trace then sweeps the HISTORICAL
// boxes instead of the live ones, so the shot lands where the shooter saw his target,
// not where it stands one RTT later. -1 everywhere else — singleplayer, bots, and the
// predicting client never rewind.
static long g_lagcomp_view = -1;

static float bot_trace_one(const bot_t *b, v3 o, v3 d, int *part) {
  return ent_trace_box(b->pos, bot_eye(b), b->yaw, 0.0f, o, d, part);
}

static float player_trace(const player_t *pl, v3 o, v3 d, int *part) {
  if (!pl->alive) return -1.0f;
  return ent_trace_box(pl->pos, pl->eye, pl->yaw, pl->lean, o, d, part);
}

// Nearest entity along the ray that is closer than *dist (pre-filled with the wall hit,
// so cover genuinely blocks shots).
static int ent_trace(int exclude, v3 o, v3 d, float *dist, int *part) {
  int hit = -1, pt;
  float t;
  ENT_FOREACH(e) {
    if (e == exclude) continue;
    if (g_lagcomp_view >= 0) {           // server, human shooter: rewound boxes
      if ((t = net_hist_trace(e, g_lagcomp_view, 0.0f, o, d, &pt)) > 0 &&
          t < *dist) {
        *dist = t; *part = pt; hit = e;
      }
      continue;
    }
    int h = human_of_ent(e);
    if (h >= 0) {   // player_trace gates on alive itself
      if ((t = player_trace(&g_players[h], o, d, &pt)) > 0 && t < *dist) {
        *dist = t; *part = pt; hit = e;
      }
    } else {
      if (!g_bots[e - 1].alive || !g_bots[e - 1].active) continue;
      if ((t = bot_trace_one(&g_bots[e - 1], o, d, &pt)) > 0 && t < *dist) {
        *dist = t; *part = pt; hit = e;
      }
    }
  }
  return hit;
}

static int ent_alive(int e) {
  int h = human_of_ent(e);
  if (h >= 0) return g_humans_on[h] && g_players[h].alive;
  return g_bots[e - 1].active && g_bots[e - 1].alive;
}

static v3 ent_aimpoint(int e) {  // upper chest
  int h = human_of_ent(e);
  if (h >= 0) {
    // EVERY human's aim point carries the lean offset, not only entity 0's —
    // a leaning human whose aim point stays centred is aim assist pulling
    // wide and bots aiming at where he is not.
    const player_t *pl = &g_players[h];
    v3 o = v3_scale(lean_offset(pl->yaw, pl->lean), LEAN_BODY_K);  // travels with the torso box
    return (v3){{pl->pos.x + o.x, pl->pos.y + pl->eye - ENT_CHEST_OFF, pl->pos.z + o.z}};
  }
  const bot_t *b = &g_bots[e - 1];
  return (v3){{b->pos.x, b->pos.y + bot_eye(b) - ENT_CHEST_OFF, b->pos.z}};
}

static int bot_threat_accept(const bot_t *b, int src, int score) {
  if (src < 0 || src >= MAX_ENTS || !ent_alive(src)) return 0;
  if (b->threat_t <= 0 || b->threat_src < 0 || !ent_alive(b->threat_src))
    return 1;
  if (score != b->threat_score) return score > b->threat_score;
  // threat_t is a bounded evidence age: an equal hit after at least one bot
  // tick is newer, while equal hits in one world tick resolve by entity ID.
  if (b->threat_t < BOT_THREAT_TICKS) return 1;
  return src < b->threat_src;
}

static void bot_focus_invalidate(bot_t *b) {
  // Every commitment that was planned around WHO the target is dies here, and this is
  // the one chokepoint all three "the target identity is changing" sites go through
  // (damage reveal, rescan switch, target lost).
  if (b->flank_t) bot_flank_release(b);
  b->cov_valid = 0;
  b->in_cover = 0;
  b->search_pos = (v3){{0, 0, 0}};
  b->search_index = 0;
  b->search_t = 0;
  b->search_visited = 0;
}

// A FRESH SIGHTING, SEEDED IN ONE PLACE. Both paths that can produce one — eyes on the
// target, and taking a hit from somewhere — record the same eight fields, including the
// 0.20 m fresh-sighting radius and the ONE error-angle draw that decides which way this
// bot will be wrong once it loses sight. bot_belief_aimpoint is the single READER of
// that pair; this is the single writer.
static void bot_belief_seed(bot_t *b, v3 pos, v3 vel) {
  b->last_seen = pos;
  b->last_seen_vel = vel;
  b->belief_pos = pos;
  b->belief_err_a = rng_rangef(&g_rng_bot, -F_PI, F_PI);
  b->belief_radius = 0.20f;
  b->last_seen_tick = g_tick;
  b->search_pos = pos;
  b->lose_t = 0;
}

static void bot_damage_remember(bot_t *b, int src) {
  const bot_skill_t *sk = &BOT_SKILL[g_cfg.sp_diff];
  v3 pos = ent_pos(src);
  b->belief = BOT_BELIEF_OCCLUDED_HOLD;
  bot_belief_seed(b, pos, (v3){{0, 0, 0}});
  b->search_index = 0;
  b->search_dir = 1;
  b->search_t = sk->hold_ticks;
  b->search_visited = 0;
}

static void bot_damage_reveal(bot_t *b, int src, int score) {
  const bot_skill_t *sk = &BOT_SKILL[g_cfg.sp_diff];
  if (b->tgt != src) bot_focus_invalidate(b);
  b->tgt = src;
  bot_damage_remember(b, src);
  b->threat_src = src;
  b->threat_t = BOT_THREAT_TICKS;
  b->threat_score = score;
  b->err = sk->err0;
  b->react_t = (int)(sk->react_ms * 0.6f * (float)TICK_HZ / 1000.0f);
}

// Is the segment o->tp clear of world geometry? The 0.3 m slack — a target is a box,
// not a point, so a wall hugging it does not count as cover — lives here and nowhere
// else.
#define LOS_SLACK 0.3f

// Any-hit occlusion test: TRUE the moment one solid blocks the segment, no nearest-hit
// bookkeeping. los_clear is a boolean question, and answering it with trace_world's
// full min-scan made LOS tracing 61% of the whole sim at 20 bots (measured); the first
// blocking hit answers it identically (min-over-hits >= lim <=> no hit < lim).
static int los_blocked(v3 o, v3 d, float lim) {
  if (lim > GUN_RANGE) return 1;
  // This loop is where the 20-bot sim spends most of its time (gprof: 11.9 of 12.1
  // million ray_aabb calls arrive from here, ~23% of sim self-time), so it looks like
  // the obvious place to optimize.
  for (int i = 0; i < g_num_solids; i++) {
    v3 n;
    float t = ray_aabb(o, d, g_solids[i].min, g_solids[i].max, &n);
    if (t > 0 && t < lim) return 1;
  }
  if (d.y < -1e-8f) {  // arena floor plane, exactly trace_world's test
    float t = -o.y / d.y;
    if (t > 0 && t < lim) {
      float hx = o.x + d.x * t, hz = o.z + d.z * t;
      if (hx > -ARENA_HALF && hx < ARENA_HALF && hz > -ARENA_HALF && hz < ARENA_HALF)
        return 1;
    }
  }
  return 0;
}

static int los_clear(v3 o, v3 tp) {
  v3 d = v3_sub(tp, o);
  float len = v3_len(d);
  return !los_blocked(o, v3_scale(d, 1.0f / len), len - LOS_SLACK);
}

// Line of sight from the bot's eye to the target's aim point. The range reject
// stays BEFORE the trace: it is one subtract against a couple of dozen ray casts.
static int bot_can_see(const bot_t *b, int e, float *dist) {
  v3 o = {{b->pos.x, b->pos.y + bot_eye(b), b->pos.z}};
  v3 tp = ent_aimpoint(e);
  float len = v3_len(v3_sub(tp, o));
  if (len < 0.01f || len > GUN_RANGE) return 0;
  if (!los_clear(o, tp)) return 0;
  *dist = len;
  return 1;
}

// Is `e` inside the horizontal arc `cos_lim` opens in front of the bot? Kept OUT of
// bot_can_see on purpose: that one answers "is the line clear", which is what the
// `lean` proof asks it and what a cover/peek probe means by it.
static int bot_in_arc(const bot_t *b, int e, float cos_lim) {
  v3 tp = ent_aimpoint(e);
  float dx = tp.x - b->pos.x, dz = tp.z - b->pos.z;
  float hl = sqrtf(dx * dx + dz * dz);
  if (hl <= 0.01f) return 1;   // on top of it: the arc has no meaning
  return (dx * sinf(b->yaw) + dz * -cosf(b->yaw)) / hl > cos_lim;
}

