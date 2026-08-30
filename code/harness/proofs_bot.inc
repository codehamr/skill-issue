// =============================================================================
// harness/proofs_bot.inc  —  bothear/botweapon/botflank/botmemory(observe)/
// padbackend_proof, proof_world_save.
// Proof: the proofs themselves. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: botmemory is skill-conditioned — it only exits 0 at `skill
// normal`; verify a failure against a HEAD build before bisecting.
// =============================================================================
// HEARING IS AN AUTHORITATIVE CHANNEL AND ITS NEGATIVES MATTER AS MUCH AS ITS
// POSITIVES. A bot that hears everything has no flank to be caught on and a bot that
// hears nothing cannot be pushed; both read as "the AI is broken" and neither shows up
// in a screenshot. Staged so every arm is asserted in one place: a shot is heard and
// TURNS the bot, a footstep is heard and MOVES it, a CROUCHED walk at 8 m and a step at
// 12 m are NOT heard (crouch=0 / far=0 are PASS values), a louder source wins, the
// bearing carries its error, the memory expires back to patrol, and the bot does not
// fire at a sound it has never seen.
// bothear_proof case results. File-scope (not parameters): the printf/exit
// check at the bottom of bothear_proof needs every one of these, and the
// brief's case interface is `int *fail` only — no second out-param channel.
// Prefixed bh_ to stay clear of -Wshadow across the rest of this one-TU file
// (bare names like `far`/`step`/`move`/`cap` are common enough elsewhere).
static int bh_shot, bh_turn, bh_cap, bh_nofire, bh_move, bh_shot_far, bh_step,
    bh_crouch, bh_slow_quiet, bh_far, bh_step_refresh, bh_cross_hysteresis,
    bh_localised, bh_localised_wall, bh_spin, bh_startle_spin,
    bh_heard_over_memory, bh_seen_beats_heard, bh_priority, bh_expiry;

// The BOTHEAR_STAGE fixture macro is used by every case below, so it has to
// be defined before the first one (a #define is not scoped to a C function)
// and undefined only after the last; bothear_proof itself no longer uses it
// directly, but the staging bracket (proof_world_save/restore) still lives
// there, per the brief.
#define BOTHEAR_STAGE() do {                                               \
    memset(g_humans_on, 0, sizeof g_humans_on);                              \
    g_bots[0] = (bot_t){.pos = {{0,0,0}}, .prev_pos = {{0,0,0}},             \
                         .target = {{0,0,8}}, .hp = PLAYER_HP, .alive = 1,    \
                         .active = 1, .grounded = 1, .eye = EYE_STAND,       \
                         .tgt = -1, .wp.cur = WPN_AR};                       \
    wpn_fill_mags(&g_bots[0].wp);                                            \
    anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw,                 \
               g_bots[0].wp.cur);                                             \
  } while (0)

// A wall intersects this otherwise open shot. The accepted score is also a
// direct guard that the 0.55 attenuation is applied rather than bypassed.
static void bothear_case_wall_shot(int *fail) {
  g_num_solids = 1;
  g_solids[0] = (solid_t){.min = {{4,0,-1}}, .max = {{6,3,1}}, .pen = 0};
  BOTHEAR_STAGE();
  g_humans_on[1] = 1;
  g_players[1] = (player_t){.pos = {{10,0,0}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_tick = 100;
  bothear_emit_shot(ent_of_human(1), (v3){{10,1.0f,0}});
  float yaw_before = g_bots[0].yaw;
  g_tick++;
  bot_tick(0);
  bot_t *b = &g_bots[0];
  // The score is the RELATIVE loudness left at this range — (34-10.05)/34 = 0.7044 —
  // times the 0.55 wall factor.
  bh_shot = b->hear_tick == 100 && b->hear_kind == BOT_NOISE_SHOT &&
             b->hear_score > 0.385f && b->hear_score < 0.390f;
  float turn_delta = fabsf(wrap_pi(b->yaw - yaw_before));
  bh_turn = turn_delta > 1e-4f;
  // A turn onto a SOUND runs at the startle rate, not the tracking rate.
  bh_cap = turn_delta <= BOT_ALERT_TURN * TICK_DT + 1e-4f;
  if (!bh_shot || !bh_turn || !bh_cap) *fail = 1;
}

// The living source is directly in the FOV but behind a wall.
static void bothear_case_nofire(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  g_num_solids = 1;
  g_solids[0] = (solid_t){.min = {{-1,0,-6}}, .max = {{1,3,-4}}, .pen = 0};
  BOTHEAR_STAGE();
  g_humans_on[1] = 1;
  g_players[1] = (player_t){.pos = {{0,0,-10}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_tick = 150;
  bothear_emit_shot(ent_of_human(1), (v3){{0,1.0f,-10}});
  g_tick++;
  bot_tick(0);
  bh_nofire = g_bots[0].tgt == -1 && g_bots[0].react_t == 0 &&
               g_match.shots[1] == 0 &&
               g_bots[0].wp.ammo[WPN_AR] == WPN_DEF[WPN_AR].mag;
  if (!bh_nofire) *fail = 1;
}

// The first several ticks are deliberately turn-only: the investigation
// movement gate must wait until the sound is within 60 degrees of view.
static void bothear_case_move(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  g_num_solids = 0;
  BOTHEAR_STAGE();
  g_tick = 200;
  bothear_emit_shot(2, (v3){{10,1.0f,0}});
  for (int i = 0; i < 32; i++) { g_tick++; bot_tick(0); }
  bh_move = g_bots[0].pos.x > 0.02f;
  if (!bh_move) *fail = 1;
}

static void bothear_case_shot_far(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 300;
  bothear_emit_shot(2, (v3){{35,1.0f,0}});
  g_tick++;
  bot_tick(0);
  bh_shot_far = g_bots[0].hear_tick != 0;
  if (bh_shot_far) *fail = 1;
}

// Footstep reach comes from the same pure source the animation contact uses.
static void bothear_case_step(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 400;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{8,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f));
  g_tick++;
  bot_tick(0);
  bh_step = g_bots[0].hear_tick == 400 &&
             g_bots[0].hear_kind == BOT_NOISE_STEP;
  if (!bh_step) *fail = 1;
}

static void bothear_case_crouch(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 420;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{8,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 1.0f));
  g_tick++;
  bot_tick(0);
  bh_crouch = g_bots[0].hear_tick != 0;
  if (bh_crouch) *fail = 1;
}

static void bothear_case_slow_quiet(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 440;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{4,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN] * 0.2f, 0.0f));
  g_tick++;
  bot_tick(0);
  bh_slow_quiet = g_bots[0].hear_tick == 0;
  if (!bh_slow_quiet) *fail = 1;
}

static void bothear_case_far(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 460;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{12,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f));
  g_tick++;
  bot_tick(0);
  bh_far = g_bots[0].hear_tick != 0;
  if (bh_far) *fail = 1;
}

// The believed SOURCE keeps refreshing itself. ...while a DIFFERENT source at
// a merely similar score does not displace the incumbent. Fused: the second
// half reuses `run_radius` from the first (a local, not a global), so this is
// one case rather than two.
static void bothear_case_step_refresh_xhyst(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 480;
  float run_radius = bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f);
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{6,0,0}}, run_radius);
  g_tick++;
  bot_tick(0);
  v3 first_hear_pos = g_bots[0].hear_pos;
  long first_hear_tick = g_bots[0].hear_tick;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{9.0f,0,0}}, run_radius);  // WEAKER
  g_tick++;
  bot_tick(0);
  bh_step_refresh = g_bots[0].hear_tick > first_hear_tick &&
                     g_bots[0].hear_src == 2 &&
                     v3_len(v3_sub(g_bots[0].hear_pos, first_hear_pos)) > 1e-3f;

  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 490;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{6,0,0}}, run_radius);
  g_tick++;
  bot_tick(0);
  bot_noise_emit(3, BOT_NOISE_STEP, (v3){{-5.8f,0,0}}, run_radius);
  g_tick++;
  bot_tick(0);
  bh_cross_hysteresis = g_bots[0].hear_src == 2;
  if (!bh_step_refresh || !bh_cross_hysteresis) *fail = 1;
}

// LOCALISATION. The stored point is a bearing with error, never the metre the boot
// actually landed on, and the error is bigger through a wall.
static void bothear_case_localise(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 495;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{0,0,10}}, 34.0f);
  g_tick++;
  bot_tick(0);
  float open_err = v3_len(v3_sub(g_bots[0].hear_pos, (v3){{0,0,10}}));
  bh_localised = fabsf(open_err - BOT_HEAR_ERR * 10.0f) < 1e-3f &&
                  g_bots[0].hear_pos.y == 0.0f;
  if (!bh_localised) *fail = 1;
}

static void bothear_case_localise_wall(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  g_num_solids = 1;
  g_solids[0] = (solid_t){.min = {{-2,0,4}}, .max = {{2,3,5}}, .pen = 0};
  BOTHEAR_STAGE();
  g_tick = 497;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{0,0,10}}, 34.0f);
  g_tick++;
  bot_tick(0);
  // TWO-SIDED, and against the exact expression the code evaluates.
  float wall_want = f_clamp(2.0f * BOT_HEAR_ERR * 10.0f, 0.0f, BOT_HEAR_ERR_MAX);
  float wall_err = v3_len(v3_sub(g_bots[0].hear_pos, (v3){{0,0,10}}));
  bh_localised_wall = fabsf(wall_err - wall_want) < 1e-3f;
  g_num_solids = 0;
  if (!bh_localised_wall) *fail = 1;
}

// THE STARTLE, at the angle the whole complaint was about: a boot lands 6 m directly
// BEHIND a patrolling bot.
static void bothear_case_startle_spin(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 700;
  g_bots[0].yaw = g_bots[0].prev_yaw = 0.0f;   // facing -z; the noise is at +z
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{0,0,6}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f));
  bh_spin = -1;
  for (int i = 0; i < 400 && bh_spin < 0; i++) {
    g_tick++;
    bot_tick(0);
    v3 hp = g_bots[0].hear_pos;
    float want = atan2f(hp.x - g_bots[0].pos.x, -(hp.z - g_bots[0].pos.z));
    if (fabsf(wrap_pi(want - g_bots[0].yaw)) < 0.1745f) bh_spin = i + 1;
  }
  bh_startle_spin = bh_spin > 0 && bh_spin <= 72;   // 0.6 s for 180 deg, every tier
  if (!bh_startle_spin) *fail = 1;
}

