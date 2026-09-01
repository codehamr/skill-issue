// =============================================================================
// sim/weapon.inc  —  trigger/swap/reload path, wpn_def_t.
// Proof: wpn, parity, recoil, netrecoil, vmframe. One TU: included once by game.c,
// in definition order —
// moving a definition out is a semantic change.
// Contract: wpn_def_t carries no pen column — penetration is ONE number
// (PEN_PATH) and no per-weapon curve.
// =============================================================================
// ---------------------------------------------------------------------------
// Weapon — the trigger, swap and reload path for BOTH weapons: fire mode is
// structural (WPN_DEF.full_auto) and per-weapon feel is g_cfg.wp. Recoil, look lag,
// breathing and stride are physical pitch/yaw state of the barrel. Camera, visible gun
// and shot direction all consume that state; there is no random accuracy cone.
// ---------------------------------------------------------------------------

// THE keystone call: real entity id, per-human RNG stream, per-human
// anim — while any of the three is a literal/singleton, every g_local_ent reader is
// CONSISTENTLY wrong and one human looks correct.
static void weapon_fire(player_t *p, int ent) {
  int h = human_of_ent(ent);
  v3 o = player_eye_pos(p);
  gun_shoot(ent, &p->wp, o, p->yaw, p->pitch, p->vel, p->grounded, &g_rng_gun[h]);
}

static void weapon_tick(player_t *p, int ent, const int held[IN_COUNT]) {
  weapon_t *w = &p->wp;
  const wpn_def_t *def = &WPN_DEF[w->cur];
  w->prev_rec_p = w->rec_p;
  w->prev_rec_y = w->rec_y;
  w->prev_punch = w->punch;
  w->prev_punch_roll = w->punch_roll;
  w->prev_ads_s = w->ads_s;
  w->prev_switch = (float)w->switch_t / GUN_SWITCH_TICKS;

  gun_cool(w);

  // Swap: instant identity change, then the raise animation gates everything. A running
  // reload is abandoned (the mag stays as it was).
  int want = -1;
  if (held[IN_SWAP] && !p->prev_held[IN_SWAP]) want = (w->cur + 1) % WPN_COUNT;
  if (held[IN_SLOT1] && !p->prev_held[IN_SLOT1]) want = WPN_AR;
  if (held[IN_SLOT2] && !p->prev_held[IN_SLOT2]) want = WPN_SR;
  if (want >= 0 && want != w->cur && w->switch_t == 0) {
    // The abandoned reload's SOUND stops with it — otherwise the buffer plays to its
    // end and announces A LIVE WEAPON (the bolt cue) for a magazine that never went in.
    if (w->reload_t > 0)
      // .src: a designated-initializer push with the field left out is
      // SILENTLY entity 0 (zero-init) — every identity field is explicit now.
      ev_push((event_t){.type = EV_RELOAD_CANCEL, .src = ent, .a = p->pos,
                        .wpn = w->cur});
    w->cur = want;
    def = &WPN_DEF[w->cur];
    w->switch_t = GUN_SWITCH_TICKS;
    w->reload_t = 0;
    w->dry_done = 0;         // a fresh weapon gets its own one click
    // cool[] is deliberately NOT touched: the outgoing weapon's cycle keeps
    // running (a quick-switch must not reset the SR's bolt) and the incoming
    // weapon's own cooldown still applies from before it was holstered.
    w->idx = 0;
  }
  int busy = w->reload_t > 0 || w->switch_t > 0;

  // Step the physical hold before any bore consumer. The wall test, ADS denial and a
  // same-tick shot must all see the weapon in the pose the hands reached this tick.
  gun_sway_tick(w, p->vel, p->grounded, p->yaw, p->pitch);

  // A walled rifle cannot be shouldered onto the eye line: ADS intent is denied by
  // the same predicate that lowers the carriage and refuses the trigger, so all
  // three tell one story (and NEF_ADS carries the denial to every viewer for free).
  int walled = gun_wall_blocked(player_eye_pos(p), p->yaw, p->pitch, w);
  gun_ads(w, held[IN_ADS] && !busy && !walled);

  gun_reload_tick(w);
  if (held[IN_RELOAD] && !p->prev_held[IN_RELOAD] && !busy &&
      w->ammo[w->cur] < def->mag) {
    w->reload_tac = w->ammo[w->cur] > 0;
    w->reload_t = wpn_reload_ticks(w->cur, w->reload_tac);
    w->dry_done = 0;         // ...and so does a fresh magazine
    // .src AND .a, mirroring the bot push: with .src set and .a zero the
    // audio router takes the audio_place branch and pans a remote reload to
    // the ARENA ORIGIN (screen centre) — no proof watches reload panning.
    ev_push((event_t){.type = EV_RELOAD, .src = ent, .a = p->pos,
                      .wpn = w->cur, .k = w->reload_tac});
  }

  // Full-auto weapons fire while held; semi/bolt only on the press edge.
  // No shots from the dead or after the match ended.
  int trigger = held[IN_FIRE] && p->alive && !g_match.over &&
                (def->full_auto || !p->prev_held[IN_FIRE]);
  // THE DRY CLICK IS A LATCH, NOT AN EDGE. `trigger` is already false on every
  // held tick of a semi/bolt weapon, so the latch alone gives exactly one click
  // per press there and exactly one for a full-auto weapon that runs dry under
  // a held trigger — the case the old press-edge test made unreachable, and the
  // commonest way anybody empties a magazine.
  if (!held[IN_FIRE]) w->dry_done = 0;
  // `!walled` here spares the per-attempt refusal inside gun_shoot the dry-click
  // bookkeeping; gun_shoot's own gate stays the authoritative chokepoint (bots and
  // every other caller reach it without passing through this trigger).
  if (trigger && !walled &&
      w->cool[w->cur] == 0 && w->reload_t == 0 && w->switch_t == 0) {
    if (w->ammo[w->cur] > 0) weapon_fire(p, ent);
    else if (!w->dry_done) {
      ev_push((event_t){.type = EV_DRYFIRE, .src = ent, .a = p->pos});
      w->dry_done = 1;
    }
  }

  gun_recover(w, held[IN_FIRE]);
}

