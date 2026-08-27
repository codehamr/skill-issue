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
    anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw);                \
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
  anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw);
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
  anim_reset(&g_bots[0].anim, g_bots[0].pos, g_bots[0].yaw);
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