// REACHABILITY, which is the whole reason the branch was rewritten.
static void bothear_case_heard_over_memory(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_humans_on[1] = 1;
  g_players[1] = (player_t){.pos = {{0,0,-10}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_tick = 800;
  g_bots[0].tgt = ent_of_human(1);
  g_bots[0].belief = BOT_BELIEF_OCCLUDED_HOLD;
  g_bots[0].last_seen = g_bots[0].belief_pos = g_players[1].pos;
  g_bots[0].last_seen_tick = g_tick - 100;   // the memory is OLDER than the boot
  g_bots[0].search_t = 600;
  g_num_solids = 1;
  g_solids[0] = (solid_t){.min = {{-2,0,-6}}, .max = {{2,3,-5}}, .pen = 0};
  float memory_yaw = g_bots[0].yaw = g_bots[0].prev_yaw = F_PI;  // at +z
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{8,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f));
  g_tick++;
  bot_tick(0);
  // The EAR must own this tick, not merely "the yaw moved" — the belief branch would
  // also have turned this fixture (its remembered point is at -z and the bot faces +z),
  // so a bare movement test passed with the whole reachability change reverted.
  float heard_bearing = atan2f(8.0f - g_bots[0].pos.x, -(0.0f - g_bots[0].pos.z));
  bh_heard_over_memory = g_bots[0].tgt == ent_of_human(1) &&
                          g_bots[0].startled == 1 &&
                          fabsf(wrap_pi(g_bots[0].yaw - memory_yaw)) > 1e-4f &&
                          fabsf(wrap_pi(heard_bearing - g_bots[0].yaw)) <
                              fabsf(wrap_pi(heard_bearing - memory_yaw));
  g_num_solids = 0;
  if (!bh_heard_over_memory) *fail = 1;
}

// Same fixture, target VISIBLE: the noise must not move it at all.
static void bothear_case_seen_beats_heard(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_humans_on[1] = 1;
  g_players[1] = (player_t){.pos = {{0,0,-10}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_tick = 900;
  g_bots[0].tgt = ent_of_human(1);
  g_bots[0].scan_t = 100;
  g_bots[0].react_t = 100;   // seen, but not yet allowed to shoot
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{8,0,0}},
                 bot_step_noise_radius(g_cfg.mv[MV_RUN], 0.0f));
  g_tick++;
  bot_tick(0);
  // want_yaw is the target at -z; a startled bot would have turned toward +x.
  bh_seen_beats_heard = g_bots[0].yaw < 0.02f && g_bots[0].yaw > -0.02f;
  if (!bh_seen_beats_heard) *fail = 1;
}

static void bothear_case_priority_expiry(int *fail) {
  memset(g_match.noise, 0, sizeof g_match.noise);
  BOTHEAR_STAGE();
  g_tick = 500;
  bot_noise_emit(2, BOT_NOISE_STEP, (v3){{6,0,0}}, 11.0f);
  bothear_emit_shot(3, (v3){{10,1.0f,0}});
  g_tick++;
  bot_tick(0);
  bh_priority = g_bots[0].hear_kind == BOT_NOISE_SHOT;
  long until = g_bots[0].hear_until;
  v3 patrol_start = g_bots[0].pos;
  g_bots[0].target = v3_add(g_bots[0].pos,
                             (v3){{sinf(g_bots[0].yaw) * 8.0f, 0,
                                   -cosf(g_bots[0].yaw) * 8.0f}});
  g_tick = until + 1;
  bot_tick(0);
  bh_expiry = g_bots[0].hear_tick == 0 && g_bots[0].tgt < 0 &&
               v3_len(v3_sub(g_bots[0].pos, patrol_start)) > 1e-4f;
  if (!bh_priority || !bh_expiry) *fail = 1;
}
#undef BOTHEAR_STAGE

static void bothear_proof(void) {
  proof_world_t w;
  proof_world_save(&w);

  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_bots_frozen = 0;
  g_num_events = 0;

  int fail = 0;
  bothear_case_wall_shot(&fail);
  bothear_case_nofire(&fail);
  bothear_case_move(&fail);
  bothear_case_shot_far(&fail);
  bothear_case_step(&fail);
  bothear_case_crouch(&fail);
  bothear_case_slow_quiet(&fail);
  bothear_case_far(&fail);
  bothear_case_step_refresh_xhyst(&fail);
  bothear_case_localise(&fail);
  bothear_case_localise_wall(&fail);
  bothear_case_startle_spin(&fail);
  bothear_case_heard_over_memory(&fail);
  bothear_case_seen_beats_heard(&fail);
  bothear_case_priority_expiry(&fail);

  const char *pk = bh_priority ? "shot" : "step";
  const char *ex = bh_expiry ? "patrol" : "heard";

  proof_world_restore(&w);
  int events_restored = g_num_events == w.num_events &&
                        memcmp(g_events, w.events, sizeof w.events) == 0;
  printf("bothear shot=%d turn=%d move=%d nofire=%d step=%d crouch=%d far=%d "
         "priority=%s expiry=%s cap=%d refresh=%d xhyst=%d loc=%d locwall=%d "
         "spin=%d startled=%d seenwins=%d\n",
         bh_shot, bh_turn, bh_move, bh_nofire, bh_step, bh_crouch, bh_far,
         pk, ex, bh_cap, bh_step_refresh, bh_cross_hysteresis, bh_localised,
         bh_localised_wall, bh_spin, bh_heard_over_memory, bh_seen_beats_heard);
  if (fail || !events_restored) exit(1);
}

// Deterministic weapon-policy proof. It exercises the production decision and
// request helpers directly, then restores every staged simulation/event field.
static void botweapon_proof(void) {
  proof_world_t w;
  proof_world_save(&w);

  g_tick = 100;
  g_num_events = 0;
  bot_t *b = &g_bots[0];
  *b = (bot_t){.pos = {{3,0,4}}, .active = 1, .alive = 1, .grounded = 1,
               .eye = EYE_STAND, .tgt = -1, .wp.cur = WPN_AR};
  wpn_fill_mags(&b->wp);

  b->wp.ammo[WPN_AR] = 0;
  bot_request_weapon(b, 0, bot_desired_weapon(b, 1, 10.0f, 0.0f));
  const char *empty = WPN_DEF[b->wp.cur].prefix;

  b->wp = (weapon_t){.cur = WPN_AR};
  wpn_fill_mags(&b->wp);
  bot_request_weapon(b, 0, bot_desired_weapon(b, 1, 24.0f, 0.0f));
  const char *far = WPN_DEF[b->wp.cur].prefix;

  b->wp = (weapon_t){.cur = WPN_SR};
  wpn_fill_mags(&b->wp);
  bot_request_weapon(b, 0, bot_desired_weapon(b, 1, 15.9f, 0.0f));
  const char *near = WPN_DEF[b->wp.cur].prefix;

  b->wp = (weapon_t){.cur = WPN_SR};
  wpn_fill_mags(&b->wp);
  bot_request_weapon(b, 0, bot_desired_weapon(b, 1, 24.0f, 5.1f));
  const char *moving = WPN_DEF[b->wp.cur].prefix;

  b->wp = (weapon_t){.cur = WPN_SR};
  wpn_fill_mags(&b->wp);
  b->hear_tick = g_tick;
  b->hear_until = g_tick + 1;
  // An investigation is a NON-engaging state.
  bot_request_weapon(b, 0, bot_desired_weapon(b, 0, 24.0f, 0.0f));
  const char *heard = WPN_DEF[b->wp.cur].prefix;
  b->wp = (weapon_t){.cur = WPN_SR};
  wpn_fill_mags(&b->wp);
  int heard_engaged = bot_desired_weapon(b, 1, 24.0f, 0.0f) == WPN_SR;

  b->hear_tick = b->hear_until = 0;
  b->wp = (weapon_t){.cur = WPN_AR, .switch_t = 1};
  wpn_fill_mags(&b->wp);
  int raise = !bot_request_weapon(b, 0, WPN_SR) && b->wp.cur == WPN_AR;

  b->wp = (weapon_t){.cur = WPN_SR, .reload_t = 7, .reload_tac = 1,
                      .dry_done = 1, .idx = 4};
  wpn_fill_mags(&b->wp);
  b->wp.cool[WPN_SR] = 17;
  int swapped_away = bot_request_weapon(b, 0, WPN_AR);
  int cancelled = g_num_events == 1 && g_events[0].type == EV_RELOAD_CANCEL &&
                  g_events[0].src == 1 && g_events[0].a.x == b->pos.x &&
                  g_events[0].a.y == b->pos.y && g_events[0].a.z == b->pos.z &&
                  g_events[0].wpn == WPN_SR && b->wp.reload_t == 0 &&
                  b->wp.reload_tac == 0 && b->wp.dry_done == 0 && b->wp.idx == 0;
  b->wp.switch_t = 0;
  int swapped_back = bot_request_weapon(b, 0, WPN_SR);
  int cooldown = swapped_away && swapped_back && b->wp.cool[WPN_SR] == 17;

  b->wp = (weapon_t){.cur = WPN_AR};
  wpn_fill_mags(&b->wp);
  int hysteresis = bot_desired_weapon(b, 1, 23.9f, 0.0f) == WPN_AR &&
                   bot_desired_weapon(b, 1, 24.0f, 0.0f) == WPN_SR;
  b->wp.cur = WPN_SR;
  hysteresis = hysteresis && bot_desired_weapon(b, 1, 16.0f, 0.0f) == WPN_SR &&
               bot_desired_weapon(b, 1, 15.9f, 0.0f) == WPN_AR;

  // A policy-triggered SR swap is still raising on its next tick.
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_humans_on[0] = 1;
  g_players[0] = (player_t){.pos = {{0,0,-30}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_num_solids = 0;
  g_num_events = 0;
  g_bots_frozen = 0;
  *b = (bot_t){.pos = {{0,0,0}}, .hp = PLAYER_HP, .alive = 1, .active = 1,
               .grounded = 1, .eye = EYE_STAND, .tgt = 0, .scan_t = 100,
               .react_t = 2, .burst = 3,
               .wp = {.cur = WPN_AR, .switch_t = 2}};
  wpn_fill_mags(&b->wp);
  bot_tick(0);
  int ads_raise = b->wp.switch_t > 0 && b->wp.ads_lin == 0.0f && b->wp.ads_s == 0.0f;

  // A real auditory investigation has no visible target and therefore engaging=0.
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_num_solids = 0;
  g_num_events = 0;
  *b = (bot_t){.pos = {{0,0,0}}, .hp = PLAYER_HP, .alive = 1, .active = 1,
               .grounded = 1, .eye = EYE_STAND, .tgt = -1, .scan_t = 100,
               .burst = 3, .hear_pos = {{0,0,-8}}, .hear_tick = g_tick - 1,
               .hear_until = g_tick + 100, .hear_kind = BOT_NOISE_STEP,
               .hear_score = 3.0f, .wp = {.cur = WPN_SR}};
  wpn_fill_mags(&b->wp);
  bot_tick(0);
  int live_heard = b->tgt == -1 && b->wp.cur == WPN_AR &&
                   b->wp.switch_t > 0 && b->swap_cd > 0;

  // Emergency selection is a combat safety rule, not a policy preference: a
  // visible threat and an empty current magazine must bypass the AI swap
  // cooldown and change to the loaded weapon before reload can start.
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_humans_on[0] = 1;
  g_players[0] = (player_t){.pos = {{0,0,-10}}, .hp = PLAYER_HP, .alive = 1,
                             .grounded = 1, .eye = EYE_STAND};
  g_num_solids = 0;
  g_num_events = 0;
  *b = (bot_t){.pos = {{0,0,0}}, .hp = PLAYER_HP, .alive = 1, .active = 1,
               .grounded = 1, .eye = EYE_STAND, .tgt = 0, .scan_t = 100,
               .react_t = 2, .burst = 3, .swap_cd = 100,
               .wp = {.cur = WPN_AR}};
  b->wp.ammo[WPN_AR] = 0;
  b->wp.ammo[WPN_SR] = WPN_DEF[WPN_SR].mag;
  b->wp.cool[WPN_AR] = 7;
  b->wp.cool[WPN_SR] = 11;
  bot_tick(0);
  int live_emergency = b->wp.cur == WPN_SR && b->wp.switch_t > 0 &&
                       b->wp.reload_t == 0 &&
                       b->swap_cd == BOT_SWAP_COOLDOWN_TICKS &&
                       b->wp.ammo[WPN_AR] == 0 &&
                       b->wp.ammo[WPN_SR] == WPN_DEF[WPN_SR].mag &&
                       b->wp.cool[WPN_AR] == 6 && b->wp.cool[WPN_SR] == 10;

  // A close-range preference may not holster a loaded SR in favour of an
  // empty AR. Drive the same visible-target bot_tick path rather than calling
  // the pure selector in isolation.
  memset(&g_match, 0, sizeof g_match);
  g_match.nbots = 1;
  g_num_events = 0;
  *b = (bot_t){.pos = {{0,0,0}}, .hp = PLAYER_HP, .alive = 1, .active = 1,
               .grounded = 1, .eye = EYE_STAND, .tgt = 0, .scan_t = 100,
               .react_t = 2, .burst = 3, .wp = {.cur = WPN_SR}};
  b->wp.ammo[WPN_AR] = 0;
  b->wp.ammo[WPN_SR] = WPN_DEF[WPN_SR].mag;
  b->wp.cool[WPN_SR] = 13;
  bot_tick(0);
  int live_loaded_sr = b->wp.cur == WPN_SR && b->wp.switch_t == 0 &&
                       b->wp.reload_t == 0 && b->swap_cd == 0 &&
                       b->wp.ammo[WPN_AR] == 0 &&
                       b->wp.ammo[WPN_SR] == WPN_DEF[WPN_SR].mag &&
                       b->wp.cool[WPN_SR] == 12;

  proof_world_restore(&w);
  printf("botweapon empty=%s far=%s near=%s moving=%s heard=%s engaged_deaf=%d "
         "raise=%d cooldown=%s hysteresis=%d\n", empty, far, near, moving,
         heard, heard_engaged, raise, cooldown ? "kept" : "lost", hysteresis);
  if (strcmp(empty, "sr") || strcmp(far, "sr") || strcmp(near, "ar") ||
      strcmp(moving, "ar") || strcmp(heard, "ar") || !heard_engaged ||
      !raise || !cooldown ||
      !hysteresis || !cancelled || !ads_raise || !live_heard ||
      !live_emergency || !live_loaded_sr) exit(1);
}

// A FLANK IS A COMMITMENT, and every way one can go wrong is silent. The anchor must
// be off the direct axis (or it is a charge), inside the arena (or the bot walks into
// the shell), held for its budget (or it aborts the moment the target moves), abandoned
// when the target is gone (or the bot walks to an empty corner forever), cooled down
// after (or every bot flanks every engagement), and DROPPED when the belief that
// started it dies. Staged so each of those is one assertion.
static void botflank_proof(void) {
  proof_world_t w;
  proof_world_save(&w);

  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_num_solids = 0;
  g_num_events = 0;
  g_bots_frozen = 0;
  g_tick = 100;
  // STAGE THE RATE AND THE STREAM. The claim is a per-tick probability, so a rig that
  // drives the real gate otherwise reports on its own luck: this one passed from a cold
  // start and failed after a 7200-tick 20-bot match had advanced g_rng_bot.
  g_rng_bot = 0x9E3779B97F4A7C15ull;
  bot_skill_t rig = BOT_SKILL[g_cfg.sp_diff];
  rig.tac = 1.0f;
  const bot_skill_t *sk = &rig;

  // The anchor swings around the TARGET, both ways, and stays in the arena.
  bot_t *b = &g_bots[0];
  *b = (bot_t){.pos = {{0,0,12}}, .hp = PLAYER_HP, .alive = 1, .active = 1,
                .grounded = 1, .eye = EYE_STAND, .tgt = -1, .wp.cur = WPN_AR};
  v3 tpos = {{0,0,0}};
  v3 left, right;
  int have_l = bot_flank_spot(b, tpos, 1, &left);
  int have_r = bot_flank_spot(b, tpos, -1, &right);
  if (!have_l) left = b->pos;    // never read an unset candidate: the axis and
  if (!have_r) right = b->pos;   // radius tests below both fail on b->pos
  float base = atan2f(b->pos.x - tpos.x, b->pos.z - tpos.z);
  float al = atan2f(left.x - tpos.x, left.z - tpos.z);
  float ar = atan2f(right.x - tpos.x, right.z - tpos.z);
  // >= 45 deg of bearing change is the difference between a flank and a shuffle,
  // and the two sides must land on OPPOSITE sides of the current bearing.
  int axis = have_l && have_r &&
             fabsf(wrap_pi(al - base)) > F_PI_4 &&
             fabsf(wrap_pi(ar - base)) > F_PI_4 &&
             wrap_pi(al - base) * wrap_pi(ar - base) < 0.0f;
  // The RANGE band, which the in-arena/fits filters do not re-check and which is what
  // stops a point-blank fight producing a 2 m shuffle and a 25 m one sending the bot to
  // the far wall.
  float dl = sqrtf((left.x - tpos.x) * (left.x - tpos.x) +
                   (left.z - tpos.z) * (left.z - tpos.z));
  float dr = sqrtf((right.x - tpos.x) * (right.x - tpos.x) +
                   (right.z - tpos.z) * (right.z - tpos.z));
  int inside = have_l && have_r &&
               dl > 7.99f && dl < 14.01f && dr > 7.99f && dr < 14.01f;

  // COMMITMENT: once claimed, the anchor does not move while the bot walks to
  // it. Force the claim by zeroing the cooldown and driving the live leg.
  float wx = 0, wz = 0, pace = 0;
  b->last_seen = tpos;
  b->strafe_dir = 1;
  b->flank_cd = 0;
  int committed = 0, moved = 0;
  for (int i = 0; i < 40000 && !committed; i++) {
    bot_flank_tick(b);
    if (bot_flank_steer(b, sk, 12.0f, &wx, &wz, &pace)) committed = 1;
  }
  v3 anchor = b->flank_p;
  int stable = 1;
  for (int i = 0; i < 60; i++) {
    bot_flank_tick(b);
    if (!bot_flank_steer(b, sk, 12.0f, &wx, &wz, &pace)) { stable = 0; break; }
    if (v3_len(v3_sub(b->flank_p, anchor)) > 1e-6f) { stable = 0; break; }
    if (pace < 0.999f) { stable = 0; break; }
    b->pos.x += wx * 0.05f;   // walk it, so arrival is reachable
    b->pos.z += wz * 0.05f;
    moved = 1;
  }
  int commit = committed && stable && moved;

  // COVER OUTRANKS THE ANGLE: an empty magazine ends the flank on the spot, and it may
  // not restart while the cooldown it set is running.
  b->wp.reload_t = 30;
  bot_flank_tick(b);
  int aborted = b->flank_t == 0 && b->flank_cd > 0 &&
                !bot_flank_steer(b, sk, 12.0f, &wx, &wz, &pace);
  b->wp.reload_t = 0;
  int held_off = 1;
  for (int i = 0; i < 200; i++) {
    bot_flank_tick(b);
    if (bot_flank_steer(b, sk, 12.0f, &wx, &wz, &pace)) { held_off = 0; break; }
  }

  // ARRIVAL releases, and it is COUNTED: standing on the anchor is not a flank
  // in progress, and an uncounted arrival is a census that cannot tell a
  // completed swing from a commitment that quietly timed out.
  b->flank_cd = 0;
  b->flank_t = 300;
  b->flank_p = b->pos;
  int arrived0 = b->flank_arrived;
  int arrival = !bot_flank_steer(b, sk, 12.0f, &wx, &wz, &pace) &&
                b->flank_t == 0 && b->flank_cd > 0 &&
                b->flank_arrived == arrived0 + 1;

  // A LOST TARGET ends the swing: the anchor was planned around somebody who is
  // no longer there, and a frozen commitment resumed against the next enemy is
  // the "it ran off to nowhere" tell.
  b->flank_cd = 0;
  b->flank_t = 300;
  bot_belief_clear(b);
  int dropped = b->flank_t == 0;

  proof_world_restore(&w);
  printf("botflank axis=%d inside=%d commit=%d abort=%d cooldown=%d arrive=%d "
         "dropped=%d\n",
         axis, inside, commit, aborted, held_off, arrival, dropped);
  if (!axis || !inside || !commit || !aborted || !held_off || !arrival ||
      !dropped) exit(1);
}

// THE BOTMEMORY FIXTURE, once.
static void botmem_stage(bot_t seed, v3 hpos) {
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_humans_on[0] = 1;
  g_num_solids = 0;
  g_num_events = 0;
  seed.prev_pos = seed.pos;
  seed.hp = PLAYER_HP;
  seed.alive = 1;
  seed.active = 1;
  seed.grounded = 1;
  seed.eye = EYE_STAND;
  seed.wp.cur = WPN_AR;
  g_bots[0] = seed;
  wpn_fill_mags(&g_bots[0].wp);
  anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw,
             g_bots[0].wp.cur);
  g_players[0] = (player_t){.pos = hpos, .hp = PLAYER_HP,
                            .alive = 1, .grounded = 1, .eye = EYE_STAND};
}

// Deterministic proof that a bot degrades a lost target through belief states
// without firing at hidden knowledge, keeps the entity target past the old
// abrupt memory window, and returns to visible on reacquisition.
// botmemory_proof case results, file-scope for the same reason as bh_* above
// bothear's cases (the driver's printf/exit needs them; the interface is
// `int *fail` only). Prefixed bm_.
static int bm_acquired, bm_after_loss, bm_after_60, bm_after_300,
    bm_reacquired, bm_rear_blind, bm_front_seen, bm_alert_armed,
    bm_alert_closes, bm_alert_down, bm_hidden_retained, bm_hidden_fire,
    bm_crossfire_stable, bm_search_hold_still, bm_fixed_search_anchor,
    bm_search_progress, bm_search_wrapped, bm_search_moved,
    bm_visible_decoy_held, bm_visible_incumbent_held,
    bm_acquisition_fov_held, bm_threat_equal_id, bm_threat_recency, bm_lose_t;

static void botmemory_case_acquire(int *fail) {
  botmem_stage((bot_t){.target = {{0,0,-8}}, .tgt = -1}, (v3){{0,0,-8}});

  g_tick++;
  bot_tick(0);
  bm_acquired = g_bots[0].tgt == 0 &&
                 g_bots[0].belief == BOT_BELIEF_VISIBLE &&
                 g_bots[0].lose_t == 0;
  if (!bm_acquired) *fail = 1;
}

// hidden_fire once meant "fired at a remembered position at all" — the PRE-FIRE
// tactic makes exactly that a designed behaviour. Fused: hidden_fire/
// hidden_retained/old_shots/loss_pos are locals threaded continuously across
// both tick loops and into the reacquire check below, so this stays one case.
static void botmemory_case_occlusion_search_reacquire(int *fail) {
  g_num_solids = 1;
  g_solids[0] = (solid_t){.min = {{-2,0,-5}}, .max = {{2,3,-4}}, .pen = 0};
  bm_hidden_fire = 0;
  bm_hidden_retained = 1;
  int old_shots = g_match.shots[1];

  g_tick++;
  bot_tick(0);
  bm_after_loss = g_bots[0].tgt == 0 &&
                   g_bots[0].belief == BOT_BELIEF_OCCLUDED_HOLD &&
                   g_bots[0].lose_t > 0;
  v3 loss_pos = g_bots[0].pos;
  // Keep the LIVE target sealed away after the loss. Search assertions below
  // must be about the remembered point, not an accidental flank on this fixture.
  g_players[0].pos = (v3){{12, 0, -8}};
  g_solids[1] = (solid_t){.min = {{10,0,-10}}, .max = {{14,3,-6}}, .pen = 0};
  g_num_solids = 2;
  if (g_match.shots[1] != old_shots && !g_bots[0].prefiring) bm_hidden_fire = 1;
  old_shots = g_match.shots[1];
  g_match.nbots = 2;
  g_bots[1] = (bot_t){.pos = {{5,0,-8}}, .hp = PLAYER_HP, .alive = 1,
                       .active = 1, .grounded = 1, .eye = EYE_STAND};
  bm_visible_decoy_held = 1;

  for (int i = 1; i < 60; i++) {
    g_tick++;
    bot_tick(0);
    if (g_bots[0].tgt != 0) bm_visible_decoy_held = 0;
    if (g_bots[0].tgt != 0 || g_bots[0].lose_t <= 0) bm_hidden_retained = 0;
    if (g_match.shots[1] != old_shots && !g_bots[0].prefiring) bm_hidden_fire = 1;
    old_shots = g_match.shots[1];
  }
  bm_after_60 = g_bots[0].tgt == 0 &&
                 (g_bots[0].belief == BOT_BELIEF_INVESTIGATE ||
                  g_bots[0].belief == BOT_BELIEF_SEARCH_SWEEP) &&
                 g_bots[0].lose_t >= 60;
  bm_search_hold_still = v3_len(v3_sub(g_bots[0].pos, loss_pos)) < 0.20f;
  bm_fixed_search_anchor =
      v3_len(v3_sub(g_bots[0].search_pos, g_bots[0].last_seen)) > 0.50f &&
      v3_len(v3_sub(g_bots[0].search_pos, g_bots[0].belief_pos)) > 0.50f;

  for (int i = 60; i < 300; i++) {
    g_tick++;
    bot_tick(0);
    if (g_bots[0].tgt != 0 || g_bots[0].lose_t <= 0) bm_hidden_retained = 0;
    if (g_match.shots[1] != old_shots && !g_bots[0].prefiring) bm_hidden_fire = 1;
    old_shots = g_match.shots[1];
  }
  bm_after_300 = g_bots[0].tgt == 0 &&
                  (g_bots[0].belief == BOT_BELIEF_INVESTIGATE ||
                   g_bots[0].belief == BOT_BELIEF_SEARCH_SWEEP) &&
                  g_bots[0].lose_t >= 300;
  bm_lose_t = g_bots[0].lose_t;
  int reacquire0 = g_bots[0].reacquire_count;

  g_num_solids = 0;
  // Reacquisition needs the target back in the LINE and back in the ARC, so the fixture
  // now states the second precondition instead of leaving it to wherever the search
  // walk happened to leave the bot facing.
  g_bots[0].yaw = g_bots[0].prev_yaw =
      atan2f(g_players[0].pos.x - g_bots[0].pos.x,
             -(g_players[0].pos.z - g_bots[0].pos.z));
  g_tick++;
  bot_tick(0);
  bm_reacquired = g_bots[0].tgt == 0 &&
                   g_bots[0].belief == BOT_BELIEF_VISIBLE &&
                   g_bots[0].lose_t == 0 &&
                   g_bots[0].reacquire_count == reacquire0 + 1;
  bm_search_moved = v3_len(v3_sub(loss_pos, g_bots[0].pos)) > 0.50f;

  if (!bm_after_loss || !bm_after_60 || !bm_search_hold_still ||
      !bm_fixed_search_anchor || !bm_after_300 || !bm_reacquired ||
      !bm_hidden_retained || bm_hidden_fire || !bm_search_moved ||
      !bm_visible_decoy_held) *fail = 1;
}

// THE RETENTION ARC. A target with a clear line but standing directly BEHIND the bot
// is not visible — the acquisition cone used to gate only the first sighting, so an
// incumbent was tracked through the back of the head, which is the oldest "the AI is
// cheating" tell there is.
static void botmemory_case_retention_arc(int *fail) {
  botmem_stage((bot_t){.yaw = 0.0f, .tgt = 0, .belief = BOT_BELIEF_VISIBLE,
                       .scan_t = 100, .react_t = 100, .burst = 3},
               (v3){{0,0,8}});                                  // BEHIND (+z)
  g_tick++;
  bot_tick(0);
  bm_rear_blind = g_bots[0].belief != BOT_BELIEF_VISIBLE && g_match.shots[1] == 0;
  g_bots[0].belief = BOT_BELIEF_VISIBLE;
  g_bots[0].lose_t = 0;
  g_bots[0].scan_t = 100;
  g_players[0].pos = (v3){{0,0,-8}};                             // IN FRONT
  g_tick++;
  bot_tick(0);
  bm_front_seen = g_bots[0].belief == BOT_BELIEF_VISIBLE && g_bots[0].lose_t == 0;
  if (!bm_rear_blind || !bm_front_seen) *fail = 1;
}

// AFTER THE FIGHT. Losing a target must not hand the bot straight back to patrol — it
// walks over and CLEARS the last place it knew about first, and only then stands
// down.
static void botmemory_case_alert(int *fail) {
  botmem_stage((bot_t){.tgt = 0, .belief = BOT_BELIEF_VISIBLE, .scan_t = 4000,
                       .react_t = 100, .burst = 3},
               (v3){{0,0,-12}});
  g_tick++;
  bot_tick(0);                       // sees him: last_seen is (0,0,-12)
  g_players[0].alive = 0;            // ...and the fight ends
  g_tick++;
  bot_tick(0);
  bm_alert_armed = g_bots[0].tgt < 0 && g_bots[0].alert_t > 0 &&
                    v3_len(v3_sub(g_bots[0].alert_p, (v3){{0,0,-12}})) < 0.5f;
  float alert_d0 = v3_len(v3_sub(g_bots[0].pos, g_bots[0].alert_p));
  for (int i = 0; i < 240; i++) { g_tick++; bot_tick(0); }
  // It is CLOSING on the spot, not wandering off on a patrol leg.
  bm_alert_closes =
      v3_len(v3_sub(g_bots[0].pos, g_bots[0].alert_p)) < alert_d0 - 1.0f;
  bm_alert_down = 0;
  for (int i = 0; i < 2400 && !bm_alert_down; i++) {
    g_tick++;
    bot_tick(0);
    bm_alert_down = g_bots[0].alert_t == 0;   // ...and it does stand down again
  }
  if (!bm_alert_armed || !bm_alert_closes || !bm_alert_down) *fail = 1;
}

// A visible incumbent remains authoritative outside the acquisition cone.
// Its hold bonus must participate in the same arbitration as a new frontal
// candidate; a targetless bot must still reject the same side candidate.
static void botmemory_case_incumbent_fov(int *fail) {
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 2;
  g_humans_on[0] = 1;
  g_num_solids = 0;
  g_num_events = 0;
  g_players[0] = (player_t){.pos = {{5,0,0}}, .hp = PLAYER_HP,
                             .alive = 1, .grounded = 1, .eye = EYE_STAND};
  g_bots[0] = (bot_t){.pos = {{0,0,0}}, .prev_pos = {{0,0,0}},
                       .hp = PLAYER_HP, .alive = 1, .active = 1,
                       .grounded = 1, .eye = EYE_STAND, .yaw = 0.0f,
                       .tgt = 0, .belief = BOT_BELIEF_VISIBLE, .scan_t = 1,
                       .react_t = 100, .burst = 3, .wp.cur = WPN_AR};
  wpn_fill_mags(&g_bots[0].wp);
  anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw,
             g_bots[0].wp.cur);
  g_bots[1] = (bot_t){.pos = {{0,0,-8}}, .prev_pos = {{0,0,-8}},
                       .hp = PLAYER_HP, .alive = 1, .active = 1,
                       .grounded = 1, .eye = EYE_STAND};
  g_tick++;
  bot_tick(0);
  bm_visible_incumbent_held = g_bots[0].tgt == 0;

  g_match.nbots = 1;
  g_bots[0].tgt = -1;
  g_bots[0].scan_t = 1;
  g_tick++;
  bot_tick(0);
  bm_acquisition_fov_held = g_bots[0].tgt == -1;
  if (!bm_visible_incumbent_held || !bm_acquisition_fov_held) *fail = 1;
}

// Replanning must consume the finite candidate set. The fixture makes all
// six physical anchors valid and distinct; all six must be consumed before
// the seventh replan wraps to one of them.
static void botmemory_case_search_replan(int *fail) {
  g_num_solids = 0;
  g_bots[0] = (bot_t){.pos = {{0,0,0}}, .last_seen = {{0,0,-8}},
                       .belief_pos = {{1,0,-7}},
                       .last_seen_vel = {{0,0,-1}}, .search_dir = 1};
  v3 search_anchor[7];
  for (int i = 0; i < 7; i++) {
    bot_search_plan_next(&g_bots[0], &BOT_SKILL[g_cfg.sp_diff]);
    search_anchor[i] = g_bots[0].search_pos;
  }
  bm_search_progress = 1;
  for (int i = 0; i < 6; i++)
    for (int j = i + 1; j < 6; j++)
      if (v3_len(v3_sub(search_anchor[i], search_anchor[j])) <= 0.05f)
        bm_search_progress = 0;
  bm_search_wrapped = 0;
  for (int i = 0; i < 6; i++)
    if (v3_len(v3_sub(search_anchor[i], search_anchor[6])) <= 0.05f)
      bm_search_wrapped = 1;
  if (!bm_search_progress || !bm_search_wrapped) *fail = 1;
}

// Seed stronger incumbent evidence, then deliver two weaker crossfire hits
// in both orders. Neither order may downgrade or dislodge the incumbent.
static void botmemory_case_threat_arbitration(int *fail) {
  g_bots[1] = (bot_t){.pos = {{3,0,-8}}, .hp = PLAYER_HP, .alive = 1,
                       .active = 1, .grounded = 1, .eye = EYE_STAND};
  g_players[0] = (player_t){.pos = {{-3,0,-8}}, .hp = PLAYER_HP,
                             .alive = 1, .grounded = 1, .eye = EYE_STAND};
  g_match.nbots = 2;
  g_humans_on[0] = 1;
  bot_t threat_seed = (bot_t){.pos = {{0,0,0}}, .hp = PLAYER_HP,
                               .alive = 1, .active = 1, .grounded = 1,
                               .eye = EYE_STAND, .tgt = 2,
                               .belief = BOT_BELIEF_OCCLUDED_HOLD,
                               .threat_src = 2, .threat_t = BOT_THREAT_TICKS,
                               .threat_score = 48, .wp.cur = WPN_AR};
  g_bots[0] = threat_seed;
  ent_damage(1, 1, 2, 2, g_bots[1].pos, g_bots[0].pos);
  ent_damage(1, 2, 2, 0, g_players[0].pos, g_bots[0].pos);
  int crossfire_a = g_bots[0].tgt == 2 && g_bots[0].threat_src == 2 &&
                    g_bots[0].threat_score == 48 &&
                    g_bots[0].threat_t <= BOT_THREAT_TICKS;
  g_bots[0] = threat_seed;
  ent_damage(1, 2, 2, 0, g_players[0].pos, g_bots[0].pos);
  ent_damage(1, 1, 2, 2, g_bots[1].pos, g_bots[0].pos);
  int crossfire_b = g_bots[0].tgt == 2 && g_bots[0].threat_src == 2 &&
                    g_bots[0].threat_score == 48 &&
                    g_bots[0].threat_t <= BOT_THREAT_TICKS;
  bm_crossfire_stable = crossfire_a && crossfire_b;

  // Equal, same-tick evidence is independent of event order and resolves to
  // the lower entity ID from an empty evidence store.
  bot_t empty_threat = threat_seed;
  empty_threat.tgt = -1;
  empty_threat.threat_src = -1;
  empty_threat.threat_t = 0;
  empty_threat.threat_score = 0;
  g_bots[0] = empty_threat;
  ent_damage(1, 1, 2, 2, g_bots[1].pos, g_bots[0].pos);
  ent_damage(1, 1, 2, 0, g_players[0].pos, g_bots[0].pos);
  int equal_order_a = g_bots[0].tgt == 0 && g_bots[0].threat_src == 0;
  g_bots[0] = empty_threat;
  ent_damage(1, 1, 2, 0, g_players[0].pos, g_bots[0].pos);
  ent_damage(1, 1, 2, 2, g_bots[1].pos, g_bots[0].pos);
  int equal_order_b = g_bots[0].tgt == 0 && g_bots[0].threat_src == 0;
  bm_threat_equal_id = equal_order_a && equal_order_b;

  // Equal evidence from a later tick is newer and wins before entity ID. Same-tick
  // equal evidence remains entity-ID deterministic in the crossfire check above.
  g_bots[0] = threat_seed;
  g_bots[0].tgt = 0;
  g_bots[0].threat_src = 0;
  g_bots[0].threat_t = BOT_THREAT_TICKS - 1;
  ent_damage(1, 3, 2, 2, g_bots[1].pos, g_bots[0].pos);
  bm_threat_recency = g_bots[0].tgt == 2 && g_bots[0].threat_src == 2 &&
                       g_bots[0].threat_score == 48 &&
                       g_bots[0].threat_t == BOT_THREAT_TICKS;
  if (!bm_crossfire_stable || !bm_threat_equal_id || !bm_threat_recency)
    *fail = 1;
}

static void botmemory_proof(void) {
  proof_world_t w;
  proof_world_save(&w);

  g_bots_frozen = 0;
  g_predicting = 1;
  g_tick = 100;

  int fail = 0;
  botmemory_case_acquire(&fail);
  botmemory_case_occlusion_search_reacquire(&fail);
  botmemory_case_retention_arc(&fail);
  botmemory_case_alert(&fail);
  botmemory_case_incumbent_fov(&fail);
  botmemory_case_search_replan(&fail);
  botmemory_case_threat_arbitration(&fail);

  proof_world_restore(&w);
  int nested_ev_peak = g_ev_peak;
  long nested_ev_drops = g_ev_drops;
  int nested_predicting = g_predicting;
  bothear_proof();
  int nested_proof_restored = g_ev_peak == nested_ev_peak &&
                              g_ev_drops == nested_ev_drops &&
                              g_predicting == nested_predicting;
  printf("botmemory acquired=%d loss=%d t60=%d t300=%d reacquired=%d "
         "rearblind=%d frontseen=%d alert=%d/%d/%d "
         "retained=%d hidden_fire=%d crossfire=%d holdstill=%d "
         "searchanchor=%d searchprogress=%d searchwrap=%d searchmove=%d "
         "decoyhold=%d incumbent=%d acquirefov=%d equalid=%d recency=%d "
         "proofrestore=%d lose_t=%d\n",
         bm_acquired, bm_after_loss, bm_after_60, bm_after_300, bm_reacquired,
         bm_rear_blind, bm_front_seen, bm_alert_armed, bm_alert_closes, bm_alert_down,
         bm_hidden_retained, bm_hidden_fire, bm_crossfire_stable, bm_search_hold_still,
         bm_fixed_search_anchor, bm_search_progress, bm_search_wrapped, bm_search_moved,
         bm_visible_decoy_held, bm_visible_incumbent_held, bm_acquisition_fov_held,
         bm_threat_equal_id, bm_threat_recency, nested_proof_restored, bm_lose_t);
  if (fail || !nested_proof_restored) exit(1);
}

// A RE-OBSERVATION MAY NOT DIFFERENCE ACROSS AN OCCLUSION GAP. bot_belief_observe
// estimates the target's velocity only while the bot stayed VISIBLE; differencing a
// fresh position against a stale last_seen over the wrong dt synthesizes a lead the
// bot never saw. Staged: two visible ticks build a velocity, then OCCLUDED_HOLD +
// predict, then a re-observation after a double-length step — last_seen_vel must
// come back at 0.
static void botmemoryobserve_proof(void) {
  proof_world_t w;
  proof_world_save(&w);

  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 1;
  g_humans_on[0] = 1;
  g_tick = 100;
  g_bots[0] = (bot_t){.hp = PLAYER_HP, .alive = 1, .active = 1, .grounded = 1,
                       .eye = EYE_STAND, .tgt = 0};
  g_players[0] = (player_t){.hp = PLAYER_HP, .alive = 1, .grounded = 1,
                             .eye = EYE_STAND};

  float dx = g_cfg.mv[MV_RUN] * TICK_DT * 0.5f;
  g_players[0].pos = (v3){{0, 0, -8}};
  bot_belief_observe(&g_bots[0], 0);
  g_bots[0].last_seen = ent_pos(0);

  g_tick++;
  g_players[0].pos.x += dx;
  bot_belief_observe(&g_bots[0], 0);
  g_bots[0].last_seen = ent_pos(0);

  g_tick++;
  g_bots[0].belief = BOT_BELIEF_OCCLUDED_HOLD;
  bot_belief_predict(&g_bots[0]);

  g_tick++;
  g_players[0].pos.x += dx * 2.0f;
  bot_belief_observe(&g_bots[0], 0);

  float speed = sqrtf(g_bots[0].last_seen_vel.x * g_bots[0].last_seen_vel.x +
                      g_bots[0].last_seen_vel.z * g_bots[0].last_seen_vel.z);

  proof_world_restore(&w);
  printf("botmemoryobserve speed=%.3f\n", speed);
  if (speed > 0.001f) exit(1);
}

static uint32_t proof_hash_bytes(uint32_t h, const void *data, size_t n) {
  const unsigned char *p = (const unsigned char *)data;
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}

static void proof_hash_qpos(np_wr *w, v3 p) {
  np_w16(w, (uint16_t)net_q_pos(p.x));
  np_w16(w, (uint16_t)net_q_pos(p.y));
  np_w16(w, (uint16_t)net_q_pos(p.z));
}

static uint32_t netloop_report_hash(void) {
  unsigned char buf[4096];
  np_wr w = {buf, sizeof buf, 0, 0};

  np_w32(&w, (uint32_t)g_tick);
  np_w32(&w, g_map_seed);
  np_w8(&w, (uint8_t)g_match.nbots);
  np_w8(&w, (uint8_t)g_match.over);
  np_w8(&w, (uint8_t)g_match.winner);
  np_w8(&w, (uint8_t)g_match.restart_req);

  ENT_FOREACH(e) {
    net_ent_t ne;
    net_ent_from_live(&ne, e);
    net_ent_encode(&w, &ne);
    np_w16(&w, (uint16_t)g_match.kills[e]);
    np_w16(&w, (uint16_t)g_match.deaths[e]);
    np_w16(&w, (uint16_t)g_match.shots[e]);
    np_w16(&w, (uint16_t)g_match.hits[e]);
  }

  for (int i = 0; i < g_match.nbots; i++) {
    const bot_t *b = &g_bots[i];
    np_w8(&w, (uint8_t)b->active);
    np_w8(&w, (uint8_t)b->alive);
    np_w8(&w, (uint8_t)b->belief);
    np_w8(&w, (uint8_t)(b->tgt + 1));
    np_w16(&w, (uint16_t)b->lose_t);
    np_w16(&w, (uint16_t)b->search_index);
    np_w16(&w, (uint16_t)b->search_t);
    np_w16(&w, (uint16_t)b->reacquire_count);
    proof_hash_qpos(&w, b->belief_pos);
    proof_hash_qpos(&w, b->search_pos);
    proof_hash_qpos(&w, b->last_seen);
  }

  return w.bad ? 0 : proof_hash_bytes(2166136261u, buf, w.at);
}

static int padbackend_state_equal(const pad_state_t *a, const pad_state_t *b) {
  if (a->connected != b->connected || a->lx != b->lx || a->ly != b->ly ||
      a->rx != b->rx || a->ry != b->ry || a->lt != b->lt || a->rt != b->rt)
    return 0;
  for (int i = 0; i < PB_COUNT; i++)
    if (a->btn[i] != b->btn[i]) return 0;
  return 1;
}

static int padbackend_state_neutral(const pad_state_t *p) {
  return padbackend_state_equal(p, &(pad_state_t){0});
}

static void padbackend_expect(int ok, const char *name, int *cases) {
  (*cases)++;
  if (!ok) {
    fprintf(stderr, "padbackend FAIL %s\n", name);
    exit(1);
  }
}

static void padbackend_expect_standard_button(pad_state_t *out, uint32_t bit,
                                              pad_btn_t expected,
                                              const char *name, int *cases) {
  pad_apply_standard(out, &(pad_standard_state_t){
    .connected = 1, .buttons = bit,
  });
  int one_hot = out->connected;
  for (int b = 0; b < PB_COUNT; b++)
    if (out->btn[b] != (b == (int)expected)) one_hot = 0;
  padbackend_expect(one_hot, name, cases);
}

static void padbackend_expect_xinput_button(pad_state_t *out, uint16_t bit,
                                            pad_btn_t expected,
                                            const char *name, int *cases) {
  pad_apply_xinput(out, &(pad_xinput_raw_t){.buttons = bit});
  int one_hot = out->connected;
  for (int b = 0; b < PB_COUNT; b++)
    if (out->btn[b] != (b == (int)expected)) one_hot = 0;
  padbackend_expect(one_hot, name, cases);
}

// Native Windows adapters are intentionally absent from this Linux harness, so this
// exercises the portable mapping contract through exactly the interfaces they will use.
static void padbackend_expect_hid_button(const pad_hid_raw_t *tpl, int bit,
                                         int expected, float elt, float ert,
                                         const char *name, int *cases) {
  pad_hid_raw_t r = *tpl;
  r.buttons = 1u << bit;
  pad_state_t out;
  pad_apply_hid(&out, &r);
  int ok = out.connected && out.lt == elt && out.rt == ert;
  for (int b = 0; b < PB_COUNT; b++)
    if (out.btn[b] != (b == expected)) ok = 0;
  padbackend_expect(ok, name, cases);
}
// THE THREE BACKEND SHAPES MUST NORMALISE TO ONE g_pad. Windows can hand the same
// physical controller to either XInput or DirectInput8 and Linux hands it over as an
// evdev/HID report, and each names its axes and buttons differently — so a mapper that
// is right for one and wrong for another is a pad that half works on one machine and
// nowhere else, with nothing reporting an error. Every button and both triggers, in
// all three shapes, against the one shared vocabulary.
// padbackend_proof cases take `int *cases` rather than the generic `int
// *fail`: every assertion already goes through padbackend_expect (defined
// above, unchanged), which exits(1) itself with a named diagnostic on the
// first failure — there never was an aggregate fail flag to accumulate here,
// only the running `cases` counter the pre-existing helpers already thread
// by pointer. `out`'s value never needs to survive a case boundary: every
// pad_apply_* call fully overwrites it (memset on disconnect, unconditional
// full-field rebuild via pad_apply_standard otherwise), so each case is free
// to declare its own local `out`.
static void padbackend_case_standard(int *cases) {
  pad_state_t out = {0};
  pad_standard_state_t sample = {
    .connected = 1, .lx = 0.5f, .ly = -0.25f, .rx = -0.75f, .ry = 1.0f,
    .buttons = 0x0004u,
  };

  pad_apply_standard(&out, &sample);
  padbackend_expect(out.connected && out.lx == 0.5f && out.ly == -0.25f &&
                    out.rx == -0.75f && out.ry == 1.0f && out.btn[PB_A],
                    "normal-a-stick", cases);

  pad_apply_standard(&out, &(pad_standard_state_t){0});
  padbackend_expect(padbackend_state_neutral(&out), "zero", cases);

  pad_apply_standard(&out, &(pad_standard_state_t){
    .connected = 1, .lx = -2.0f, .ly = 2.0f, .rx = -1.0f, .ry = 1.0f,
  });
  padbackend_expect(out.lx == -1.0f && out.ly == 1.0f &&
                    out.rx == -1.0f && out.ry == 1.0f, "stick-bounds", cases);

  pad_apply_standard(&out, &(pad_standard_state_t){
    .connected = 1, .lt = -0.5f, .rt = 1.5f,
  });
  padbackend_expect(out.lt == 0.0f && out.rt == 1.0f, "trigger-bounds", cases);

  padbackend_expect_standard_button(&out, 0x0001u, PB_START,  "menu-start",  cases);
  padbackend_expect_standard_button(&out, 0x0002u, PB_SELECT, "view-select", cases);
  padbackend_expect_standard_button(&out, 0x0004u, PB_A,      "a",           cases);
  padbackend_expect_standard_button(&out, 0x0008u, PB_B,      "b",           cases);
  padbackend_expect_standard_button(&out, 0x0010u, PB_X,      "x",           cases);
  padbackend_expect_standard_button(&out, 0x0020u, PB_Y,      "y",           cases);
  padbackend_expect_standard_button(&out, 0x0040u, PB_UP,     "dpad-up",     cases);
  padbackend_expect_standard_button(&out, 0x0080u, PB_DOWN,   "dpad-down",   cases);
  padbackend_expect_standard_button(&out, 0x0100u, PB_LEFT,   "dpad-left",   cases);
  padbackend_expect_standard_button(&out, 0x0200u, PB_RIGHT,  "dpad-right",  cases);
  padbackend_expect_standard_button(&out, 0x0400u, PB_LB,     "lb",          cases);
  padbackend_expect_standard_button(&out, 0x0800u, PB_RB,     "rb",          cases);
  padbackend_expect_standard_button(&out, 0x1000u, PB_L3,     "l3",          cases);
  padbackend_expect_standard_button(&out, 0x2000u, PB_R3,     "r3",          cases);
}

static void padbackend_case_xinput(int *cases) {
  pad_state_t out = {0};
  pad_apply_xinput(&out, &(pad_xinput_raw_t){
    .lx = -32768, .ly = 0, .rx = 32767, .ry = -16384,
    .lt = 0, .rt = 255,
  });
  padbackend_expect(out.lx == -1.0f && out.ly == 0.0f && out.rx == 1.0f &&
                    out.ry < -0.49f && out.ry > -0.51f && out.lt == 0.0f &&
                    out.rt == 1.0f, "xinput-ranges", cases);
  padbackend_expect_xinput_button(&out, 0x0001u, PB_UP, "xi-dpad-up", cases);
  padbackend_expect_xinput_button(&out, 0x0002u, PB_DOWN, "xi-dpad-down", cases);
  padbackend_expect_xinput_button(&out, 0x0004u, PB_LEFT, "xi-dpad-left", cases);
  padbackend_expect_xinput_button(&out, 0x0008u, PB_RIGHT, "xi-dpad-right", cases);
  padbackend_expect_xinput_button(&out, 0x0010u, PB_START, "xi-start", cases);
  padbackend_expect_xinput_button(&out, 0x0020u, PB_SELECT, "xi-select", cases);
  padbackend_expect_xinput_button(&out, 0x0040u, PB_L3, "xi-l3", cases);
  padbackend_expect_xinput_button(&out, 0x0080u, PB_R3, "xi-r3", cases);
  padbackend_expect_xinput_button(&out, 0x0100u, PB_LB, "xi-lb", cases);
  padbackend_expect_xinput_button(&out, 0x0200u, PB_RB, "xi-rb", cases);
  padbackend_expect_xinput_button(&out, 0x1000u, PB_A, "xi-a", cases);
  padbackend_expect_xinput_button(&out, 0x2000u, PB_B, "xi-b", cases);
  padbackend_expect_xinput_button(&out, 0x4000u, PB_X, "xi-x", cases);
  padbackend_expect_xinput_button(&out, 0x8000u, PB_Y, "xi-y", cases);
}

// pad_apply_hid: the DirectInput layout mapper, per confirmed device shape.
// Kept as one case (vendor-specific + the one-hot sweep + the dongle/DualShock/
// X360/Switch/slider shapes + POV + disconnect): AX_XYZRZ/AX_SIX are locals
// shared across all of it, and the brief's own rule for a shared local is to
// keep its cases together rather than thread a new parameter.
static void padbackend_case_hid(int *cases) {
  pad_state_t out = {0};
  const uint32_t AX_XYZRZ = 1u << PH_X | 1u << PH_Y | 1u << PH_Z | 1u << PH_RZ;
  const uint32_t AX_SIX = AX_XYZRZ | 1u << PH_RX | 1u << PH_RY;
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x2DC8, .axes = AX_XYZRZ, .pov = -1,
    .ax = {[PH_X] = 0.5f, [PH_Y] = 0.5f, [PH_Z] = -0.25f, [PH_RZ] = 1.0f},
  });
  padbackend_expect(out.connected && out.lx == 0.5f && out.ly == -0.5f &&
                    out.rx == -0.25f && out.ry == -1.0f &&
                    out.lt == 0.0f && out.rt == 0.0f,
                    "hid-8bitdo-bt-sticks", cases);

  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x2DC8, .axes = AX_XYZRZ, .pov = -1,
    .buttons = 1u << 10 | 1u << 11 | 1u << 3 | 1u << 13 | 1u << 2,
  });
  padbackend_expect(out.btn[PB_SELECT] && out.btn[PB_START] &&
                    out.btn[PB_X] && out.btn[PB_L3] && out.btn[PB_P3] &&
                    !out.btn[PB_A] && !out.btn[PB_B] &&
                    !out.btn[PB_Y] && !out.btn[PB_R3],
                    "hid-8bitdo-bt-buttons", cases);

  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x2DC8, .axes = AX_XYZRZ, .pov = -1,
    .buttons = 1u << 8 | 1u << 9,
  });
  padbackend_expect(out.lt == 1.0f && out.rt == 1.0f,
                    "hid-8bitdo-bt-trig", cases);

  // One-hot over every vendor table, every mapped AND every gap bit.
  const pad_hid_raw_t TPL_8BITDO = {.connected = 1, .vid = 0x2DC8,
                                    .axes = AX_XYZRZ, .pov = -1};
  const pad_hid_raw_t TPL_SONY = {.connected = 1, .vid = 0x054C,
                                  .axes = AX_SIX, .pov = -1,
                                  .ax = {[PH_RX] = -1.0f, [PH_RY] = -1.0f}};
  const pad_hid_raw_t TPL_SWITCH = {.connected = 1, .vid = 0x057E, .pov = -1,
    .axes = 1u << PH_X | 1u << PH_Y | 1u << PH_RX | 1u << PH_RY};
  const pad_hid_raw_t TPL_XBOX = {.connected = 1, .axes = AX_XYZRZ, .pov = -1};
  struct { const pad_hid_raw_t *tpl; const char *name;
           struct { int8_t bit, pb; float lt, rt; } row[20]; } sweeps[] = {
    {&TPL_8BITDO, "hid-onehot-8bitdo", {
      {0, PB_A, 0, 0},  {1, PB_B, 0, 0},  {2, PB_P3, 0, 0}, {3, PB_X, 0, 0},
      {4, PB_Y, 0, 0},  {5, PB_P4, 0, 0}, {6, PB_LB, 0, 0}, {7, PB_RB, 0, 0},
      {8, -1, 1, 0},    {9, -1, 0, 1},    {10, PB_SELECT, 0, 0},
      {11, PB_START, 0, 0}, {12, -1, 0, 0}, {13, PB_L3, 0, 0},
      {14, PB_R3, 0, 0}, {15, -1, 0, 0}, {16, PB_P2, 0, 0},
      {17, PB_P1, 0, 0}, {-1, 0, 0, 0}}},
    {&TPL_SONY, "hid-onehot-sony", {
      {0, PB_X, 0, 0},  {1, PB_A, 0, 0},  {2, PB_B, 0, 0},  {3, PB_Y, 0, 0},
      {4, PB_LB, 0, 0}, {5, PB_RB, 0, 0}, {6, -1, 1, 0},    {7, -1, 0, 1},
      {8, PB_SELECT, 0, 0}, {9, PB_START, 0, 0}, {10, PB_L3, 0, 0},
      {11, PB_R3, 0, 0}, {12, -1, 0, 0}, {-1, 0, 0, 0}}},
    {&TPL_SWITCH, "hid-onehot-switch", {
      {0, PB_A, 0, 0},  {1, PB_B, 0, 0},  {2, PB_X, 0, 0},  {3, PB_Y, 0, 0},
      {4, PB_LB, 0, 0}, {5, PB_RB, 0, 0}, {6, -1, 1, 0},    {7, -1, 0, 1},
      {8, PB_SELECT, 0, 0}, {9, PB_START, 0, 0}, {10, PB_L3, 0, 0},
      {11, PB_R3, 0, 0}, {12, -1, 0, 0}, {13, -1, 0, 0}, {-1, 0, 0, 0}}},
    {&TPL_XBOX, "hid-onehot-generic", {
      {0, PB_A, 0, 0},  {1, PB_B, 0, 0},  {2, PB_X, 0, 0},  {3, PB_Y, 0, 0},
      {4, PB_LB, 0, 0}, {5, PB_RB, 0, 0}, {6, PB_SELECT, 0, 0},
      {7, PB_START, 0, 0}, {8, PB_L3, 0, 0}, {9, PB_R3, 0, 0},
      {10, -1, 0, 0}, {-1, 0, 0, 0}}},
  };
  for (size_t s = 0; s < sizeof sweeps / sizeof sweeps[0]; s++)
    for (int i = 0; i < 20 && sweeps[s].row[i].bit >= 0; i++)
      padbackend_expect_hid_button(sweeps[s].tpl, sweeps[s].row[i].bit,
                                   sweeps[s].row[i].pb, sweeps[s].row[i].lt,
                                   sweeps[s].row[i].rt, sweeps[s].name, cases);

  // Same vendor over the 2.4G dongle in D-input mode: all six axes exist and
  // the right stick moves to Rx/Ry; Z/Rz must then be IGNORED, not triggers.
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x2DC8, .axes = AX_SIX, .pov = -1,
    .ax = {[PH_RX] = 0.5f, [PH_RY] = -1.0f, [PH_Z] = 0.9f, [PH_RZ] = 0.9f},
  });
  padbackend_expect(out.rx == 0.5f && out.ry == 1.0f &&
                    out.lt == 0.0f && out.rt == 0.0f,
                    "hid-8bitdo-dongle", cases);

  // DualShock shape: right stick Z/Rz, ANALOG triggers Rx/Ry resting at -1.
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x054C, .axes = AX_SIX, .pov = -1,
    .ax = {[PH_Z] = 0.5f, [PH_RZ] = -0.5f, [PH_RX] = -1.0f, [PH_RY] = 0.0f},
    .buttons = 1u << 1 | 1u << 0 | 1u << 8,
  });
  padbackend_expect(out.rx == 0.5f && out.ry == 0.5f &&
                    out.lt == 0.0f && out.rt == 0.5f &&
                    out.btn[PB_A] && out.btn[PB_X] && out.btn[PB_SELECT] &&
                    !out.btn[PB_B], "hid-sony", cases);

  // Xbox-through-DInput shape: right stick Rx/Ry, both triggers folded onto
  // one Z axis (LT +, RT -).
  const uint32_t AX_X360 = 1u << PH_X | 1u << PH_Y | 1u << PH_Z |
                           1u << PH_RX | 1u << PH_RY;
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .axes = AX_X360, .pov = -1,
    .ax = {[PH_RX] = 0.25f, [PH_RY] = 0.75f, [PH_Z] = 1.0f},
  });
  padbackend_expect(out.rx == 0.25f && out.ry == -0.75f &&
                    out.lt == 1.0f && out.rt == 0.0f, "hid-x360-lt", cases);
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .axes = AX_X360, .pov = -1, .ax = {[PH_Z] = -1.0f},
  });
  padbackend_expect(out.lt == 0.0f && out.rt == 1.0f, "hid-x360-rt", cases);

  // Switch Pro shape: sticks on X/Y + Rx/Ry, everything else digital.
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .vid = 0x057E, .pov = -1,
    .axes = 1u << PH_X | 1u << PH_Y | 1u << PH_RX | 1u << PH_RY,
    .ax = {[PH_RX] = 1.0f}, .buttons = 1u << 6 | 1u << 8 | 1u << 9,
  });
  padbackend_expect(out.rx == 1.0f && out.lt == 1.0f && out.rt == 0.0f &&
                    out.btn[PB_SELECT] && out.btn[PB_START], "hid-switch", cases);

  // Slider triggers (rest -1), and the generic Xbox-order button guess.
  pad_apply_hid(&out, &(pad_hid_raw_t){
    .connected = 1, .axes = AX_XYZRZ | 1u << PH_S0 | 1u << PH_S1, .pov = -1,
    .ax = {[PH_S0] = 1.0f, [PH_S1] = -1.0f}, .buttons = 1u << 6 | 1u << 7,
  });
  padbackend_expect(out.lt == 1.0f && out.rt == 0.0f &&
                    out.btn[PB_SELECT] && out.btn[PB_START],
                    "hid-generic-sliders", cases);

  // POV decoding: hundredths of a degree CW from north, -1 centered, and the
  // 8-sector rounding that makes 35999 read as "up" again.
  struct { int pov; int up, dn, lf, rt_; } povs[] = {
    {-1, 0, 0, 0, 0},    {0, 1, 0, 0, 0},     {4500, 1, 0, 0, 1},
    {9000, 0, 0, 0, 1},  {18000, 0, 1, 0, 0}, {27000, 0, 0, 1, 0},
    {31500, 1, 0, 1, 0}, {35999, 1, 0, 0, 0},
  };
  for (size_t i = 0; i < sizeof povs / sizeof povs[0]; i++) {
    pad_apply_hid(&out, &(pad_hid_raw_t){
      .connected = 1, .axes = AX_XYZRZ, .pov = povs[i].pov,
    });
    padbackend_expect(out.btn[PB_UP] == povs[i].up &&
                      out.btn[PB_DOWN] == povs[i].dn &&
                      out.btn[PB_LEFT] == povs[i].lf &&
                      out.btn[PB_RIGHT] == povs[i].rt_, "hid-pov", cases);
  }

  // A pump that reports nothing must fully clear a previously live state —
  // the mapper is all-or-nothing, never a field merge.
  pad_apply_hid(&out, &(pad_hid_raw_t){0});
  padbackend_expect(padbackend_state_neutral(&out), "hid-disconnect", cases);
}