// The viewmodel's life comes from spring groups: look sway, move inertia, walk bob,
// three fire channels (back/lift/side), slide/strafe roll, and the landing dip shared
// with the camera.
static void vm_tick(player_t *p) {
  vm_t *v = &p->vm;
  float sc = g_cfg.wp[p->wp.cur][WP_SWAY];
  v->prev_sway_p = v->sway_p; v->prev_sway_y = v->sway_y;
  v->prev_lag_x = v->lag_x; v->prev_lag_y = v->lag_y; v->prev_lag_z = v->lag_z;
  v->prev_bob_x = v->bob_x; v->prev_bob_y = v->bob_y;
  v->prev_kick_back = v->kick_back;
  v->prev_kick_lift = v->kick_lift;
  v->prev_kick_side = v->kick_side;
  v->prev_roll = v->roll;
  v->prev_lean_lag = v->lean_lag;
  v->prev_slide_s = v->slide_s;
  v->prev_gunwall = v->gunwall;

  // Local pose follower retained for reload/slide composition and hand inertia. The
  // fireable barrel's angular sway is authoritative weapon_t state (gun_sway_tick), so
  // vm_stage_springs never uses this follower to invent another shootable axis.
  float dyaw = p->yaw - v->last_yaw, dpit = p->pitch - v->last_pitch;
  v->last_yaw = p->yaw; v->last_pitch = p->pitch;
  float damp = 1.0f - 0.88f * p->wp.ads_s;  // ADS pins the sight
  float a = f_clamp(14.0f * TICK_DT, 0, 1);
  v->sway_y += (f_clamp(-dyaw / TICK_DT * 0.014f, -0.05f, 0.05f) * sc * damp - v->sway_y) * a;
  v->sway_p += (f_clamp(-dpit / TICK_DT * 0.011f, -0.04f, 0.04f) * sc * damp - v->sway_p) * a;

  // Move inertia: world acceleration projected into gun space.
  v3 dv = v3_add(p->vel, v3_scale(v->last_vel, -1.0f));
  v->last_vel = p->vel;
  float fx = sinf(p->yaw), fz = -cosf(p->yaw);
  float ax = (dv.x * fz * -1.0f + dv.z * fx) / TICK_DT;        // right accel
  float az = (dv.x * fx + dv.z * fz) / TICK_DT;                // forward accel
  a = f_clamp(10.0f * TICK_DT, 0, 1);
  v->lag_x += (f_clamp(-ax * 0.0009f, -0.03f, 0.03f) * sc * damp - v->lag_x) * a;
  v->lag_z += (f_clamp(-az * 0.0007f, -0.035f, 0.035f) * sc * damp - v->lag_z) * a;
  // The vertical channel reads the EYE's own rate as well as the world's: a
  // crouch or a stand moves the eye ~2 m/s while p->vel.y stays 0, and a gun
  // that ignores it teleports through the transition while every jump makes it
  // float. One channel, both sources, so the settle is the same motion.
  if (v->last_eye <= 0.0f) v->last_eye = p->eye;
  float deye = (p->eye - v->last_eye) / TICK_DT;
  v->last_eye = p->eye;
  // A respawn snaps the eye scalar in one tick — 150 m/s is not a motion the
  // gun should answer; the fastest real transition (stand->crouch) is ~2.5.
  if (fabsf(deye) > 8.0f) deye = 0.0f;
  v->lag_y += (f_clamp((-p->vel.y - deye) * 0.006f, -0.035f, 0.035f) * sc * damp - v->lag_y) * a;

  // Walk bob: the phase advances at a STRIDE CADENCE (~1.6..2.3 Hz), not with distance
  // — at 12 m/s a distance phase would wag the gun at ~10 Hz, pure jitter.
  float hs = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
  float run_t = f_clamp(hs / g_cfg.mv[MV_RUN], 0, 1);
  float amp_t = p->grounded && !p->sliding && hs > 0.4f ? run_t : 0;
  v->bob_amp += (amp_t - v->bob_amp) * f_clamp(6.0f * TICK_DT, 0, 1);
  if (v->bob_amp > 0.01f)
    v->bob_phase += F_TAU * (1.6f + 0.7f * run_t) * TICK_DT;
  float bs = v->bob_amp * sc * damp;
  v->bob_x = sinf(v->bob_phase) * 0.014f * bs;
  v->bob_y = -(0.5f - 0.5f * cosf(v->bob_phase * 2.0f)) * 0.011f * bs;

  // Fire response: compression peaks first; the softer lift peaks two ticks later;
  // signed side travel is tiny and tightly damped.
  recoil_spring(&v->kick_back, &v->kick_back_v, RECOIL_BACK_K, RECOIL_BACK_C);
  recoil_spring(&v->kick_lift, &v->kick_lift_v, RECOIL_LIFT_K, RECOIL_LIFT_C);
  recoil_spring(&v->kick_side, &v->kick_side_v, RECOIL_SIDE_K, RECOIL_SIDE_C);

  // Lean lag. The camera's roll and offset are rigid — the gun is not: it hangs off two
  // arms, so it arrives at the new lean a beat late and settles.
  v->lean_lag += (p->lean - v->lean_lag) * f_clamp(9.0f * TICK_DT, 0, 1);

  // The slide blend: fast in (the drop IS violent), slower out (standing up
  // from a skid is a push, not a snap). The pose terms it drives live in
  // vm_build and are all hip-scaled, so ADS keeps its honest sight picture.
  float st = p->sliding ? 1.0f : 0.0f;
  v->slide_s += (st - v->slide_s) *
                f_clamp((p->sliding ? 14.0f : 6.0f) * TICK_DT, 0, 1);

  // The wall tuck's FP half, chasing the same crisp predicate the carriage and the
  // trigger gate read, through the same ease_k kernel at the same 14/7 rates as the
  // body's gunwall_s follower — first and third person may not disagree about a wall.
  int walled = gun_wall_blocked(player_eye_pos(p), p->yaw, p->pitch, &p->wp);
  v->gunwall += ((walled ? 1.0f : 0.0f) - v->gunwall) *
                ease_k(walled ? 14.0f : 7.0f);

  // Roll: slide tilt plus a strafe hint.
  float roll_t = (p->sliding ? 0.09f : 0.0f);
  float side = p->vel.x * fz * -1.0f + p->vel.z * fx;
  roll_t += f_clamp(-side / g_cfg.mv[MV_RUN], -1, 1) * 0.018f * damp;
  v->roll += (roll_t * sc - v->roll) * f_clamp(7.0f * TICK_DT, 0, 1);
}