static void padbackend_proof(void) {
  int cases = 0;
  padbackend_case_standard(&cases);
  padbackend_case_xinput(&cases);
  padbackend_case_hid(&cases);
  printf("padbackend ok cases=%d\n", cases);
}

// --- Lean-step presentation contract ---------------------------------------
// This proof deliberately lives beside proof_world_save/restore.  The step is a
// presentation solve, but it consults the simulation's solid list; leaving one of
// these synthetic walls behind would make every proof after it depend on command
// order.  All dimensions of the step itself come from skeleton.inc.  The numbers
// below are acceptance tolerances or fixture extents, never a second pose definition.
#define LS_SYM_M       0.005f
#define LS_SYM_DEG     2.0f
#define LS_PEN_M       0.0005f
#define LS_SINK_M      0.002f
#define LS_HEAD_GAP_M  0.0015f

enum { LS_OPEN, LS_SIDEWALL, LS_CORNER, LS_SLOT };
static const char *LS_PART[3] = {"outsole", "boot", "bootcuff"};
static const float LS_ALPHA[5] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};

typedef struct {
  const char *name;
  int side, active, gait_same, event_same, scan_ok, renderer_ok, one_side;
  float lean, want_mm, got_mm, ratio, knee[2], flex, hip_drop_mm;
  float sink_mm, reach_mm, head_gap_mm, wall_mm;
  float ctrl_knee;
  v3 hip, ankle;
  float aabb_mm[3][5], mesh_mm[3][5];
  long scans, ticks;
} leanstep_case_t;

static float leanstep_knee_deg(const pose_t *ls_pose, int ls_side) {
  v3 ls_u = v3_sub(ls_pose->hip[ls_side], ls_pose->knee[ls_side]);
  v3 ls_l = v3_sub(ls_pose->ankle[ls_side], ls_pose->knee[ls_side]);
  float ls_ul = v3_len(ls_u), ls_ll = v3_len(ls_l);
  if (ls_ul < 1e-6f || ls_ll < 1e-6f) return 0.0f;
  return acosf(f_clamp(v3_dot(ls_u, ls_l) / (ls_ul * ls_ll), -1.0f, 1.0f)) /
         DEG2RAD;
}

static int leanstep_gait_same(const anim_t *ls_a, const anim_t *ls_b) {
  return !memcmp(ls_a->foot, ls_b->foot, sizeof ls_a->foot) &&
         !memcmp(ls_a->stance, ls_b->stance, sizeof ls_a->stance) &&
         !memcmp(&ls_a->phase, &ls_b->phase, sizeof ls_a->phase);
}

static int leanstep_event_is_step(const event_t *ls_ev, int ls_n) {
  for (int ls_i = 0; ls_i < ls_n; ls_i++)
    if (ls_ev[ls_i].type == EV_STEP) return 1;
  return 0;
}