// One tick of the lean.
static void player_lean_tick(player_t *p, const int held[IN_COUNT]) {
  p->prev_lean = p->lean;
  float want = (held[IN_LEAN_R] ? 1.0f : 0.0f) - (held[IN_LEAN_L] ? 1.0f : 0.0f);
  float sp = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
  float run = g_cfg.mv[MV_RUN] > 0.1f ? g_cfg.mv[MV_RUN] : 0.1f;
  want *= 1.0f - f_clamp((sp / run - LEAN_SPD_LO) / (LEAN_SPD_HI - LEAN_SPD_LO),
                         0.0f, 1.0f);
  if (p->sliding || !p->alive) want = 0.0f;
  float rate = fabsf(want) > fabsf(p->lean_in) ? LEAN_IN_RATE : LEAN_OUT_RATE;
  p->lean_in += (want - p->lean_in) * ease_k(rate);

  // TWO probes, and the pair is the whole design. `now` is the hard constraint — the
  // eye may never be nearer than LEAN_MARGIN to a solid, applied as a floor after the
  // easing rather than through it. `ahead` is the warning: the same ray from where the
  // eye will be in LEAN_LOOK seconds, which lets the ease finish before the wall
  // arrives.
  float clr_now = 1.0f, clr_ahead = 1.0f;
  if (fabsf(p->lean_in) > 1e-4f) {
    float dist = fabsf(lean_eye_off(p->lean_in));
    float s = p->lean_in > 0 ? 1.0f : -1.0f;
    v3 o = player_eye_base(p);
    v3 d = {{cosf(p->yaw) * s, 0, sinf(p->yaw) * s}};
    v3 ah = {{o.x + p->vel.x * LEAN_LOOK, o.y, o.z + p->vel.z * LEAN_LOOK}};
    float ax = ah.x - o.x, az = ah.z - o.z, al = sqrtf(ax * ax + az * az);
    if (al > LEAN_LOOK_MAX) {          // sprinting must not anticipate a wall
      ah.x = o.x + ax * LEAN_LOOK_MAX / al;   // two rooms early
      ah.z = o.z + az * LEAN_LOOK_MAX / al;
    }
    // BOTH probes cast at TWO heights: the base eye AND the eye at the full lean's own
    // drop (the arc lowers it up to 0.091 m).
    float oy2 = o.y - lean_eye_drop(fabsf(p->lean_in));
    float bn = dist, ba = dist;
    for (int i = 0; i < g_num_solids; i++) {
      v3 n;
      for (int hgt = 0; hgt < 2; hgt++) {
        v3 oh = o, ahh = ah;
        if (hgt) { oh.y = oy2; ahh.y = oy2; }
        float t = ray_aabb(oh, d, g_solids[i].min, g_solids[i].max, &n);
        if (t > 0 && t - LEAN_MARGIN < bn) bn = t - LEAN_MARGIN;
        t = ray_aabb(ahh, d, g_solids[i].min, g_solids[i].max, &n);
        if (t > 0 && t - LEAN_MARGIN < ba) ba = t - LEAN_MARGIN;
      }
    }
    clr_now = (bn > 0 ? bn : 0.0f) / dist;
    clr_ahead = (ba > 0 ? ba : 0.0f) / dist;
  }
  float want_clr = fminf(clr_now, clr_ahead);
  p->lean_clr += (want_clr - p->lean_clr) * ease_k(LEAN_CLR_RATE);
  if (p->lean_clr > clr_now) p->lean_clr = clr_now;
  p->lean = p->lean_in * f_clamp(p->lean_clr, 0.0f, 1.0f);
}

// The MOVE half of a human's tick: movement, lean, weapon, and — local only — the
// viewmodel.
static void player_move_tick(player_t *p, int ent, const int held[IN_COUNT],
                             float wish_px, float wish_py) {
  const float *mv = g_sim_mv;
  p->prev_pos = p->pos;
  p->prev_eye = p->eye;
  p->prev_fov_kick = p->fov_kick;
  p->prev_land_dip = p->land_dip;
  int was_grounded = p->grounded;

  // Match over: a fire press queues the rematch (raw input, since the dead-state filter
  // below would swallow it).
  if (!g_online && ent == g_local_ent && g_match.over && held[IN_FIRE] &&
      !p->prev_held[IN_FIRE])
    g_match.restart_req = 1;

  // Dead: gameplay input is ignored (the scoreboard still works), a countdown respawns
  // us somewhere away from the fight. prev_held must keep tracking the RAW input
  // through the substitution: recorded from the zeroed array, every button physically
  // held through death read as a brand-new press afterwards — a held trigger fired the
  // SR on the exact respawn tick, and a trigger held since before the match ended
  // queued the rematch with no deliberate press.
  const int *raw_held = held;
  int dead_held[IN_COUNT] = {0};
  if (!p->alive) {
    if (!g_match.over && --p->respawn_t <= 0) {
      player_respawn(p, ent);
    } else {
      dead_held[IN_SCORE] = held[IN_SCORE];
      dead_held[IN_VIEW] = held[IN_VIEW];  // camera toggling stays available
      held = dead_held;
    }
  }
  // Both LOCAL: a remote client pressing V must not flip the server's
  // config (config_write would persist it on the next menu close), and the
  // scoreboard key is the local player's HUD state.
  if (ent == g_local_ent) {
    g_score_held = held[IN_SCORE];
    if (held[IN_VIEW] && !p->prev_held[IN_VIEW])
      g_cfg.third_person = !g_cfg.third_person;
  }

  // A jump press is buffered for a few ticks so a press just before landing
  // (or on the exact landing tick) is not silently swallowed.
  if (held[IN_JUMP] && !p->prev_held[IN_JUMP]) p->jump_buf = JUMP_BUFFER_TICKS;
  else if (p->jump_buf > 0) p->jump_buf--;

  if (p->slide_cd > 0) p->slide_cd--;

  // Slide: crouch while moving fast on the ground — on the press edge, or on the
  // landing tick with crouch held.
  float sp2d = sqrtf(p->vel.x * p->vel.x + p->vel.z * p->vel.z);
  int crouch_edge = held[IN_CROUCH] && !p->prev_held[IN_CROUCH];
  int landed = p->grounded && !p->prev_grounded;
  if (crouch_edge && !p->grounded) p->slide_queued = 1;
  if (!held[IN_CROUCH]) p->slide_queued = 0;
  // Entry gate on a fraction of run speed — the ground move always reaches
  // full run speed (no friction equilibrium below it), so no slider combo can
  // put the gate out of reach.
  if (!p->sliding && p->grounded && held[IN_CROUCH] && (crouch_edge || landed) &&
      sp2d > mv[MV_RUN] * SLIDE_START_SPEED) {
    p->sliding = 1;
    // The boost fires on a fresh grounded press or a queued airborne press — never when
    // crouch was simply held through a slide-jump, and always cooldown-gated, so hop
    // chains bleed out instead of pumping forever.
    if ((crouch_edge || p->slide_queued) && p->slide_cd == 0) {
      sp2d = mv_slide_launch(&p->vel, sp2d);
      p->slide_cd = SLIDE_COOLDOWN_TICKS;
    }
  }
  if (p->grounded) p->slide_queued = 0;  // the landing consumed (or wasted) it
  // A slide ends by releasing crouch, leaving the ground, or bleeding down to
  // crouch-walk speed — then it settles seamlessly into a regular crouch.
  if (p->sliding && mv_slide_should_end(p->grounded, held[IN_CROUCH], sp2d))
    p->sliding = 0;

  // Crouch state. On the ground the box folds from the top (it follows the smoothed eye
  // down, so the camera never pokes above the box) and standing up requires headroom.
  const float tuck = HEIGHT_STAND - HEIGHT_CROUCH;
  if (held[IN_CROUCH]) {
    if (!p->crouched && !p->grounded) {
      p->pos.y += tuck;
      p->eye -= tuck;
      p->tuck_y = tuck;   // publish it: the figure must not follow this step
    }
    p->crouched = 1;
  } else if (p->crouched) {
    if (p->grounded) {
      if (player_fits(p, HEIGHT_STAND)) p->crouched = 0;
    } else {
      p->pos.y -= tuck;  // legs extend back down, head stays level
      if (p->pos.y >= 0 && player_fits(p, HEIGHT_STAND)) {
        p->eye += tuck;
        p->crouched = 0;
        p->tuck_y = 0.0f;
      } else {
        p->pos.y += tuck;  // no room below (solid or arena floor): stay tucked
      }
    }
  }
  float height = player_col_height(p);
  float max_speed = p->crouched ? mv[MV_CROUCH_SP] : mv[MV_RUN];
  max_speed *= mv_ads_walk_scale(p->wp.ads_s);  // aimed walk: slight tactical slow

  // Wish direction in the yaw plane.
  float fx = sinf(p->yaw), fz = -cosf(p->yaw);
  float wx = 0, wz = 0;
  if (held[IN_FWD])   { wx += fx; wz += fz; }
  if (held[IN_BACK])  { wx -= fx; wz -= fz; }
  if (held[IN_RIGHT]) { wx += -fz; wz += fx; }
  if (held[IN_LEFT])  { wx -= -fz; wz -= fx; }
  // Left stick: analog move intent in the same wish space, handed in by the caller
  // AFTER deadzone (the sim never reads g_pad: pad settings are client preference, and
  // a server applying ITS deadzone to a client's stick is a permanent misprediction).
  if (wish_px != 0.0f || wish_py != 0.0f) {
    wx += fx * wish_py + -fz * wish_px;
    wz += fz * wish_py + fx * wish_px;
  }
  float wlen = sqrtf(wx * wx + wz * wz);
  if (wlen > 1e-5f) {
    float m = wlen > 1.0f ? 1.0f : wlen;
    wx = wx / wlen * m;
    wz = wz / wlen * m;
  }

  if (p->grounded) {
    if (p->sliding) sp2d = mv_slide_step(&p->vel, sp2d, wx, wz, wlen);
    else            mv_walk(&p->vel, wx, wz, wlen, max_speed);
    if (p->jump_buf > 0) {
      p->vel.y = mv_jump_speed();
      p->grounded = 0;
      p->sliding = 0;  // slide-jump: full horizontal momentum carries over
      p->jump_buf = 0;
    }
  } else {
    mv_air(&p->vel, wx, wz, wlen, sp2d);
  }

  p->vel.y -= GRAVITY * TICK_DT;
  p->grounded = 0;
  float vy_pre = p->vel.y;  // impact speed: the move zeroes vel.y on contact
  ent_move(&p->pos, &p->vel, &p->grounded, height);
  arena_clamp(&p->pos);
  // Touching down un-bakes the tuck WITHOUT a step in pos.y: on the ground the
  // collider's floor is the boots again (a grounded crouch folds the box from the TOP,
  // following the smoothed eye).
  if (p->grounded) p->tuck_y = 0.0f;

  // Landing dip: a fresh ground contact compresses the eye in proportion to the impact
  // speed, then springs back.
  p->land_dip -= p->land_dip * f_clamp(LAND_DIP_RECOVER * TICK_DT, 0, 1);
  if (p->grounded && !was_grounded && vy_pre < 0) {
    float d = -vy_pre * LAND_DIP_SCALE;
    if (d > LAND_DIP_MAX) d = LAND_DIP_MAX;
    if (d > p->land_dip) p->land_dip = d;
  }

  // Dead: the camera sinks toward the floor — a cheap, readable death cam.
  float eye_target = !p->alive ? 0.35f : p->crouched ? EYE_CROUCH : EYE_STAND;
  p->eye += (eye_target - p->eye) * f_clamp(EYE_LERP_RATE * TICK_DT, 0, 1);

  // FOV kick: widen while sliding, eased both ways for a smooth speed surge.
  float kick_target = p->sliding ? 1.0f : 0.0f;
  p->fov_kick += (kick_target - p->fov_kick) * f_clamp(FOV_KICK_RATE * TICK_DT, 0, 1);

  // Lean before the weapon: a shot fired this tick leaves from the leaned eye.
  player_lean_tick(p, held);

  weapon_tick(p, ent, held);
  if (ent == g_local_ent) vm_tick(p);   // the viewmodel is the local client's
  p->prev_grounded = was_grounded;      // read only by our own next move tick
  for (int i = 0; i < IN_COUNT; i++) p->prev_held[i] = raw_held[i];
}