static int leanstep_noise_is_step(
    const bot_noise_t ls_noise[BOT_NOISE_COUNT][MAX_ENTS]) {
  for (int ls_i = 0; ls_i < MAX_ENTS; ls_i++)
    if (ls_noise[BOT_NOISE_STEP][ls_i].tick ||
        ls_noise[BOT_NOISE_STEP][ls_i].radius != 0.0f) return 1;
  return 0;
}

static int leanstep_wire(int ls_ent, unsigned char *ls_buf, size_t ls_cap) {
  net_ent_t ls_ne;
  net_ent_from_live(&ls_ne, ls_ent);
  np_wr ls_wr = {ls_buf, ls_cap, 0, 0};
  net_ent_encode(&ls_wr, &ls_ne);
  return ls_wr.bad ? -1 : (int)ls_wr.at;
}

// Signed distance to an AABB: positive outside, negative inside.  The mesh pass
// uses this for the true rendered vertices; a separate range-AABB pass below catches
// the rare large triangle whose surface crosses a wall without placing a vertex in it.
static float leanstep_point_box(v3 ls_p, const solid_t *ls_s) {
  float ls_d2 = 0.0f;
  int ls_inside = 1;
  for (int ls_k = 0; ls_k < 3; ls_k++) {
    float ls_d = 0.0f;
    if (ls_p.e[ls_k] < ls_s->min.e[ls_k]) {
      ls_d = ls_s->min.e[ls_k] - ls_p.e[ls_k];
      ls_inside = 0;
    } else if (ls_p.e[ls_k] > ls_s->max.e[ls_k]) {
      ls_d = ls_p.e[ls_k] - ls_s->max.e[ls_k];
      ls_inside = 0;
    }
    ls_d2 += ls_d * ls_d;
  }
  if (!ls_inside) return sqrtf(ls_d2);
  float ls_in = 1e9f;
  for (int ls_k = 0; ls_k < 3; ls_k++) {
    ls_in = fminf(ls_in, ls_p.e[ls_k] - ls_s->min.e[ls_k]);
    ls_in = fminf(ls_in, ls_s->max.e[ls_k] - ls_p.e[ls_k]);
  }
  return -ls_in;
}

static float leanstep_range_aabb_pen(int ls_v0, int ls_v1,
                                     const solid_t *ls_s) {
  v3 ls_lo = {{1e9f, 1e9f, 1e9f}}, ls_hi = {{-1e9f, -1e9f, -1e9f}};
  for (int ls_f = ls_v0; ls_f + VERT_FLOATS <= ls_v1; ls_f += VERT_FLOATS)
    for (int ls_k = 0; ls_k < 3; ls_k++) {
      float ls_x = g_scene[ls_f + ls_k];
      if (ls_x < ls_lo.e[ls_k]) ls_lo.e[ls_k] = ls_x;
      if (ls_x > ls_hi.e[ls_k]) ls_hi.e[ls_k] = ls_x;
    }
  float ls_pen = 1e9f;
  for (int ls_k = 0; ls_k < 3; ls_k++) {
    float ls_ov = fminf(ls_hi.e[ls_k], ls_s->max.e[ls_k]) -
                  fmaxf(ls_lo.e[ls_k], ls_s->min.e[ls_k]);
    if (ls_ov <= 0.0f) return 0.0f;
    if (ls_ov < ls_pen) ls_pen = ls_ov;
  }
  return ls_pen;
}

static void leanstep_stage_solids(int ls_kind, int ls_side, float ls_inner) {
  g_num_solids = 0;
  if (ls_kind == LS_OPEN) return;
  if (ls_kind == LS_SLOT) {
    g_solids[g_num_solids++] = (solid_t){
      .min = {{ls_inner, -0.25f, -2.0f}},
      .max = {{ls_inner + 2.0f, 2.5f, 2.0f}}, .pen = 0};
    g_solids[g_num_solids++] = (solid_t){
      .min = {{-ls_inner - 2.0f, -0.25f, -2.0f}},
      .max = {{-ls_inner, 2.5f, 2.0f}}, .pen = 0};
  } else if (ls_side) {
    g_solids[g_num_solids++] = (solid_t){
      .min = {{ls_inner, -0.25f, -2.0f}},
      .max = {{ls_inner + 2.0f, 2.5f, 2.0f}}, .pen = 0};
  } else {
    g_solids[g_num_solids++] = (solid_t){
      .min = {{-ls_inner - 2.0f, -0.25f, -2.0f}},
      .max = {{-ls_inner, 2.5f, 2.0f}}, .pen = 0};
  }
  if (ls_kind == LS_CORNER)
    g_solids[g_num_solids++] = (solid_t){
      .min = {{-2.0f, -0.25f, -2.0f}},
      .max = {{2.0f, 2.5f, -0.25f}}, .pen = 0};
}

static void leanstep_mesh_measure(float ls_aabb[3], float ls_mesh[3],
                                  int ls_seen[3], float *ls_wall) {
  for (int ls_part = 0; ls_part < 3; ls_part++) {
    ls_aabb[ls_part] = 0.0f;
    ls_mesh[ls_part] = 0.0f;
    ls_seen[ls_part] = 0;
  }
  *ls_wall = g_num_solids ? 1e9f : 0.0f;
  for (int ls_i = 0; ls_i < g_nprobe; ls_i++) {
    int ls_part = -1;
    if (!g_probe[ls_i].name) continue;
    for (int ls_k = 0; ls_k < 3; ls_k++)
      if (!strcmp(g_probe[ls_i].name, LS_PART[ls_k])) ls_part = ls_k;
    if (ls_part < 0) continue;
    if (g_probe[ls_i].v1 - g_probe[ls_i].v0 < VERT_FLOATS) continue;
    ls_seen[ls_part]++;
    for (int ls_s = 0; ls_s < g_num_solids; ls_s++) {
      float ls_ap = leanstep_range_aabb_pen(g_probe[ls_i].v0,
                                             g_probe[ls_i].v1,
                                             &g_solids[ls_s]);
      if (ls_ap > ls_aabb[ls_part]) ls_aabb[ls_part] = ls_ap;
      for (int ls_f = g_probe[ls_i].v0;
           ls_f + VERT_FLOATS <= g_probe[ls_i].v1; ls_f += VERT_FLOATS) {
        v3 ls_p = {{g_scene[ls_f], g_scene[ls_f + 1], g_scene[ls_f + 2]}};
        float ls_sd = leanstep_point_box(ls_p, &g_solids[ls_s]);
        if (ls_sd < *ls_wall) *ls_wall = ls_sd;
        if (ls_sd < 0.0f && -ls_sd > ls_mesh[ls_part])
          ls_mesh[ls_part] = -ls_sd;
      }
    }
  }
  if (*ls_wall > 1e8f) *ls_wall = 0.0f;
}

static void leanstep_render_sweep(const anim_t *ls_anim, v3 ls_pos,
                                  float ls_yaw, leanstep_case_t *ls_res) {
  ls_res->renderer_ok = 1;
  ls_res->wall_mm = 1e9f;
  for (int ls_ai = 0; ls_ai < 5; ls_ai++) {
    scene_reset();
    g_nprobe = 0;
    g_probe_on = 1;
    long ls_scan0 = g_lean_step_scans;
    scene_draw_figure(ls_anim, ls_pos, ls_yaw, NULL, LS_ALPHA[ls_ai], 0.0f,
                      (v3){{1, 1, 1}}, WPN_AR, (gunanim_t){0});
    probe(NULL);
    g_probe_on = 0;
    if (g_lean_step_scans != ls_scan0) ls_res->renderer_ok = 0;
    float ls_wall;
    float ls_ab[3], ls_me[3];
    int ls_seen[3];
    leanstep_mesh_measure(ls_ab, ls_me, ls_seen, &ls_wall);
    if (ls_wall < ls_res->wall_mm) ls_res->wall_mm = ls_wall;
    for (int ls_part = 0; ls_part < 3; ls_part++) {
      if (!ls_seen[ls_part]) ls_res->renderer_ok = 0;
      ls_res->aabb_mm[ls_part][ls_ai] = ls_ab[ls_part] * 1000.0f;
      ls_res->mesh_mm[ls_part][ls_ai] = ls_me[ls_part] * 1000.0f;
    }
  }
  if (ls_res->wall_mm > 1e8f) ls_res->wall_mm = 0.0f;
  else ls_res->wall_mm *= 1000.0f;
}