// "THIS SOLDIER IS FIGHTING" — the player's carry predicate, and the ONE copy of it.
// Two callers: the server/offline pose (below) and the online client's own predicted
// body, which is the one entity no snapshot bit describes (client_anim_all).
static int player_carry_ready(const player_t *p) {
  return p->prev_held[IN_FIRE] || p->prev_held[IN_ADS] || p->wp.cool[p->wp.cur] > 0 ||
         // THE WEAPON IS BUSY IN HIS HANDS. Nobody reloads or brings up a fresh rifle
         // with the last one hanging 34 degrees off the shoulder, and both states last
         // the better part of a second — long enough for an opponent to read them as
         // the figure standing there doing nothing.
         p->wp.reload_t > 0 || p->wp.switch_t > 0 ||
         // ...AND HE IS BEING SHOT AT. This is the human's half of the bot's
         // `tgt >= 0`: a bot raises its rifle when it acquires a man, and the one
         // signal a mouse-aiming player gives that he is in a fight BEFORE his own
         // trigger says so is that rounds are landing on him. Without it the rifle
         // came up on the trigger and nowhere earlier, which is a carry state that
         // only ever tells an opponent the truth after the first round has left.
         p->flash > 0;
}

// The PRESENT half: anim solve and the corpse handover.
static void player_present_tick(player_t *p, int ent) {
  int h = human_of_ent(ent);
  // THE HIT FLASH COUNTS DOWN HERE, in the half that runs exactly once per tick — the
  // move half is replayed on every rollback, which burnt a 12-tick flash in four.
  // client_anim_all runs this same line for the entities it solves instead.
  if (p->flash > 0) p->flash--;
  // prev_held holds THIS tick's raw input (copied at the end of the move
  // half), and the anim only runs alive, where raw == effective held.
  if (p->alive) {
    // THE PLAYER'S CARRY PREDICATE, AND IT IS READ BACK OFF THE ENTITY. A client
    // cannot re-derive this one — it never sees a foreign player's buttons — so it
    // travels as a flag bit, and computing it a second time at the wire is exactly
    // how the pose and the packet would drift apart.
    p->wp.ready_want = (unsigned char)player_carry_ready(p);
    int idle_ok = gun_pose_state(&p->wp, &g_player_anim[h], p->vel,
                                   p->grounded, p->sliding);
    weapon_anim_tick(&g_player_anim[h], p->pos, player_eye_pos(p), p->vel,
                     p->yaw, p->pitch,
                     p->grounded, p->crouched && !p->sliding, p->tuck_y,
                     p->sliding, p->wp.ads_s, p->lean, p->wp.ready_want, ent,
                     &p->wp, idle_ok);
  }
  else {
    g_player_anim[h].prev_pose = g_player_anim[h].pose;
    if (g_player_corpse_q[h]) {
      g_player_corpse_q[h] = 0;
      corpse_add(&g_player_anim[h], p->pos, p->pos, g_player_rag[h], p->yaw,
                 0.0f, 0.0f, gun_anim(&p->wp), p->wp.cur, ent);
    }
  }
}

// Input storage for humans 1..MAX_HUMANS-1 — filled from the network (Phase
// 3+); slot 0 is unused (the local player's input arrives per call).
static int   g_human_held_all[SRV_LOBBIES][MAX_HUMANS][IN_COUNT];
#define g_human_held (g_human_held_all[g_lobby])
static float g_human_wish_all[SRV_LOBBIES][MAX_HUMANS][2];
#define g_human_wish (g_human_wish_all[g_lobby])
// Whether this human's move tick runs THIS world tick.
static int   g_human_move_all[SRV_LOBBIES][MAX_HUMANS] =
    {[0 ... SRV_LOBBIES - 1] = {[0 ... MAX_HUMANS - 1] = 1}};
#define g_human_move (g_human_move_all[g_lobby])
// The view tick of the input consumed this world tick (lag comp);
// -1 = no rewind (offline, or the client is not synced yet). Consume-written.
static long  g_human_view_all[SRV_LOBBIES][MAX_HUMANS] =
    {[0 ... SRV_LOBBIES - 1] = {[0 ... MAX_HUMANS - 1] = -1}};
#define g_human_view (g_human_view_all[g_lobby])

// THE WORLD TICK. The name and signature stay: sim_advance and app_frame call this once
// per tick with the LOCAL player's input.
static void player_tick(player_t *p, const int held[IN_COUNT]) {
  (void)p;   // p is &g_players[0]; the roster below covers it
  g_tick++;
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h]) continue;
    int ent = ent_of_human(h);
    player_t *hp = &g_players[h];
    float wpx = 0.0f, wpy = 0.0f;
    const int *hh = g_human_held[h];
    if (ent == g_local_ent) {
      hh = held;
      // The pad wish is assembled HERE, outside the sim half — local input
      // only, gated exactly as frame_input_keys gates the keyboard.
      if (g_pad.connected && hp->alive && !g_menu_open) {
        float mx, my;
        if (pad_deadzone(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L], &mx, &my) > 0) {
          wpx = mx;
          wpy = my;
        }
      }
    } else {
      if (!g_human_move[h]) continue;   // no input consumed -> no move
      wpx = g_human_wish[h][0];
      wpy = g_human_wish[h][1];
      // Shots inside THIS move trace against the world the shooter saw.
      g_lagcomp_view = g_human_view[h];
    }
    player_move_tick(hp, ent, hh, wpx, wpy);
    g_lagcomp_view = -1;               // bots and the present half stay live
  }
  match_tick();
  vfx_tick();
  for (int h = 0; h < MAX_HUMANS; h++)
    if (g_humans_on[h])
      player_present_tick(&g_players[h], ent_of_human(h));
  // The outbound event log is filled HERE, in the one world tick every caller shares,
  // and not in the server's own entry point: each caller drains g_events at its own
  // moment — the harness at the end of sim_advance, the server right after its tick —
  // so a capture anywhere else sees an empty list for somebody.
  net_ev_capture();
}

// One SERVER tick: the same world tick plus the PER-TICK event drain — without the
// drain the 128-slot ring saturates after ~15 ticks, silently (measured ev_peak is 8-10
// per tick at 20 bots hard).
static void net_srv_consume_inputs(void);   // defined with the conn layer
[[maybe_unused]] static void server_tick_once(const int held[IN_COUNT]) {
  static const int no_held[IN_COUNT] = {0};
  net_srv_consume_inputs();   // one buffered client input per world tick
  player_tick(&g_players[0], held ? held : no_held);
  net_hist_record();
  g_num_events = 0;      // player_tick already captured them into the log
}