// One production tick against an otherwise identical non-leaning control.  For air
// and slide, ls_clear first creates a real step, then proves that the next tick clears
// it without scanning.  Crouch is settled before the measured tick so its production
// blend, rather than a hand-authored factor, drives the result.
static leanstep_case_t leanstep_case(const char *ls_name, int ls_kind, int ls_side,
                                     float ls_lean_abs, int ls_crouch,
                                     v3 ls_vel, int ls_grounded, int ls_slide,
                                     int ls_clear, float ls_wall_k) {
  leanstep_case_t ls_res = {.name = ls_name, .side = ls_side,
                            .lean = ls_side ? ls_lean_abs : -ls_lean_abs};
  v3 ls_pos = {{0, 0, 0}};
  anim_t ls_ctrl, ls_step;
  g_num_solids = 0;
  anim_reset(&ls_ctrl, ls_pos, 0.0f, WPN_AR);
  anim_reset(&ls_step, ls_pos, 0.0f, WPN_AR);

  for (int ls_i = 0; ls_i < (ls_crouch ? 80 : 0); ls_i++) {
    g_tick++;
    anim_tick(&ls_ctrl, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1,
              ls_crouch, 0.0f, 0, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
    anim_tick(&ls_step, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1,
              ls_crouch, 0.0f, 0, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
  }

  float ls_home = fabsf(ls_step.foot[ls_side].pos.x - ls_pos.x);
  float ls_inner = ls_home + LEAN_STEP_MARGIN_M + LEAN_STEP_OUT_M * ls_wall_k;
  leanstep_stage_solids(ls_kind, ls_side, ls_inner);
  int ls_pre_gait = 1, ls_pre_scan = 1, ls_pre_quiet = 1;
  long ls_pre_scans = 0, ls_pre_ticks = 0;
  int ls_settle = !ls_clear && ls_grounded && !ls_slide &&
                  v3_dot(ls_vel, ls_vel) < 1e-8f;
  g_num_events = 0;
  memset(g_match.noise, 0, sizeof g_match.noise);
  for (int ls_i = 0; ls_i < (ls_settle ? 160 : 0); ls_i++) {
    g_tick++;
    long ls_cs0 = g_lean_step_scans;
    anim_tick(&ls_ctrl, ls_pos, ls_vel, 0.0f, 0.0f, ls_grounded,
              ls_crouch, 0.0f, ls_slide, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
    if (g_lean_step_scans != ls_cs0) ls_pre_scan = 0;
    long ls_ss0 = g_lean_step_scans;
    anim_tick(&ls_step, ls_pos, ls_vel, 0.0f, 0.0f, ls_grounded,
              ls_crouch, 0.0f, ls_slide, 0.0f, ls_res.lean,
              0, 0, WPN_AR, 1);
    long ls_ds = g_lean_step_scans - ls_ss0;
    if (ls_ds > 1) ls_pre_scan = 0;
    ls_pre_scans += ls_ds;
    ls_pre_ticks++;
    if (!leanstep_gait_same(&ls_ctrl, &ls_step)) ls_pre_gait = 0;
  }
  if (leanstep_event_is_step(g_events, g_num_events) ||
      leanstep_noise_is_step(g_match.noise)) ls_pre_quiet = 0;
  if (ls_clear) {
    g_tick++;
    anim_tick(&ls_ctrl, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1,
              ls_crouch, 0.0f, 0, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
    anim_tick(&ls_step, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1,
              ls_crouch, 0.0f, 0, 0.0f, ls_res.lean, 0, 0, WPN_AR, 1);
  }

  event_t ls_ctrl_ev[MAX_EVENTS];
  bot_noise_t ls_ctrl_noise[BOT_NOISE_COUNT][MAX_ENTS];
  g_num_events = 0;
  memset(g_match.noise, 0, sizeof g_match.noise);
  long ls_ctrl_scan0 = g_lean_step_scans;
  anim_tick(&ls_ctrl, ls_pos, ls_vel, 0.0f, 0.0f, ls_grounded,
            ls_crouch, 0.0f, ls_slide, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
  long ls_ctrl_scans = g_lean_step_scans - ls_ctrl_scan0;
  int ls_ctrl_nev = g_num_events;
  memcpy(ls_ctrl_ev, g_events, sizeof ls_ctrl_ev);
  memcpy(ls_ctrl_noise, g_match.noise, sizeof ls_ctrl_noise);

  g_num_events = 0;
  memset(g_match.noise, 0, sizeof g_match.noise);
  float ls_step_sink0 = ls_step.sink;
  long ls_step_scan0 = g_lean_step_scans;
  anim_tick(&ls_step, ls_pos, ls_vel, 0.0f, 0.0f, ls_grounded,
            ls_crouch, 0.0f, ls_slide, 0.0f, ls_res.lean, 0, 0, WPN_AR, 1);
  ls_res.scans = g_lean_step_scans - ls_step_scan0;
  ls_res.scan_ok = ls_pre_scan && ls_ctrl_scans == 0 && ls_res.scans <= 1;
  ls_res.scans += ls_pre_scans;
  ls_res.ticks = ls_pre_ticks + 1;
  ls_res.event_same = ls_ctrl_nev == g_num_events &&
      !memcmp(ls_ctrl_ev, g_events, (size_t)g_num_events * sizeof *g_events) &&
      !memcmp(ls_ctrl_noise, g_match.noise, sizeof ls_ctrl_noise) &&
      !leanstep_event_is_step(g_events, g_num_events) &&
      !leanstep_noise_is_step(g_match.noise) && ls_pre_quiet;
  ls_res.gait_same = ls_pre_gait && leanstep_gait_same(&ls_ctrl, &ls_step);
  // Isolate the leg pose from the already-established torso Lean.  The comparison
  // carries the identical settled state and removes only the presentation offset/k;
  // upright-vs-Lean would charge the old torso counterweight to this feature.
  anim_t ls_same = ls_step;
  ls_same.lean_step_off = (v3){{0, 0, 0}};
  ls_same.lean_step_k = 0.0f;
  ls_same.sink = ls_step_sink0;
  skel_solve(&ls_same, ls_pos, 0.0f, 0.0f, WPN_AR, 1,
             SKEL_ADVANCE_STATE);
  ls_res.sink_mm = fabsf(ls_same.sink - ls_step.sink) * 1000.0f;
  ls_res.active = g_lean_step_side;
  ls_res.want_mm = v3_len(g_lean_step_want_off) * 1000.0f;
  ls_res.got_mm = v3_len(g_lean_step_got_off) * 1000.0f;
  ls_res.ratio = g_lean_step_ratio;
  for (int ls_i = 0; ls_i < 2; ls_i++)
    ls_res.knee[ls_i] = leanstep_knee_deg(&ls_step.pose, ls_i);
  float ls_ctrl_knee = leanstep_knee_deg(&ls_same.pose, ls_side);
  ls_res.ctrl_knee = ls_ctrl_knee;
  ls_res.flex = ls_ctrl_knee - ls_res.knee[ls_side];
  ls_res.hip_drop_mm = LEAN_STEP_HIP_DROP_M * fabsf(ls_step.lean_step_k) * 1000.0f;
  ls_res.hip = ls_step.pose.hip[ls_side];
  ls_res.ankle = ls_step.pose.ankle[ls_side];
  v3 ls_actual_add = v3_sub(ls_step.pose.ankle[ls_side],
                             ls_same.pose.ankle[ls_side]);
  ls_res.reach_mm = v3_len(v3_sub(ls_actual_add,
                                  ls_step.lean_step_off)) * 1000.0f;

  v3 ls_right = {{1, 0, 0}};
  float ls_head_lat = v3_dot(v3_sub(ls_step.pose.head, ls_ctrl.pose.head), ls_right);
  ls_res.head_gap_mm = fabsf(fabsf(ls_head_lat) -
                                  fabsf(lean_eye_off(ls_res.lean))) * 1000.0f;
  int ls_actual = ls_step.lean_step_k > 1e-4f ? 1
                : ls_step.lean_step_k < -1e-4f ? 0 : -1;
  // lean_step_k is intentionally the single owner of the active leg.  Comparing
  // ankles against an upright control is not a two-leg test: the ordinary pelvis
  // lean can make the other leg hit its existing reach clamp.  Instead prove that
  // the one signed state, its cached offset and the diagnostic all name one side;
  // the transition proof below separately requires a neutral tick before reversal.
  float ls_off_len = v3_len(ls_step.lean_step_off);
  ls_res.one_side = ls_actual == g_lean_step_side &&
                    (ls_actual < 0 ? ls_off_len < 0.0005f
                                   : ls_off_len > 0.0001f) &&
                    (ls_actual < 0 || ls_actual == ls_side);

  leanstep_render_sweep(&ls_step, ls_pos, 0.0f, &ls_res);
  return ls_res;
}

static void leanstep_print_case(const leanstep_case_t *ls_res) {
  printf("leanstep %-13s lean=%+.2f want=%.1fmm got=%.1fmm ratio=%.3f "
         "side=%c knees=%.2f/%.2fdeg flex=%.2fdeg hip=%.1fmm sink=%.2fmm "
         "reach=%.2fmm "
         "headgap=%.2fmm wall=%.2fmm scans=%ld/%ld\n",
         ls_res->name, (double)ls_res->lean, (double)ls_res->want_mm,
         (double)ls_res->got_mm, (double)ls_res->ratio,
         ls_res->active < 0 ? '-' : ls_res->active ? 'R' : 'L',
         (double)ls_res->knee[0], (double)ls_res->knee[1],
         (double)ls_res->flex, (double)ls_res->hip_drop_mm,
         (double)ls_res->sink_mm, (double)ls_res->reach_mm,
         (double)ls_res->head_gap_mm,
         (double)ls_res->wall_mm, ls_res->scans, ls_res->ticks);
  for (int ls_part = 0; ls_part < 3; ls_part++) {
    printf("leanstep   %-8s aabb/mesh(mm)", LS_PART[ls_part]);
    for (int ls_ai = 0; ls_ai < 5; ls_ai++)
      printf(" a%.2f=%.2f/%.2f", (double)LS_ALPHA[ls_ai],
             (double)ls_res->aabb_mm[ls_part][ls_ai],
             (double)ls_res->mesh_mm[ls_part][ls_ai]);
    printf("\n");
  }
}

static float leanstep_span_knee(v3 ls_hip, v3 ls_ankle) {
  float ls_span = v3_len(v3_sub(ls_ankle, ls_hip));
  float ls_max = ANIM_LEG * ANIM_REACH_K;
  if (ls_span > ls_max) ls_span = ls_max;
  float ls_c = (ANIM_THIGH * ANIM_THIGH + ANIM_SHIN * ANIM_SHIN -
                ls_span * ls_span) / (2.0f * ANIM_THIGH * ANIM_SHIN);
  return acosf(f_clamp(ls_c, -1.0f, 1.0f)) / DEG2RAD;
}

// Diagnostic only: solve the two local authoring knobs against the same ankle and
// pelvis frame the production pose produced.  This does not feed a second pose into
// the gate; it tells the integrator whether the 8-degree floor is reachable by the
// plan's 15--30 mm drop band, or how many millimetres of hip travel toward the active
// foot would be needed at the authored 20 mm drop.
static void leanstep_flex_diagnostic(const leanstep_case_t *ls_res) {
  v3 ls_base_hip = ls_res->hip;
  ls_base_hip.y += ls_res->hip_drop_mm * 0.001f;
  float ls_drop_only = -1.0f, ls_out_only = -1.0f;
  float ls_combo_drop = -1.0f, ls_combo_out = -1.0f;
  for (int ls_di = 60; ls_di <= 120; ls_di++) {
    float ls_drop = (float)ls_di * 0.00025f;
    v3 ls_hip = ls_base_hip;
    ls_hip.y -= ls_drop;
    float ls_flex = ls_res->ctrl_knee - leanstep_span_knee(ls_hip, ls_res->ankle);
    if (ls_drop_only < 0.0f && ls_flex >= 8.0f && ls_flex <= 15.0f)
      ls_drop_only = ls_drop;
    for (int ls_oi = 0; ls_oi <= 120; ls_oi++) {
      float ls_out = (float)ls_oi * 0.00025f;
      v3 ls_h2 = ls_hip;
      ls_h2.x += (ls_res->side ? 1.0f : -1.0f) * ls_out;
      float ls_f2 = ls_res->ctrl_knee - leanstep_span_knee(ls_h2, ls_res->ankle);
      if (ls_f2 >= 8.0f && ls_f2 <= 15.0f &&
          (ls_combo_out < 0.0f || ls_out < ls_combo_out)) {
        ls_combo_drop = ls_drop;
        ls_combo_out = ls_out;
      }
      if (fabsf(ls_drop - LEAN_STEP_HIP_DROP_M) < 0.00013f &&
          ls_out_only < 0.0f && ls_f2 >= 8.0f && ls_f2 <= 15.0f)
        ls_out_only = ls_out;
    }
  }
  printf("leanstep flexdiag %-13s actual=%.2fdeg drop-only=",
         ls_res->name, (double)ls_res->flex);
  if (ls_drop_only >= 0.0f) printf("%.2fmm", (double)(ls_drop_only * 1000.0f));
  else printf(">30mm");
  printf(" outward@%.0fmm=", (double)(LEAN_STEP_HIP_DROP_M * 1000.0f));
  if (ls_out_only >= 0.0f) printf("%.2fmm", (double)(ls_out_only * 1000.0f));
  else printf(">30mm");
  printf(" best-combo=");
  if (ls_combo_out >= 0.0f)
    printf("drop %.2fmm + outward %.2fmm\n",
           (double)(ls_combo_drop * 1000.0f), (double)(ls_combo_out * 1000.0f));
  else
    printf("none-in-band\n");
}

static int leanstep_case_ok(const leanstep_case_t *ls_res, int ls_expect_active) {
  int ls_ok = ls_res->gait_same && ls_res->event_same && ls_res->scan_ok &&
              ls_res->renderer_ok && ls_res->one_side &&
              ls_res->sink_mm <= LS_SINK_M * 1000.0f;
  if (ls_res->want_mm > 0.1f) {
    float ls_expect_ratio = f_clamp(ls_res->got_mm / ls_res->want_mm,
                                    0.0f, 1.0f);
    ls_ok &= fabsf(ls_res->ratio - ls_expect_ratio) <= 0.02f;
  }
  if (ls_expect_active)
    ls_ok &= ls_res->active == ls_res->side && ls_res->got_mm > 0.5f &&
             ls_res->reach_mm <= LS_PEN_M * 1000.0f &&
             ls_res->scans > 0 && ls_res->scans <= ls_res->ticks;
  else
    ls_ok &= ls_res->active < 0 && ls_res->got_mm < 0.1f &&
             ls_res->scans <= ls_res->ticks;
  for (int ls_part = 0; ls_part < 3; ls_part++)
    for (int ls_ai = 0; ls_ai < 5; ls_ai++)
      if (ls_res->aabb_mm[ls_part][ls_ai] > LS_PEN_M * 1000.0f ||
          ls_res->mesh_mm[ls_part][ls_ai] > LS_PEN_M * 1000.0f) ls_ok = 0;
  return ls_ok;
}

static float leanstep_wall_ratio(int ls_side, float ls_wall_k,
                                 float *ls_got_mm, int *ls_scan_ok,
                                 long *ls_scans) {
  v3 ls_pos = {{0, 0, 0}};
  anim_t ls_anim;
  g_num_solids = 0;
  anim_reset(&ls_anim, ls_pos, 0.0f, WPN_AR);
  float ls_home = fabsf(ls_anim.foot[ls_side].pos.x - ls_pos.x);
  float ls_inner = ls_home + LEAN_STEP_MARGIN_M + LEAN_STEP_OUT_M * ls_wall_k;
  leanstep_stage_solids(LS_SIDEWALL, ls_side, ls_inner);
  *ls_scan_ok = 1;
  *ls_scans = 0;
  for (int ls_i = 0; ls_i < 160; ls_i++) {
    long ls_scan0 = g_lean_step_scans;
    anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0,
              0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
              0, 0, WPN_AR, 1);
    long ls_tick_scans = g_lean_step_scans - ls_scan0;
    if (ls_tick_scans > 1) *ls_scan_ok = 0;
    *ls_scans += ls_tick_scans;
  }
  *ls_got_mm = v3_len(g_lean_step_got_off) * 1000.0f;
  if (*ls_scans == 0) *ls_scan_ok = 0;
  return g_lean_step_ratio;
}

static int leanstep_wall_sweep(void) {
  const float ls_k[6] = {1.25f, 1.00f, 0.75f, 0.50f, 0.25f, 0.10f};
  float ls_ratio[2][6], ls_got[2][6];
  int ls_ok = 1;
  for (int ls_side = 0; ls_side < 2; ls_side++) {
    printf("leanstep wall-%c", ls_side ? 'R' : 'L');
    for (int ls_i = 0; ls_i < 6; ls_i++) {
      int ls_scan_ok;
      long ls_scans;
      ls_ratio[ls_side][ls_i] = leanstep_wall_ratio(
          ls_side, ls_k[ls_i], &ls_got[ls_side][ls_i], &ls_scan_ok,
          &ls_scans);
      if (!ls_scan_ok || (ls_i && ls_ratio[ls_side][ls_i] >
                                    ls_ratio[ls_side][ls_i - 1] + 1e-4f)) ls_ok = 0;
      printf(" k%.2f=%.3f/%.1fmm/s%ld", (double)ls_k[ls_i],
             (double)ls_ratio[ls_side][ls_i], (double)ls_got[ls_side][ls_i],
             ls_scans);
    }
    printf("\n");
  }
  for (int ls_i = 0; ls_i < 6; ls_i++)
    if (fabsf(ls_got[0][ls_i] - ls_got[1][ls_i]) > LS_SYM_M * 1000.0f ||
        fabsf(ls_ratio[0][ls_i] - ls_ratio[1][ls_i]) > 0.02f) ls_ok = 0;
  return ls_ok;
}

// A static wall fixture does not catch a stale follower: the foot can first reach the
// open-space endpoint, then remain beyond a newly shortened request while it eases back.
// Grow the real production state in open space, introduce a wall around that endpoint,
// and request a genuine 10% Lean on the next tick. The old endpoint is inside the new
// wall while the shortened endpoint is safely inboard. A radial body->target query misses
// that wall; only the production previous-ankle->candidate sweep can escape it now.
static int leanstep_contraction(void) {
  v3 ls_pos = {{0, 0, 0}};
  int ls_ok = 1;
  for (int ls_side = 0; ls_side < 2; ls_side++) {
    anim_t ls_anim;
    g_num_solids = 0;
    anim_reset(&ls_anim, ls_pos, 0.0f, WPN_AR);
    for (int ls_i = 0; ls_i < 160; ls_i++) {
      g_tick++;
      anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0,
                0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
                0, 0, WPN_AR, 1);
    }
    float ls_open = v3_len(ls_anim.lean_step_off);
    float ls_home = fabsf(ls_anim.foot[ls_side].pos.x - ls_pos.x);
    float ls_inner = ls_home + LEAN_STEP_MARGIN_M + LEAN_STEP_OUT_M * 0.16f;
    leanstep_stage_solids(LS_SIDEWALL, ls_side, ls_inner);

    float ls_sink0 = ls_anim.sink;
    long ls_s0 = g_lean_step_scans;
    g_tick++;
    anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0,
              0.0f, 0, 0.0f, ls_side ? 0.10f : -0.10f,
              0, 0, WPN_AR, 1);
    long ls_scans = g_lean_step_scans - ls_s0;

    float ls_want = v3_len(g_lean_step_want_off);
    float ls_got = v3_len(ls_anim.lean_step_off);
    anim_t ls_same = ls_anim;
    ls_same.lean_step_off = (v3){{0, 0, 0}};
    ls_same.lean_step_k = 0.0f;
    ls_same.sink = ls_sink0;
    skel_solve(&ls_same, ls_pos, 0.0f, 0.0f, WPN_AR, 1,
               SKEL_ADVANCE_STATE);
    v3 ls_actual_add = v3_sub(ls_anim.pose.ankle[ls_side],
                               ls_same.pose.ankle[ls_side]);
    float ls_reach = v3_len(v3_sub(ls_actual_add,
                                    ls_anim.lean_step_off));

    // The fixture deliberately introduces a wall between ticks, so alpha=0 still is
    // the legitimately open previous frame. Freeze only this discontinuous-world mesh
    // check on the newly solved current pose; the shrink assertion above catches the
    // stale follower. The continuous-yaw proof below keeps the real pose history.
    anim_t ls_current = ls_anim;
    ls_current.prev_pose = ls_current.pose;
    leanstep_case_t ls_render = {0};
    leanstep_render_sweep(&ls_current, ls_pos, 0.0f, &ls_render);
    float ls_pen = 0.0f;
    for (int ls_part = 0; ls_part < 3; ls_part++)
      for (int ls_ai = 0; ls_ai < 5; ls_ai++)
        ls_pen = fmaxf(ls_pen, fmaxf(ls_render.aabb_mm[ls_part][ls_ai],
                                    ls_render.mesh_mm[ls_part][ls_ai]));
    float ls_expect_ratio = ls_want > 1e-6f
                              ? f_clamp(ls_got / ls_want, 0.0f, 1.0f) : 0.0f;
    int ls_one = ls_scans == 1 && ls_render.renderer_ok &&
                 ls_open > ls_want + 0.050f &&
                 ls_open > ls_got + 0.040f && ls_got < 0.025f &&
                 fabsf(g_lean_step_ratio - ls_expect_ratio) <= 0.02f &&
                 ls_reach <= LS_PEN_M &&
                 ls_pen <= LS_PEN_M * 1000.0f &&
                 ls_render.wall_mm >= -LS_PEN_M * 1000.0f;
    printf("leanstep contract-%c open=%.1fmm want=%.1fmm got=%.1fmm "
           "ratio=%.3f reach=%.2fmm pen=%.2fmm wall=%.2fmm scans=%ld %s\n",
           ls_side ? 'R' : 'L', (double)(ls_open * 1000.0f),
           (double)(ls_want * 1000.0f), (double)(ls_got * 1000.0f),
           (double)g_lean_step_ratio, (double)(ls_reach * 1000.0f),
           (double)ls_pen, (double)ls_render.wall_mm, ls_scans,
           ls_one ? "ok" : "FAIL");
    if (!ls_one) ls_ok = 0;
  }
  return ls_ok;
}

// Direction changes are the case a radial base->candidate ray cannot certify.  The
// old and new endpoints both clear this tiny convex wall by more than the published
// boot margin, while their chord crosses its centre.  The renderer is then exercised
// on the untouched prev_pose->pose history at all five production alphas.
static int leanstep_yaw_chord(void) {
  v3 ls_pos = {{0, 0, 0}};
  const v3 ls_vel = {{0.0f, 0.0f, 0.0f}};
  const float ls_half = 0.0015f;
  int ls_ok = 1;
  for (int ls_side = 0; ls_side < 2; ls_side++) {
    float ls_out_yaw = (ls_side ? 1.0f : -1.0f) *
                       atan2f(LEAN_STEP_FWD_M, LEAN_STEP_OUT_M);
    float ls_old_yaw = wrap_pi(ls_out_yaw + F_PI);
    float ls_new_yaw = ls_out_yaw;
    anim_t ls_anim;
    g_num_solids = 0;
    // Keep a wide but reachable plant in the zero-yaw leg frame. These two aim angles
    // make the authored old/new deltas point exactly inward/outward across the boot's
    // narrow axis, so both endpoints clear while their 168 mm chord crosses the box.
    anim_reset(&ls_anim, ls_pos, 0.0f, WPN_AR);
    float ls_sign = ls_side ? 1.0f : -1.0f;
    v3 ls_plant = {{ls_sign * 0.20f, ANIM_ANKLE_Y, 0.0f}};
    ls_anim.foot[ls_side].pos = ls_plant;
    ls_anim.foot[ls_side].from = ls_plant;
    ls_anim.foot[ls_side].to = ls_plant;
    ls_anim.spd = 0.0f;
    ls_anim.vel_s = ls_vel;
    ls_anim.yaw_prev = ls_old_yaw;
    g_tick++;
    anim_tick(&ls_anim, ls_pos, ls_vel, ls_old_yaw, 0.0f,
              1, 0, 0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
              0, 0, WPN_AR, 1);
    // Stage the exact old production candidate as the incoming endpoint. The next
    // anim_tick itself copies this pose into prev_pose; no history is frozen after the
    // measured solve.
    ls_anim.lean_step_off = g_lean_step_want_off;
    ls_anim.lean_step_k = ls_side ? 1.0f : -1.0f;
    // Re-establish the planted zero-yaw gait frame after the endpoint query. The
    // measured anim_tick is still free to advance it normally; only fixture staging is
    // pinned, never prev_pose or the rendered interpolation.
    ls_anim.leg_yaw = 0.0f;
    ls_anim.foot[ls_side].yaw = 0.0f;
    // Keep this geometry fixture comfortably inside the ordinary leg budget. Reach
    // edge/inversion behavior is covered separately below; here only the swept chord
    // is under test.
    ls_anim.sink = 0.130f;
    skel_solve(&ls_anim, ls_pos, ls_old_yaw, 0.0f, WPN_AR, 1,
               SKEL_POSE_ONLY);
    v3 ls_from = ls_anim.pose.ankle[ls_side];

    // Ask production, on a copy and in open space, for this tick's exact candidate.
    // This avoids a copied yaw/sign/reach implementation in the proof.  Restore the
    // diagnostic counter and clock before the measured tick.
    anim_t ls_open = ls_anim;
    long ls_scan_save = g_lean_step_scans;
    long ls_tick_save = g_tick;
    g_tick++;
    anim_tick(&ls_open, ls_pos, ls_vel, ls_new_yaw, 0.0f,
              1, 0, 0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
              0, 0, WPN_AR, 1);
    v3 ls_candidate = v3_add(ls_open.foot[ls_side].pos,
                              g_lean_step_want_off);
    g_lean_step_scans = ls_scan_save;
    g_tick = ls_tick_save;

    v3 ls_chord = v3_sub(ls_candidate, ls_from);
    float ls_chord2 = v3_dot(ls_chord, ls_chord);
    v3 ls_mid = v3_lerp(ls_from, ls_candidate, 0.5f);
    float ls_from_clear = fmaxf(fabsf(ls_from.x - ls_mid.x),
                                 fabsf(ls_from.z - ls_mid.z)) - ls_half;
    float ls_to_clear = fmaxf(fabsf(ls_candidate.x - ls_mid.x),
                               fabsf(ls_candidate.z - ls_mid.z)) - ls_half;
    g_num_solids = 1;
    g_solids[0] = (solid_t){
      .min = {{ls_mid.x - ls_half, -0.25f, ls_mid.z - ls_half}},
      .max = {{ls_mid.x + ls_half, 2.5f, ls_mid.z + ls_half}}, .pen = 0};

    float ls_sink0 = ls_anim.sink;
    long ls_s0 = g_lean_step_scans;
    g_tick++;
    anim_tick(&ls_anim, ls_pos, ls_vel, ls_new_yaw, 0.0f,
              1, 0, 0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
              0, 0, WPN_AR, 1);
    long ls_scans = g_lean_step_scans - ls_s0;

    anim_t ls_same = ls_anim;
    ls_same.lean_step_off = (v3){{0, 0, 0}};
    ls_same.lean_step_k = 0.0f;
    ls_same.sink = ls_sink0;
    skel_solve(&ls_same, ls_pos, ls_new_yaw, 0.0f, WPN_AR, 1,
               SKEL_ADVANCE_STATE);
    v3 ls_actual_add = v3_sub(ls_anim.pose.ankle[ls_side],
                               ls_same.pose.ankle[ls_side]);
    float ls_reach = v3_len(v3_sub(ls_actual_add,
                                    ls_anim.lean_step_off));
    v3 ls_actual_chord = v3_sub(ls_anim.pose.ankle[ls_side], ls_from);
    float ls_t = ls_chord2 > 1e-9f
                   ? v3_dot(ls_actual_chord, ls_chord) / ls_chord2 : -1.0f;
    v3 ls_on_line = v3_add(ls_from, v3_scale(ls_chord, ls_t));
    float ls_line = v3_len(v3_sub(ls_anim.pose.ankle[ls_side], ls_on_line));
    float ls_effect = v3_len(v3_sub(ls_anim.pose.ankle[ls_side],
                                     ls_open.pose.ankle[ls_side]));

    leanstep_case_t ls_render = {0};
    leanstep_render_sweep(&ls_anim, ls_pos, ls_new_yaw, &ls_render);
    float ls_pen = 0.0f;
    for (int ls_part = 0; ls_part < 3; ls_part++)
      for (int ls_ai = 0; ls_ai < 5; ls_ai++)
        ls_pen = fmaxf(ls_pen, fmaxf(ls_render.aabb_mm[ls_part][ls_ai],
                                    ls_render.mesh_mm[ls_part][ls_ai]));
    int ls_one = ls_scans == 1 && ls_render.renderer_ok &&
                 ls_chord2 > 0.020f * 0.020f &&
                 ls_from_clear >= LEAN_STEP_MARGIN_M &&
                 ls_to_clear >= LEAN_STEP_MARGIN_M &&
                 ls_t >= -0.001f && ls_t < 0.5f &&
                 ls_line <= LS_PEN_M && ls_effect > 0.0001f &&
                 ls_reach <= LS_PEN_M &&
                 ls_pen <= LS_PEN_M * 1000.0f &&
                 ls_render.wall_mm >= -LS_PEN_M * 1000.0f;
    printf("leanstep yaw-%c chord=%.1fmm ends=%.1f/%.1fmm t=%.3f "
           "line=%.2fmm effect=%.2fmm reach=%.2fmm pen=%.2fmm "
           "wall=%.2fmm scans=%ld %s\n", ls_side ? 'R' : 'L',
           (double)(sqrtf(ls_chord2) * 1000.0f),
           (double)(ls_from_clear * 1000.0f),
           (double)(ls_to_clear * 1000.0f), (double)ls_t,
           (double)(ls_line * 1000.0f), (double)(ls_effect * 1000.0f),
           (double)(ls_reach * 1000.0f), (double)ls_pen,
           (double)ls_render.wall_mm, ls_scans, ls_one ? "ok" : "FAIL");
    if (!ls_one) ls_ok = 0;
  }
  return ls_ok;
}

// Exercise the intersection of the leg reach disk and side slab. The ordinary fixture
// gates solved-delta identity; the second starts beyond the disk and
// would turn the reach correction into an inward Lean step without the signed-delta
// gate.  The valid case also proves that the solved-ankle delta equals the stored
// presentation offset; the rejected case must publish no offset and perform no scan.
static int leanstep_reach(void) {
  v3 ls_pos = {{0, 0, 0}};
  int ls_ok = 1;
  for (int ls_side = 0; ls_side < 2; ls_side++) {
    for (int ls_far = 0; ls_far < 2; ls_far++) {
      anim_t ls_anim;
      g_num_solids = 0;
      anim_reset(&ls_anim, ls_pos, 0.0f, WPN_AR);
      float ls_sign = ls_side ? 1.0f : -1.0f;
      v3 ls_authored = {{ls_sign * LEAN_STEP_OUT_M, 0.0f,
                         -LEAN_STEP_FWD_M}};
      v3 ls_dir = v3_norm(ls_authored);
      float ls_hmax = anim_hmax(&ls_anim) * 0.97f;
      float ls_radius = ls_hmax * 1.04f;
      if (ls_far) {
        v3 ls_plant = v3_add(ls_pos, v3_scale(ls_dir, ls_radius));
        ls_plant.y = ANIM_ANKLE_Y;
        ls_anim.foot[ls_side].pos = ls_plant;
        ls_anim.foot[ls_side].from = ls_plant;
        ls_anim.foot[ls_side].to = ls_plant;
        ls_anim.foot[ls_side].t = 1.0f;
        skel_solve(&ls_anim, ls_pos, 0.0f, 0.0f, WPN_AR, 1,
                   SKEL_POSE_ONLY);
        ls_anim.prev_pose = ls_anim.pose;
      }

      float ls_sink0 = ls_anim.sink;
      long ls_s0 = g_lean_step_scans;
      g_tick++;
      anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f,
                1, 0, 0.0f, 0, 0.0f, ls_side ? 1.0f : -1.0f,
                0, 0, WPN_AR, 1);
      long ls_scans = g_lean_step_scans - ls_s0;
      v3 ls_candidate = v3_add(ls_anim.foot[ls_side].pos,
                                g_lean_step_want_off);
      float ls_radial = sqrtf(ls_candidate.x * ls_candidate.x +
                               ls_candidate.z * ls_candidate.z);
      float ls_forward = v3_dot(g_lean_step_want_off, ls_dir);

      anim_t ls_same = ls_anim;
      ls_same.lean_step_off = (v3){{0, 0, 0}};
      ls_same.lean_step_k = 0.0f;
      ls_same.sink = ls_sink0;
      skel_solve(&ls_same, ls_pos, 0.0f, 0.0f, WPN_AR, 1,
                 SKEL_ADVANCE_STATE);
      v3 ls_actual_add = v3_sub(ls_anim.pose.ankle[ls_side],
                                 ls_same.pose.ankle[ls_side]);
      float ls_reach = v3_len(v3_sub(ls_actual_add,
                                      ls_anim.lean_step_off));
      int ls_one = 1;
      if (ls_far)
        ls_one &= ls_scans == 0 && g_lean_step_side < 0 &&
                  v3_len(g_lean_step_want_off) < 1e-6f &&
                  v3_len(ls_anim.lean_step_off) < 1e-6f &&
                  ls_forward >= -1e-6f;
      else
        ls_one &= ls_reach <= LS_PEN_M && ls_scans == 1 &&
                  g_lean_step_side == ls_side &&
                  ls_forward > 1e-5f && ls_radial <= ls_hmax + 1e-5f;
      printf("leanstep reach-%c-%s hmax=%.1fmm radial=%.1fmm "
             "forward=%.2fmm delta=%.2fmm scans=%ld %s\n",
             ls_side ? 'R' : 'L', ls_far ? "invert" : "normal",
             (double)(ls_hmax * 1000.0f), (double)(ls_radial * 1000.0f),
             (double)(ls_forward * 1000.0f), (double)(ls_reach * 1000.0f),
             ls_scans, ls_one ? "ok" : "FAIL");
      if (!ls_one) ls_ok = 0;
    }
  }
  return ls_ok;
}

static int leanstep_signflip(void) {
  v3 ls_pos = {{0, 0, 0}};
  anim_t ls_ctrl, ls_anim;
  g_num_solids = 0;
  g_num_events = 0;
  memset(g_match.noise, 0, sizeof g_match.noise);
  anim_reset(&ls_ctrl, ls_pos, 0.0f, WPN_AR);
  anim_reset(&ls_anim, ls_pos, 0.0f, WPN_AR);
  anim_tick(&ls_ctrl, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0, 0.0f,
            0, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
  anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0, 0.0f,
            0, 0.0f, 1.0f, 0, 0, WPN_AR, 1);
  int ls_saw_r = ls_anim.lean_step_k > 0.0f;
  int ls_saw_zero = 0, ls_saw_l = 0, ls_dual = 0, ls_gait = 1, ls_scan = 1;
  long ls_scans = 0;
  float ls_source_lean = 1.0f;
  int ls_prev_side = 1;
  float ls_prev_off = v3_len(ls_anim.lean_step_off);
  for (int ls_i = 0; ls_i < 260; ls_i++) {
    float ls_rate = fabsf(-1.0f) > fabsf(ls_source_lean)
                      ? LEAN_IN_RATE : LEAN_OUT_RATE;
    ls_source_lean += (-1.0f - ls_source_lean) * ease_k(ls_rate);
    g_tick++;
    anim_tick(&ls_ctrl, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0, 0.0f,
              0, 0.0f, 0.0f, 0, 0, WPN_AR, 1);
    long ls_s0 = g_lean_step_scans;
    anim_tick(&ls_anim, ls_pos, (v3){{0, 0, 0}}, 0.0f, 0.0f, 1, 0, 0.0f,
              0, 0.0f, ls_source_lean, 0, 0, WPN_AR, 1);
    long ls_ds = g_lean_step_scans - ls_s0;
    ls_scans += ls_ds;
    if (ls_ds > 1) ls_scan = 0;
    if (!leanstep_gait_same(&ls_ctrl, &ls_anim)) ls_gait = 0;
    int ls_state_side = ls_anim.lean_step_k > 1e-4f ? 1
                      : ls_anim.lean_step_k < -1e-4f ? 0 : -1;
    float ls_off = v3_len(ls_anim.lean_step_off);
    if (ls_state_side != g_lean_step_side ||
        (ls_state_side < 0 && v3_len(ls_anim.lean_step_off) >= 0.0005f) ||
        (ls_prev_side >= 0 && ls_state_side >= 0 &&
         ls_prev_side != ls_state_side &&
         fminf(ls_prev_off, ls_off) >= LS_SYM_M)) ls_dual = 1;
    if (ls_off < LS_SYM_M) ls_saw_zero = 1;
    if (ls_state_side == 0) ls_saw_l = 1;
    ls_prev_side = ls_state_side;
    ls_prev_off = ls_off;
  }
  int ls_quiet = !leanstep_event_is_step(g_events, g_num_events) &&
                 !leanstep_noise_is_step(g_match.noise);
  printf("leanstep signflip R=%d zero=%d L=%d dual=%d gait=%d quiet=%d "
         "scans=%ld final=%+.3f\n", ls_saw_r, ls_saw_zero, ls_saw_l,
         ls_dual, ls_gait, ls_quiet, ls_scans, (double)ls_anim.lean_step_k);
  return ls_saw_r && ls_saw_zero && ls_saw_l && !ls_dual && ls_gait &&
         ls_quiet && ls_scan;
}

static int leanstep_origin_wire(void) {
  int ls_h = 0, ls_ent = ent_of_human(ls_h);
  player_t *ls_pl = &g_players[ls_h];
  player_spawn(ls_pl);
  ls_pl->pos = ls_pl->prev_pos = (v3){{0, 0, 0}};
  ls_pl->vel = (v3){{0.12f, 0.0f, -0.08f}};
  ls_pl->yaw = 0.37f;
  ls_pl->lean = 1.0f;
  anim_reset(&g_player_anim[ls_h], ls_pl->pos, ls_pl->yaw, ls_pl->wp.cur);
  unsigned char ls_before[64], ls_after[64];
  int ls_nb = leanstep_wire(ls_ent, ls_before, sizeof ls_before);
  v3 ls_pos0 = ls_pl->pos, ls_vel0 = ls_pl->vel;
  v3 ls_shot0 = player_eye_pos(ls_pl);
  v3 ls_ray_o = {{0, EYE_STAND, 2.0f}}, ls_ray_d = {{0, 0, -1}};
  int ls_part0 = 0, ls_part1 = 0;
  float ls_hit0 = ent_trace_box(ls_pl->pos, ls_pl->eye, ls_pl->yaw,
                                ls_pl->lean, ls_ray_o, ls_ray_d, &ls_part0);
  anim_tick(&g_player_anim[ls_h], ls_pl->pos, ls_pl->vel, ls_pl->yaw,
            ls_pl->pitch, ls_pl->grounded, ls_pl->crouched, ls_pl->tuck_y,
            ls_pl->sliding, ls_pl->wp.ads_s, ls_pl->lean,
            ls_pl->wp.ready_want, ls_ent, ls_pl->wp.cur, 1);
  int ls_na = leanstep_wire(ls_ent, ls_after, sizeof ls_after);
  v3 ls_shot1 = player_eye_pos(ls_pl);
  float ls_hit1 = ent_trace_box(ls_pl->pos, ls_pl->eye, ls_pl->yaw,
                                ls_pl->lean, ls_ray_o, ls_ray_d, &ls_part1);
  int ls_ok = !memcmp(&ls_pos0, &ls_pl->pos, sizeof ls_pos0) &&
              !memcmp(&ls_vel0, &ls_pl->vel, sizeof ls_vel0) &&
              !memcmp(&ls_shot0, &ls_shot1, sizeof ls_shot0) &&
              !memcmp(&ls_hit0, &ls_hit1, sizeof ls_hit0) &&
              ls_part0 == ls_part1 && ls_nb > 0 && ls_na == ls_nb &&
              !memcmp(ls_before, ls_after, (size_t)ls_nb);
  printf("leanstep neutral origin=%d velocity=%d shot=%d hitbox=%d wire=%d "
         "bytes=%d\n",
         !memcmp(&ls_pos0, &ls_pl->pos, sizeof ls_pos0),
         !memcmp(&ls_vel0, &ls_pl->vel, sizeof ls_vel0),
         !memcmp(&ls_shot0, &ls_shot1, sizeof ls_shot0),
         !memcmp(&ls_hit0, &ls_hit1, sizeof ls_hit0) && ls_part0 == ls_part1,
         ls_nb > 0 && ls_na == ls_nb && !memcmp(ls_before, ls_after, (size_t)ls_nb),
         ls_nb);
  return ls_ok;
}

typedef struct {
  bot_t bot;
  event_t events[MAX_EVENTS];
  bot_noise_t noise[BOT_NOISE_COUNT][MAX_ENTS];
  unsigned char wire[64];
  int nevents, nwire, scan_ok;
} leanstep_puppet_t;

static void leanstep_puppet_run(float ls_target, leanstep_puppet_t *ls_out) {
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  memset(g_match.noise, 0, sizeof g_match.noise);
  g_num_solids = 0;
  g_num_events = 0;
  g_match.nbots = 1;
  g_tick = 700;
  g_rng_bot = 0x9e3779b97f4a7c15ull;
  bot_t *ls_bot = &g_bots[0];
  *ls_bot = (bot_t){.pos = {{0, 0, 0}}, .prev_pos = {{0, 0, 0}},
                    .hp = PLAYER_HP, .alive = 1, .active = 1, .grounded = 1,
                    .eye = EYE_STAND, .puppet = 1, .pup_lean_t = ls_target,
                    .pup_speed = 0.0f, .wp.cur = WPN_AR};
  wpn_fill_mags(&ls_bot->wp);
  anim_reset(&ls_bot->anim, ls_bot->pos, ls_bot->yaw, ls_bot->wp.cur);
  ls_out->scan_ok = 1;
  for (int ls_i = 0; ls_i < 80; ls_i++) {
    g_tick++;
    long ls_s0 = g_lean_step_scans;
    bot_puppet_tick(ls_bot, 0);
    long ls_ds = g_lean_step_scans - ls_s0;
    if (ls_ds > 1 || (ls_target == 0.0f && ls_ds != 0)) ls_out->scan_ok = 0;
  }
  ls_out->bot = *ls_bot;
  ls_out->nevents = g_num_events;
  memcpy(ls_out->events, g_events, sizeof ls_out->events);
  memcpy(ls_out->noise, g_match.noise, sizeof ls_out->noise);
  ls_out->nwire = leanstep_wire(1, ls_out->wire, sizeof ls_out->wire);
}

static int leanstep_puppet(void) {
  leanstep_puppet_t ls_ctrl, ls_step;
  leanstep_puppet_run(0.0f, &ls_ctrl);
  leanstep_puppet_run(1.0f, &ls_step);
  int ls_origin = !memcmp(&ls_ctrl.bot.pos, &ls_step.bot.pos,
                          sizeof ls_ctrl.bot.pos);
  int ls_velocity = !memcmp(&ls_ctrl.bot.vel, &ls_step.bot.vel,
                            sizeof ls_ctrl.bot.vel);
  int ls_gait = leanstep_gait_same(&ls_ctrl.bot.anim, &ls_step.bot.anim);
  int ls_events = ls_ctrl.nevents == ls_step.nevents &&
      !memcmp(ls_ctrl.events, ls_step.events,
              (size_t)ls_step.nevents * sizeof *ls_step.events) &&
      !memcmp(ls_ctrl.noise, ls_step.noise, sizeof ls_step.noise) &&
      !leanstep_event_is_step(ls_step.events, ls_step.nevents) &&
      !leanstep_noise_is_step(ls_step.noise);
  int ls_wire = ls_ctrl.nwire > 0 && ls_step.nwire == ls_ctrl.nwire &&
                !memcmp(ls_ctrl.wire, ls_step.wire, (size_t)ls_ctrl.nwire);
  int ls_pose = ls_step.bot.pup_lean > 0.95f &&
                ls_step.bot.anim.lean_step_k > 0.5f;
  int ls_ok = ls_origin && ls_velocity && ls_gait && ls_events && ls_wire &&
              ls_pose && ls_ctrl.scan_ok && ls_step.scan_ok;
  printf("leanstep puppet lean=%.3f step=%+.3f origin=%d velocity=%d gait=%d "
         "events=%d wire=%d scan=%d\n", (double)ls_step.bot.pup_lean,
         (double)ls_step.bot.anim.lean_step_k, ls_origin, ls_velocity, ls_gait,
         ls_events, ls_wire, ls_ctrl.scan_ok && ls_step.scan_ok);
  return ls_ok;
}

static int leanstep_snapshot(void) {
  const int ls_src_h = 1, ls_remote_h = 2;
  const int ls_src_ent = ent_of_human(ls_src_h);
  const int ls_remote_ent = ent_of_human(ls_remote_h);
  memset(&g_match, 0, sizeof g_match);
  memset(g_humans_on, 0, sizeof g_humans_on);
  memset(g_match.noise, 0, sizeof g_match.noise);
  memset(g_cl_alive_prev, 0, sizeof g_cl_alive_prev);
  memset(g_cl_body, 0, sizeof g_cl_body);
  g_num_solids = 0;
  g_num_events = 0;
  g_online = 1;
  g_local_ent = 0;
  g_cl.ent_ss_seen[ls_remote_ent] = 0;
  player_t *ls_src = &g_players[ls_src_h];
  player_spawn(ls_src);
  ls_src->pos = ls_src->prev_pos = (v3){{0, 0, 0}};
  ls_src->wp.cur = WPN_AR;
  ls_src->yaw = 0.0f;

  anim_t ls_full;
  weapon_t ls_full_wp = ls_src->wp;
  anim_reset(&ls_full, ls_src->pos, ls_src->yaw, ls_full_wp.cur);
  float ls_source_lean = 0.0f, ls_max_ankle = 0.0f, ls_max_total = 0.0f;
  int ls_decoded = 1, ls_side_same = 1, ls_gait = 1, ls_scan = 1;
  int ls_wire_neutral = 1, ls_saw_r = 0, ls_saw_l = 0;
  for (int ls_i = 0; ls_i < 540; ls_i++) {
    float ls_target = ls_i < 120 ? 1.0f : ls_i < 360 ? -1.0f : 0.0f;
    float ls_rate = fabsf(ls_target) > fabsf(ls_source_lean)
                      ? LEAN_IN_RATE : LEAN_OUT_RATE;
    ls_source_lean += (ls_target - ls_source_lean) * ease_k(ls_rate);
    ls_src->lean = ls_source_lean;
    net_ent_t ls_ne;
    net_ent_from_live(&ls_ne, ls_src_ent);
    ls_ne.id = (uint8_t)ls_remote_ent;
    unsigned char ls_packet[64];
    np_wr ls_wr = {ls_packet, sizeof ls_packet, 0, 0};
    net_ent_encode(&ls_wr, &ls_ne);
    np_rd ls_rd = {ls_packet, ls_wr.at, 0, 0};
    net_ent_t ls_out;
    int ls_one_decoded = !ls_wr.bad && net_ent_decode(&ls_rd, &ls_out) &&
                         !ls_rd.bad && ls_out.id == ls_remote_ent;
    if (!ls_one_decoded) { ls_decoded = 0; break; }
    float ls_full_lean = net_dq_wish(ls_out.lean);
    if ((ls_i & 1) == 0) net_cl_apply_ent(&ls_out);
    player_t *ls_remote = &g_players[ls_remote_h];
    unsigned char ls_before[64], ls_after[64];
    int ls_nb = leanstep_wire(ls_remote_ent, ls_before, sizeof ls_before);
    g_tick++;
    long ls_s0 = g_lean_step_scans;
    client_anim_all();
    if (g_lean_step_scans - ls_s0 > 1) ls_scan = 0;
    long ls_s1 = g_lean_step_scans;
    int ls_idle_ok = gun_pose_state(&ls_full_wp, &ls_full,
                                      (v3){{0, 0, 0}}, 1, 0);
    anim_tick(&ls_full, (v3){{0, 0, 0}}, (v3){{0, 0, 0}}, 0.0f, 0.0f,
              1, 0, 0.0f, 0, 0.0f, ls_full_lean, 0, ls_remote_ent,
              WPN_AR, ls_idle_ok);
    if (g_lean_step_scans - ls_s1 > 1) ls_scan = 0;
    int ls_na = leanstep_wire(ls_remote_ent, ls_after, sizeof ls_after);
    if (ls_nb <= 0 || ls_na != ls_nb ||
        memcmp(ls_before, ls_after, (size_t)ls_nb)) ls_wire_neutral = 0;
    if (!leanstep_gait_same(&ls_full, &g_player_anim[ls_remote_h])) ls_gait = 0;
    // Diagnose the presentation-step contribution separately, but keep the plan's
    // 5 mm contract on the complete solved ankle below. Both controls use the actual
    // post-wire states and remove only lean_step_off/k before the same production
    // solver runs again.
    anim_t ls_full_zero = ls_full;
    anim_t ls_remote_zero = g_player_anim[ls_remote_h];
    ls_full_zero.lean_step_off = ls_remote_zero.lean_step_off =
        (v3){{0, 0, 0}};
    ls_full_zero.lean_step_k = ls_remote_zero.lean_step_k = 0.0f;
    skel_solve(&ls_full_zero, (v3){{0, 0, 0}}, 0.0f, 0.0f,
               WPN_AR, 1, SKEL_POSE_ONLY);
    skel_solve(&ls_remote_zero, ls_remote->pos, ls_remote->yaw,
               ls_remote->pitch, ls_remote->wp.cur, 1, SKEL_POSE_ONLY);
    float ls_tick_max = 0.0f;
    for (int ls_side = 0; ls_side < 2; ls_side++) {
      float ls_total = v3_len(v3_sub(
          ls_full.pose.ankle[ls_side],
          g_player_anim[ls_remote_h].pose.ankle[ls_side]));
      if (ls_total > ls_max_total) ls_max_total = ls_total;
      v3 ls_full_add = v3_sub(ls_full.pose.ankle[ls_side],
                              ls_full_zero.pose.ankle[ls_side]);
      v3 ls_remote_add = v3_sub(g_player_anim[ls_remote_h].pose.ankle[ls_side],
                                ls_remote_zero.pose.ankle[ls_side]);
      float ls_d = v3_len(v3_sub(ls_full_add, ls_remote_add));
      if (ls_d > ls_tick_max) ls_tick_max = ls_d;
    }
    if (ls_tick_max > ls_max_ankle) ls_max_ankle = ls_tick_max;
    int ls_fs = ls_full.lean_step_k > 1e-4f ? 1
              : ls_full.lean_step_k < -1e-4f ? 0 : -1;
    int ls_ds = g_player_anim[ls_remote_h].lean_step_k > 1e-4f ? 1
              : g_player_anim[ls_remote_h].lean_step_k < -1e-4f ? 0 : -1;
    if (ls_fs != ls_ds && fminf(v3_len(ls_full.lean_step_off),
                                v3_len(g_player_anim[ls_remote_h].lean_step_off)) >=
                           LS_SYM_M) ls_side_same = 0;
    if (ls_ds == 1) ls_saw_r = 1;
    if (ls_ds == 0) ls_saw_l = 1;
  }
  int ls_quiet = !leanstep_event_is_step(g_events, g_num_events) &&
                 !leanstep_noise_is_step(g_match.noise);
  int ls_ok = ls_decoded && ls_side_same && ls_gait && ls_scan &&
              ls_wire_neutral && ls_saw_r && ls_saw_l && ls_quiet &&
              ls_max_ankle <= LS_SYM_M && ls_max_total <= LS_SYM_M;
  printf("leanstep snapshot stride=2 ankle_add=%.2fmm ankle_total=%.2fmm "
         "side=%d gait=%d decode=%d "
         "wire=%d events=%d scan=%d ramp=%d/%d\n", (double)(ls_max_ankle * 1000.0f),
         (double)(ls_max_total * 1000.0f), ls_side_same, ls_gait, ls_decoded,
         ls_wire_neutral, ls_quiet, ls_scan, ls_saw_r, ls_saw_l);
  return ls_ok;
}

static void leanstep_proof(void) {
  proof_world_t ls_world;
  proof_world_save(&ls_world);
  anim_t ls_player_anim_save[MAX_HUMANS];
  memcpy(ls_player_anim_save, g_player_anim, sizeof ls_player_anim_save);
  net_client_t ls_cl_save = g_cl;
  unsigned char ls_body_save[MAX_ENTS], ls_alive_save[MAX_ENTS];
  memcpy(ls_body_save, g_cl_body, sizeof ls_body_save);
  memcpy(ls_alive_save, g_cl_alive_prev, sizeof ls_alive_save);
  int ls_online_save = g_online, ls_local_save = g_local_ent;
  const float *ls_sim_mv_save = g_sim_mv;
  const float (*ls_sim_wp_save)[WP_COUNT] = g_sim_wp;
  v3 ls_cam_save = g_cam_eye;
  int ls_cull_save = g_cam_cull, ls_kcap_save = g_fig_kcap;
  int ls_fp_save = g_fig_fp, ls_probe_save = g_probe_on;
  float ls_diss_save = g_fig_diss;
  long ls_scan_save = g_lean_step_scans;
  v3 ls_want_save = g_lean_step_want_off, ls_got_save = g_lean_step_got_off;
  float ls_ratio_save = g_lean_step_ratio;
  int ls_side_save = g_lean_step_side;

  g_sim_mv = g_cfg.mv;
  g_sim_wp = g_cfg.wp;
  g_predicting = 0;
  g_cam_eye = (v3){{0, 0.9f, 0}};
  g_cam_cull = 0;
  g_fig_kcap = 0;
  g_fig_fp = 0;
  g_fig_diss = 0.0f;
  // All direct cases are authoritative/offline. The snapshot case below opts into
  // online mode explicitly and the caller's mode/local identity are restored below.
  g_online = 0;
  g_local_ent = 0;
  g_lean_step_scans = 0;

  leanstep_case_t ls_case[22];
  int ls_n = 0;
  ls_case[ls_n++] = leanstep_case("open-L-half", LS_OPEN, 0, 0.5f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("open-R-half", LS_OPEN, 1, 0.5f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("open-L-full", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("open-R-full", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("wall-L", LS_SIDEWALL, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("wall-R", LS_SIDEWALL, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("corner-L", LS_CORNER, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("corner-R", LS_CORNER, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("slot-L", LS_SLOT, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("slot-R", LS_SLOT, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 0.45f);
  ls_case[ls_n++] = leanstep_case("crouch-L", LS_OPEN, 0, 1.0f, 1,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("crouch-R", LS_OPEN, 1, 1.0f, 1,
                                  (v3){{0, 0, 0}}, 1, 0, 0, 1.25f);
  float ls_run = g_sim_mv[MV_RUN];
  float ls_slow = ls_run * LEAN_SPD_LO * 0.75f;
  float ls_mid = ls_run * (LEAN_SPD_LO + LEAN_SPD_HI) * 0.5f;
  float ls_fast = ls_run * LEAN_SPD_HI;
  ls_case[ls_n++] = leanstep_case("slow-L", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{ls_slow, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("slow-R", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{ls_slow, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("fade-L", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{ls_mid, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("fade-R", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{ls_mid, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("fast-L", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{ls_fast, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("fast-R", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{ls_fast, 0, 0}}, 1, 0, 0, 1.25f);
  ls_case[ls_n++] = leanstep_case("air-L", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 0, 0, 1, 1.25f);
  ls_case[ls_n++] = leanstep_case("air-R", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 0, 0, 1, 1.25f);
  ls_case[ls_n++] = leanstep_case("slide-L", LS_OPEN, 0, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 1, 1, 1.25f);
  ls_case[ls_n++] = leanstep_case("slide-R", LS_OPEN, 1, 1.0f, 0,
                                  (v3){{0, 0, 0}}, 1, 1, 1, 1.25f);

  int ls_ok = ls_n == (int)(sizeof ls_case / sizeof ls_case[0]);
  for (int ls_i = 0; ls_i < ls_n; ls_i++) {
    leanstep_print_case(&ls_case[ls_i]);
    int ls_expect_active = ls_i < 16 || (ls_i >= 12 && ls_i < 16);
    if (ls_i >= 16) ls_expect_active = 0;
    if (!leanstep_case_ok(&ls_case[ls_i], ls_expect_active)) ls_ok = 0;
  }
  leanstep_flex_diagnostic(&ls_case[2]);
  leanstep_flex_diagnostic(&ls_case[3]);
  leanstep_flex_diagnostic(&ls_case[10]);
  leanstep_flex_diagnostic(&ls_case[11]);

  // Open-space mirror: ankle travel and the two mapped knee angles are the actual
  // solved quantities, not an assumption about which key means which leg.
  int ls_sym = 1;
  const int ls_pair[2][2] = {{0, 1}, {2, 3}};
  for (int ls_i = 0; ls_i < 2; ls_i++) {
    const leanstep_case_t *ls_l = &ls_case[ls_pair[ls_i][0]];
    const leanstep_case_t *ls_r = &ls_case[ls_pair[ls_i][1]];
    if (fabsf(ls_l->got_mm - ls_r->got_mm) > LS_SYM_M * 1000.0f ||
        fabsf(ls_l->knee[0] - ls_r->knee[1]) > LS_SYM_DEG ||
        fabsf(ls_l->knee[1] - ls_r->knee[0]) > LS_SYM_DEG ||
        fabsf(ls_l->hip_drop_mm - ls_r->hip_drop_mm) > LS_SYM_M * 1000.0f)
      ls_sym = 0;
  }
  float ls_full_want = sqrtf(LEAN_STEP_OUT_M * LEAN_STEP_OUT_M +
                                  LEAN_STEP_FWD_M * LEAN_STEP_FWD_M) * 1000.0f;
  int ls_shape = fabsf(ls_case[2].want_mm - ls_full_want) <= 5.0f &&
                 fabsf(ls_case[3].want_mm - ls_full_want) <= 5.0f &&
                 ls_case[0].got_mm < ls_case[2].got_mm &&
                 ls_case[1].got_mm < ls_case[3].got_mm &&
                 ls_case[2].flex >= 8.0f && ls_case[2].flex <= 15.0f &&
                 ls_case[3].flex >= 8.0f && ls_case[3].flex <= 15.0f &&
                 ls_case[2].head_gap_mm <= LS_HEAD_GAP_M * 1000.0f &&
                 ls_case[3].head_gap_mm <= LS_HEAD_GAP_M * 1000.0f;
  int ls_wall = ls_case[4].ratio <= ls_case[2].ratio + 1e-4f &&
                ls_case[5].ratio <= ls_case[3].ratio + 1e-4f &&
                ls_case[6].ratio <= ls_case[2].ratio + 1e-4f &&
                ls_case[7].ratio <= ls_case[3].ratio + 1e-4f &&
                ls_case[8].ratio <= ls_case[2].ratio + 1e-4f &&
                ls_case[9].ratio <= ls_case[3].ratio + 1e-4f;
  // The stationary crouch is now an asymmetric firing-side kneel. Lean still gets a
  // shorter visible step and the authored local weight drop, but its contact contract
  // is the firing knee (index 1) staying folded under either lean direction while the
  // support knee remains up. Standing retains the hard 8--15 degree target.
  int ls_crouch = ls_case[10].got_mm < ls_case[2].got_mm &&
                  ls_case[11].got_mm < ls_case[3].got_mm &&
                  ls_case[10].got_mm > ls_case[2].got_mm * 0.5f &&
                  ls_case[11].got_mm > ls_case[3].got_mm * 0.5f &&
                  ls_case[10].flex >= 0.0f && ls_case[10].flex <= 5.0f &&
                  ls_case[11].flex >= 0.0f && ls_case[11].flex <= 5.0f &&
                  ls_case[10].knee[1] <= 60.0f && ls_case[11].knee[1] <= 60.0f &&
                  ls_case[10].knee[0] >= 60.0f && ls_case[11].knee[0] >= 60.0f;
  int ls_speed = ls_case[12].got_mm > ls_case[14].got_mm &&
                 ls_case[13].got_mm > ls_case[15].got_mm &&
                 ls_case[14].got_mm > ls_case[16].got_mm &&
                 ls_case[15].got_mm > ls_case[17].got_mm &&
                 ls_case[16].got_mm < 0.1f && ls_case[17].got_mm < 0.1f;
  int ls_clear = ls_case[18].got_mm < 0.1f && ls_case[19].got_mm < 0.1f &&
                 ls_case[20].got_mm < 0.1f && ls_case[21].got_mm < 0.1f;
  int ls_monotone = leanstep_wall_sweep();
  int ls_contract = leanstep_contraction();
  int ls_yaw = leanstep_yaw_chord();
  int ls_reach = leanstep_reach();
  int ls_flip = leanstep_signflip();
  int ls_neutral = leanstep_origin_wire();
  int ls_puppet = leanstep_puppet();
  int ls_snapshot = leanstep_snapshot();
  ls_ok &= ls_sym && ls_shape && ls_wall && ls_crouch && ls_speed &&
           ls_clear && ls_monotone && ls_contract && ls_yaw && ls_reach &&
           ls_flip && ls_neutral && ls_puppet && ls_snapshot;

  proof_world_restore(&ls_world);
  memcpy(g_player_anim, ls_player_anim_save, sizeof ls_player_anim_save);
  g_cl = ls_cl_save;
  memcpy(g_cl_body, ls_body_save, sizeof ls_body_save);
  memcpy(g_cl_alive_prev, ls_alive_save, sizeof ls_alive_save);
  g_online = ls_online_save;
  g_local_ent = ls_local_save;
  g_sim_mv = ls_sim_mv_save;
  g_sim_wp = ls_sim_wp_save;
  g_cam_eye = ls_cam_save;
  g_cam_cull = ls_cull_save;
  g_fig_kcap = ls_kcap_save;
  g_fig_fp = ls_fp_save;
  g_fig_diss = ls_diss_save;
  g_probe_on = ls_probe_save;
  g_nprobe = 0;
  scene_reset();
  g_lean_step_scans = ls_scan_save;
  g_lean_step_want_off = ls_want_save;
  g_lean_step_got_off = ls_got_save;
  g_lean_step_ratio = ls_ratio_save;
  g_lean_step_side = ls_side_save;

  printf("leanstep symmetry=%d shape=%d wall=%d crouch=%d speed=%d clear=%d "
         "monotone=%d contract=%d yaw=%d reach=%d flip=%d neutral=%d "
         "puppet=%d snapshot=%d %s\n",
         ls_sym, ls_shape, ls_wall, ls_crouch, ls_speed, ls_clear,
         ls_monotone, ls_contract, ls_yaw, ls_reach, ls_flip, ls_neutral,
         ls_puppet, ls_snapshot, ls_ok ? "ok" : "FAIL");
  if (!ls_ok) exit(1);
}

#undef LS_SYM_M
#undef LS_SYM_DEG
#undef LS_PEN_M
#undef LS_SINK_M
#undef LS_HEAD_GAP_M
