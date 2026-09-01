// =============================================================================
// harness/proofs_net.inc  —  network/recoil/vmframe proofs.
// Proof: the proofs themselves. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: these are the multiplayer acceptance gate; a wire change needs
// NET_PROTO bumped and server + client redeployed together — EXCEPT one that
// is strictly additive into space an older build already reads as zero (the
// free NEF_ bits, NP_TELE), which degrades to the old behaviour both ways and
// is argued at its own gate rather than bumped.
// =============================================================================
// A headless client against a real --server on localhost: handshake, reconcile
// and event-channel stats, end to end and without GL.
static void netclient_proof(void) {
  // Headless end-to-end client: connect to a running `--server` on HOST:PORT, drive N
  // ticks of scripted input, and print the handshake result plus
  // prediction/reconciliation stats.
  const char *host = next_tok();
  int port = (int)next_n();
  long nticks = next_n();
  // Optional ACTS: comma-separated script action names held for the whole run, e.g.
  // "fire" or "forward,fire,ads"; default "forward", which is what this proof used to
  // hardcode.
  int held[IN_COUNT] = {0};
  char *at = take_tok();
  int got_acts = 0;
  if (at) {
    int tmp[IN_COUNT] = {0}, ok = 1;
    char buf[128];
    snprintf(buf, sizeof buf, "%s", at);
    for (char *p = buf, *q = buf; ok; q++) {
      if (*q == ',' || !*q) {
        int last = !*q;
        *q = 0;
        int a = -1;
        for (int k = 0; k < IN_COUNT; k++)
          if (!strcmp(p, ACTION_DEF[k].script)) a = k;
        if (a < 0) ok = 0; else tmp[a] = 1;
        p = q + 1;
        if (last) break;
      }
    }
    if (ok) { memcpy(held, tmp, sizeof held); got_acts = 1; }
    else g_tok_pushback = at;
  }
  if (!got_acts) held[IN_FWD] = 1;       // the historical default
  uint32_t ip = net_resolve(host);
  if (!ip) { printf("netclient resolve FAIL %s\n", host); }
  else {
    net_cl_connect(ip, (uint16_t)port, g_cfg.name);
    uint32_t t0 = net_now_ms();
    // pump the handshake (the server runs in its own process/loop)
    while (g_cl.state == NC_CONNECTING || g_cl.state == NC_JOINING) {
      net_cl_poll();
      if (net_now_ms() - t0 > 5000) break;
      struct timespec ts = {0, 5000000L}; nanosleep(&ts, NULL);
    }
    if (g_cl.state != NC_PLAYING) {
      printf("netclient state=%s reject=%d (no server on %s:%d?)\n",
             g_cl.state == NC_FAILED ? "FAILED" : "TIMEOUT", g_cl.reject,
             host, port);
    } else {
      // Drive input at ~120 Hz wall-clock, predict locally, let snapshots
      // reconcile. Simple forward-walk input so motion actually happens.
      g_cl_corrections = 0; g_cl_worst_err_mm = 0.0f;
      g_cl_ev_applied = 0; g_cl_ev_dupes = 0;
      int h = human_of_ent(g_cl.my_ent);
      // CLOCK-LOCKED TO TICK_DT, not a free-running nanosleep.
      struct timespec next;
      clock_gettime(CLOCK_MONOTONIC, &next);
      for (long i = 0; i < nticks; i++) {
        player_t *p = h >= 0 ? &g_players[h] : &g_players[0];
        // Firing blind at a wall proves the event channel, not the hit path.
        if (held[IN_FIRE] && h >= 0) {
          int tgt = -1; float best = 1e9f;
          for (int bi = 0; bi < g_match.nbots; bi++) {
            if (!g_bots[bi].active || !g_bots[bi].alive) continue;
            v3 dd = v3_sub(g_bots[bi].pos, p->pos);
            float d2 = dd.x * dd.x + dd.z * dd.z;
            if (d2 < best) { best = d2; tgt = bi + 1; }
          }
          if (tgt > 0) aim_at(p, ent_aimpoint(tgt));  // the one aim helper
        }
        // Movement rides the HELD buttons only — the rig used to hardwire
        // a full forward wish besides, so "netclient … fire" never stood
        // still and no scenario could separate standing from walking.
        net_cl_tick(held, 0.0f, 0.0f, p->yaw, p->pitch);   // predict + send
        net_cl_poll();                     // recv snapshots -> reconcile
        net_cl_extrapolate();              // the rig aims at the same dead-
                                           // reckoned targets the player
                                           // sees — view ticks assume it
        // The same clock nudge the played client runs: hold the
        // server-side queue depth at target instead of letting scheduler
        // drift walk it into the trim. The rig proves the mechanism.
        float qe = NET_QDEPTH_TARGET - g_cl.qdepth_ema;
        double rate = 1.0 + (double)f_clamp(qe * 0.004f, -0.03f, 0.01f);
        next.tv_nsec += (long)(1e9 / TICK_HZ / rate);
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
      }
      v3 fp = h >= 0 ? g_players[h].pos : (v3){{0,0,0}};
      // ev_applied counts events the SERVER sent and this client accepted; ev_dupes
      // counts resends the high-water mark rejected.
      char actbuf[96] = {0};
      for (int k = 0; k < IN_COUNT; k++)
        if (held[k]) {
          if (actbuf[0]) strncat(actbuf, ",", sizeof actbuf - strlen(actbuf) - 1);
          strncat(actbuf, ACTION_DEF[k].script, sizeof actbuf - strlen(actbuf) - 1);
        }
      printf("netclient CONNECTED ent=%d acts=%s world=%08x server_tick=%u "
             "corrections=%d worst_err=%.1fmm ev_applied=%d ev_dupes=%d "
             "ev_high=%u hp=%d alive=%d pred_ammo=%d pos=(%.2f %.2f %.2f) "
             "ping=%d qdepth=%.1f kills=%d deaths=%d shots=%d hits=%d "
             "fraglimit=%d reject=%d\n",
             g_cl.my_ent, actbuf, g_cl.world, g_cl.server_tick, g_cl_corrections,
             (double)g_cl_worst_err_mm, g_cl_ev_applied, g_cl_ev_dupes,
             g_cl.ev_high,
             h >= 0 ? g_players[h].hp : -1, h >= 0 ? g_players[h].alive : -1,
             h >= 0 ? g_players[h].wp.ammo[g_players[h].wp.cur] : -1,
             (double)fp.x, (double)fp.y, (double)fp.z,
             g_net_ping[g_cl.my_ent], (double)g_cl.qdepth_ema,
             g_match.kills[g_cl.my_ent], g_match.deaths[g_cl.my_ent],
             g_match.shots[g_cl.my_ent], g_match.hits[g_cl.my_ent],
             g_net_fraglimit, g_cl.reject);
    }
    net_cl_disconnect();
  }
}

// --- The carriage round trip -------------------------------------------------
// net_ent_from_live's contract, taken literally: "presentation is deliberately absent
// — the client re-solves it" holds only while every INPUT that solver needs is on the
// wire. anim_tick takes thirteen; three of them describe how the weapon is HELD (the
// aim blend, the carry state, the mid-air leg tuck) and they are the ones a snapshot
// used to drop on the floor, so every remote figure stood in the low carry forever
// while its (correctly synced) pitch pointed somewhere else entirely.
//
// One entity goes live -> from_live -> encode -> decode -> apply_ent, the carriage is
// wiped in between the way a client that knows nothing has it, and what comes out the
// far side has to be what went in. Then the client's own ramp runs the SAME number of
// ticks the server ran and has to land on the same blend.
typedef struct { int ads_want, ready_want; float ads_s, tuck; } carry_t;

static carry_t netcarry_read(int ent) {
  int h = human_of_ent(ent);
  const weapon_t *w = h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp;
  float tuck = h >= 0 ? g_players[h].tuck_y : 0.0f;
  return (carry_t){w->ads_want, w->ready_want, w->ads_s, tuck};
}

// Erase everything the wire is supposed to carry, so a field that survives by
// ACCIDENT (this is one process — server and client share the globals) cannot pass.
static void netcarry_wipe(int ent) {
  int h = human_of_ent(ent);
  weapon_t *w = h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp;
  w->ads_want = w->ready_want = 0;
  w->ads_lin = w->ads_s = 0.0f;
  if (h >= 0) g_players[h].tuck_y = 0.0f;
}

// Ramp the server side for `ticks`, then round-trip and ramp the client side for the
// same count. Returns 0 on a mismatch, and prints the row either way.
static int netcarry_case(const char *label, int ent, int ticks) {
  carry_t srv = netcarry_read(ent);
  for (int i = 0; i < ticks; i++) {
    int h = human_of_ent(ent);
    gun_ads(h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp, srv.ads_want);
  }
  srv = netcarry_read(ent);
  net_ent_t ne;
  net_ent_from_live(&ne, ent);
  unsigned char buf[64];
  np_wr w = {buf, sizeof buf, 0, 0};
  net_ent_encode(&w, &ne);
  np_rd r = {buf, w.at, 0, 0};
  net_ent_t out;
  int decoded = net_ent_decode(&r, &out);
  netcarry_wipe(ent);
  if (decoded) net_cl_apply_ent(&out);
  // The client re-solves the blend from the intent, at its own tick rate, through the
  // one gun_ads — so it has to arrive at the server's number, not merely at "nonzero".
  for (int i = 0; i < ticks; i++) net_cl_present_advance();
  carry_t cl = netcarry_read(ent);
  float dads = fabsf(cl.ads_s - srv.ads_s);
  // ...AND THE POSE HAS TO READ THEM. Carrying the bits and feeding them to anim_tick
  // are two different jobs, and this proof passed the first one while client_anim_all
  // was still handing the solver a literal 0 — so drive the real client tick and take
  // the carry state off the anim itself.
  int h2 = human_of_ent(ent);
  anim_t *an = h2 >= 0 ? &g_player_anim[h2] : &g_bots[ent - 1].anim;
  an->ready_s = an->ads_s = 0.0f;
  for (int i = 0; i < 30; i++) { g_tick++; client_anim_all(); }
  float pose_ready = an->ready_s, pose_ads = an->ads_s;
  // 30 ticks of ease_k(20) from 0 is ~0.99 up and ease_k(14) puts the blend past
  // half; lowered, both stay pinned at 0 because nothing ever asked them to rise.
  int want_up = srv.ready_want || srv.ads_want;
  int pose_ok = want_up ? (pose_ready > 0.5f && (!srv.ads_want || pose_ads > 0.3f))
                        : (pose_ready < 0.05f && pose_ads < 0.05f);
  int ok = decoded && cl.ads_want == srv.ads_want &&
           cl.ready_want == srv.ready_want &&
           fabsf(cl.tuck - srv.tuck) < 1e-4f && dads < 0.02f && pose_ok;
  printf("netcarry %s ent=%d ads_want=%d/%d ready=%d/%d tuck=%.3f/%.3f "
         "ads_s=%.3f/%.3f d=%.4f pose_ready=%.2f pose_ads=%.2f %s\n",
         label, ent, srv.ads_want, cl.ads_want, srv.ready_want, cl.ready_want,
         (double)srv.tuck, (double)cl.tuck, (double)srv.ads_s, (double)cl.ads_s,
         (double)dads, (double)pose_ready, (double)pose_ads, ok ? "ok" : "FAIL");
  return ok;
}

static void netcarry_proof(void) {
  g_online = 1;
  g_local_ent = 0;               // human 0 is us; everything below is FOREIGN
  g_cl.my_ent = 0;
  g_cl.synced = 1;
  // A HUMAN WHO IS AIMING, FIGHTING AND TUCKED — the three bits at once, because a
  // flags byte is exactly where two of them get packed into one shift.
  int h = 1, ent = ent_of_human(h);
  g_humans_on[h] = 1;
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 0;   // airborne: the tuck is legal
  p->pitch = 0.42f;
  p->tuck_y = HEIGHT_STAND - HEIGHT_CROUCH;
  p->wp.ads_want = 1;
  p->wp.ready_want = 1;
  anim_reset(&g_player_anim[h], p->pos, p->yaw, p->wp.cur);
  int ok = netcarry_case("human-ads", ent, 12);

  // ...AND THE SAME FOR A BOT, whose carriage rides the identical bits: a client that
  // only learned to read them for players sees twenty figures still slung.
  g_match.nbots = 1;
  bot_t *b = &g_bots[0];
  memset(b, 0, sizeof *b);
  b->active = 1; b->alive = 1; b->hp = 100;
  b->pos = b->prev_pos = (v3){{2.0f, 0.0f, 6.0f}};
  wpn_fill_mags(&b->wp);
  b->wp.ads_want = 1;
  b->wp.ready_want = 1;
  anim_reset(&b->anim, b->pos, b->yaw, b->wp.cur);
  ok &= netcarry_case("bot-ads", 1, 12);

  // HIP FIRE — READY WITHOUT ADS, and it is the case that actually tests the READY
  // bit. anim_tick raises the carry on `ready || ads > 0.05`, so with the rifle at the
  // eye the ready argument is redundant and a client that dropped it still looks
  // right; a man shooting from the hip at four metres is where the dropped bit shows.
  p->wp.ads_want = 0;
  p->wp.ready_want = 1;
  p->tuck_y = 0.0f;
  ok &= netcarry_case("human-hipfire", ent, 40);

  // THE OTHER EDGE. A rifle that comes DOWN has to come down too: an intent bit that
  // is only ever read when set is a bit that never clears.
  p->wp.ready_want = 0;
  ok &= netcarry_case("human-lowered", ent, 40);

  printf("netcarry %s\n", ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// --- The foreign figure's IMPULSES -------------------------------------------
// netcarry proves the LEVELS arrive: the aim intent, the carry state and the tuck ride
// the flag byte because they are continuous and a client cannot derive them. This
// proves the EDGES do. A shot's kick, a reload's clock and a round's flinch are the
// three things that move a body which no snapshot FIELD describes — they were written
// inside the sim, at the action, and a client never runs the sim for a foreign entity,
// so on every figure a player actually looks at all three were silently absent:
// measured before ent_on_event existed, fire_k held 0.000 across a whole burst and
// reload_t never left zero. They ride the event channel, which already carries all
// three with the shooter's and the victim's id on them.
//
// One entity, driven ONLY the way a client drives one — wire record in, events in,
// net_cl_present_advance and client_anim_all for the clocks — and the four things a
// viewer has to be able to see have to come out.
static float netfigure_pose_bore_off(const pose_t *pose, float yaw,
                                     float pitch) {
  v3 aim = {{sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)}};
  v3 gf = v3_norm(pose->gun_f);
  return acosf(f_clamp(v3_dot(gf, aim), -1.0f, 1.0f)) * (180.0f / F_PI);
}

static float netfigure_bore_off_aim(int ent) {
  const anim_t *a = ent_anim(ent);
  int h = human_of_ent(ent);
  float yaw   = h >= 0 ? g_players[h].yaw   : g_bots[ent - 1].yaw;
  float pitch = h >= 0 ? g_players[h].pitch : g_bots[ent - 1].pitch;
  return netfigure_pose_bore_off(&a->pose, yaw, pitch);
}

typedef struct {
  int ok;
  float back1, lift1;
} netfigure_fire_t;

// One foreign shot, from a fully lowered and fully settled body. The event's ballistic
// segment begins on that live barrel, adds recoil VELOCITY, and does not overwrite the
// visible body. The next client presentation tick begins the eased raise and owns the
// first spring displacement. Running this once per weapon proves EV_FIRE.wpn reaches
// both the live event anchor and the authored impulse without reintroducing an arm pop.
static netfigure_fire_t netfigure_fire_case(int ent, int wpn) {
  int h = human_of_ent(ent);
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 1;
  p->yaw = 0.7f; p->pitch = -0.05f;
  p->wp.cur = wpn;
  p->wp.ads_want = p->wp.ready_want = 0;   // PATROL: begin fully lowered
  anim_reset(&g_player_anim[h], p->pos, p->yaw, p->wp.cur);
  anim_t *a = ent_anim(ent);
  // A reset deliberately starts the SR shouldered; give its documented deliberate
  // lower the full 0.75 s before calling this a "fully settled" patrol pose.
  for (int i = 0; i < 90; i++) {
    g_tick++;
    net_cl_present_advance();
    client_anim_all();
  }

  float slung = netfigure_bore_off_aim(ent);
  float ready_slung = a->ready_s;
  v3 muz_slung = pose_muzzle(&a->pose, wpn);
  v3 bore_slung = v3_norm(a->pose.gun_f);

  // No old tracer or queued event may satisfy this case accidentally. Clearing VFX
  // also makes the one live tracer below an unambiguous record of the anchor actually
  // consumed by vfx_ev_fire, rather than an inferred copy of it.
  vfx_match_reset();
  g_num_events = 0;
  int shot = 7 + wpn;
  ev_push((event_t){.type = EV_FIRE, .src = ent, .wpn = wpn, .i = shot,
                    .a = v3_add(muz_slung, v3_scale(bore_slung, 20.0f)),
                    .b = bore_slung,
                    .f = 20.0f, .g = p->wp.ads_s});

  int ti = -1;
  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0 && g_tracers[i].src == ent) { ti = i; break; }
  v3 muz0 = pose_muzzle(&a->pose, wpn);
  int staged = g_fire_anchor_src == ent && g_fire_anchor_tick == g_tick;
  v3 event_muz0 = staged ? pose_muzzle(&g_fire_anchor_pose, wpn) : muz0;
  v3 tracer = ti >= 0 ? g_tracers[ti].a : event_muz0;
  float anchor0 = v3_len(v3_sub(tracer, event_muz0));
  float visible0 = v3_len(v3_sub(muz0, muz_slung));
  float axis0 = staged ? netfigure_pose_bore_off(&g_fire_anchor_pose,
                                                  p->yaw, p->pitch)
                       : 180.0f;
  float latch0 = a->fire_k;
  float back0 = a->recoil_back, lift0 = a->recoil_lift, side0 = a->recoil_side;
  float backv0 = a->recoil_back_v, liftv0 = a->recoil_lift_v,
        sidev0 = a->recoil_side_v;
  float raised = v3_len(v3_sub(muz_slung, event_muz0));

  g_tick++;
  net_cl_present_advance();
  client_anim_all();
  v3 muz1 = pose_muzzle(&a->pose, wpn);
  float axis1 = netfigure_bore_off_aim(ent);
  float toward0 = v3_len(v3_sub(tracer, muz0));
  float ready1 = a->ready_s;
  float back1 = a->recoil_back, lift1 = a->recoil_lift, side1 = a->recoil_side;
  float step1 = v3_len(v3_sub(muz1, muz0));

  // T0 body values and pose are continuous, while all three velocities and the event
  // anchor already carry the shot. T1 is the first displaced, partially raised pose;
  // it must move toward the fired muzzle rather than teleport there.
  int zero0 = fabsf(back0) < 1e-6f && fabsf(lift0) < 1e-6f &&
              fabsf(side0) < 1e-6f;
  int impulse0 = backv0 > 0.5f && liftv0 > 0.3f && fabsf(sidev0) > 0.02f;
  int moved1 = back1 > 0.004f && lift1 > 0.001f && fabsf(side1) > 0.0001f;
  int ok = ti >= 0 && staged && ready_slung < 0.05f && slung > 20.0f &&
           raised < 0.002f && visible0 < 0.002f && latch0 > 0.9f &&
           anchor0 < 0.002f && fabsf(axis0 - slung) < 0.25f &&
           zero0 && impulse0 && moved1 && ready1 > 0.10f && ready1 < 0.30f &&
           toward0 < 0.002f && step1 > 0.002f && step1 < 0.12f &&
           axis1 + 0.05f < axis0;
  printf("netfigure fire=%s slung=%.2fdeg raise=%.2fm | "
         "T0 anchor=%.2fmm hold=%.2fmm axis=%.2fdeg "
         "kick=(%.2fmm %.2fdeg %+.2fmm) "
         "vel=(%.2f %.2f %+.2f) latch=%.2f | "
         "T1 step=%.2fmm axis=%.2fdeg ready=%.3f "
         "kick=(%.2fmm %.2fdeg %+.2fmm) %s\n",
         wpn == WPN_SR ? "sr" : "ar", (double)slung, (double)raised,
         (double)(anchor0 * 1000.0f), (double)(visible0 * 1000.0f),
         (double)axis0,
         (double)(back0 * 1000.0f), (double)(lift0 / DEG2RAD),
         (double)(side0 * 1000.0f), (double)backv0, (double)liftv0,
         (double)sidev0, (double)latch0, (double)(step1 * 1000.0f),
         (double)axis1, (double)ready1, (double)(back1 * 1000.0f),
         (double)(lift1 / DEG2RAD), (double)(side1 * 1000.0f),
         ok ? "ok" : "FAIL");
  return (netfigure_fire_t){ok, back1, lift1};
}

static void netfigure_proof(void) {
  // Stage the complete client-visible roster rather than inheriting humans, bots,
  // prediction or queued events from an earlier command in the same --do script.
  g_online = 1;
  g_local_ent = 0;
  g_cl.my_ent = 0;
  g_cl.synced = 1;
  g_predicting = 0;
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 0;
  g_num_events = 0;
  vfx_match_reset();
  memset(g_cl_alive_prev, 0, sizeof g_cl_alive_prev);
  g_humans_on[0] = 1;
  player_spawn(&g_players[0]);
  anim_reset(&g_player_anim[0], g_players[0].pos, g_players[0].yaw,
             g_players[0].wp.cur);
  int h = 1, ent = ent_of_human(h);
  g_humans_on[h] = 1;
  g_cl_alive_prev[0] = g_cl_alive_prev[ent] = 1;
  player_t *p = &g_players[h];
  netfigure_fire_t ar = netfigure_fire_case(ent, WPN_AR);
  netfigure_fire_t sr = netfigure_fire_case(ent, WPN_SR);
  // A copied AR impulse under the SR label would satisfy all shape invariants above.
  // Require the authored heavy-rifle response as well, without duplicating its table.
  int ok_weapon = sr.back1 > ar.back1 * 1.4f && sr.lift1 > ar.lift1 * 1.8f;

  // The remaining edge checks run once, on a freshly staged AR weapon, exactly as the
  // original proof did. Fire-case state may not leak into reload/hit bookkeeping.
  g_num_events = 0;
  vfx_match_reset();
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 1;
  p->yaw = 0.7f; p->pitch = -0.05f;
  p->wp.cur = WPN_AR;
  anim_reset(&g_player_anim[h], p->pos, p->yaw, p->wp.cur);
  anim_t *A = ent_anim(ent);
  weapon_t *W = ent_weapon(ent);

  // 1. THE RELOAD, and it has to END. A clock stamped by the event and never counted
  // down is a magazine change that begins and hangs — which is what a client did with
  // reload_t before net_cl_present_advance ran gun_reload_tick on foreign weapons.
  int dur = wpn_reload_ticks(W->cur, 0);
  ev_push((event_t){.type = EV_RELOAD, .src = ent, .a = p->pos, .wpn = W->cur, .k = 0});
  int started = W->reload_t;
  // ...and the POSE has to move with it: gun_anim is what the figure's hands read.
  gunanim_t mid = {0};
  for (int i = 0; i < dur; i++) {
    if (i == dur / 3) mid = gun_anim(W);
    g_tick++; net_cl_present_advance(); client_anim_all();
  }
  int ok_reload = started == dur && W->reload_t == 0 &&
                  (fabsf(mid.arc) > 0.01f || fabsf(mid.mag) > 0.01f);

  // 2. THE ROUND LANDING: the hit flash and the flinch, both off the same EV_HIT, and
  // the flash has to EXPIRE — set by an event and counted down by nobody is a figure
  // that stays lit for the rest of the match.
  A->hit_k = 0.0f;
  ev_push((event_t){.type = EV_HIT, .a = v3_add(p->pos, (v3){{0.0f, 1.10f, 0.0f}}),
                    .b = v3_add(p->pos, (v3){{0.0f, 1.50f, 4.0f}}),
                    .i = 26, .k = 74, .src = 0, .dst = ent});
  int lit = p->flash, flinched = A->hit_k > 0.01f;
  for (int i = 0; i < BOT_FLASH_TICKS + 2; i++) {
    g_tick++; net_cl_present_advance(); client_anim_all();
  }
  int ok_hit = lit == BOT_FLASH_TICKS && flinched && p->flash == 0;

  // 3. OUR OWN BODY IS NOT A FOREIGN ONE. The local player's reload is PREDICTED ticks
  // before the server's echo of it arrives; re-stamping the clock on that echo would
  // add a full round trip to every magazine. The running clock is the whole test — no
  // mode predicate — so a reload already in flight has to survive its own event.
  player_t *me = &g_players[0];
  me->wp.cur = WPN_AR;
  me->wp.reload_t = 5;
  me->wp.reload_tac = 1;
  ev_push((event_t){.type = EV_RELOAD, .src = 0, .a = me->pos,
                    .wpn = me->wp.cur, .k = 0});
  int ok_mine = me->wp.reload_t == 5 && me->wp.reload_tac == 1;

  // 4. THE CARRY PREDICATE ITSELF — the server-side half, and the one thing on this
  // list a client cannot re-derive (it never sees a foreign player's buttons), so it
  // travels as NEF_READY and netcarry proves the transport. What it has to SAY is
  // here: a man whose weapon is busy in his hands, or who is taking rounds, is
  // fighting. Without those rows the rifle came up on the trigger and nowhere earlier,
  // which is a carry that only tells an opponent the truth after the first round.
  static player_t q;                       // static: player_t is not a stack object
  q = (player_t){0};
  q.wp.cur = WPN_AR;
  int cr_patrol = player_carry_ready(&q);
  q.wp.reload_t = 40; int cr_reload = player_carry_ready(&q); q.wp.reload_t = 0;
  q.wp.switch_t = 12; int cr_swap   = player_carry_ready(&q); q.wp.switch_t = 0;
  q.flash = BOT_FLASH_TICKS; int cr_hit = player_carry_ready(&q); q.flash = 0;
  q.prev_held[IN_FIRE] = 1; int cr_fire = player_carry_ready(&q);
  int ok_carry = !cr_patrol && cr_reload && cr_swap && cr_hit && cr_fire;

  int ok = ar.ok && sr.ok && ok_weapon && ok_reload && ok_hit && ok_mine && ok_carry;
  printf("netfigure carry patrol=%d reload=%d swap=%d hit=%d fire=%d | ",
         cr_patrol, cr_reload, cr_swap, cr_hit, cr_fire);
  printf("weapon_delta back=%.2fx lift=%.2fx | reload %d/%d end=%d "
         "arc=%.2f | flash=%d/%d flinch=%d expired=%d | own_reload=%d %s\n",
         (double)(sr.back1 / fmaxf(ar.back1, 1e-6f)),
         (double)(sr.lift1 / fmaxf(ar.lift1, 1e-6f)),
         started, dur, W->reload_t,
         (double)mid.arc, lit, BOT_FLASH_TICKS, flinched, p->flash == 0,
         me->wp.reload_t, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// --- Recoil: the authored impulses through the live shot and replay paths ---------
// These proofs intentionally live beside the prediction proofs. Presentation recoil
// is local state, but the difficult contract is ownership: gun_shoot creates the local
// VM edge, EV_FIRE creates the body edge, and rollback must reproduce (not duplicate)
// the former. None of the measurements below restates an impulse: shots enter through
// gun_shoot and the body-only check enters through the encoded EV_FIRE echo.
#define RECOIL_PROOF_TICKS 80
#define RECOIL_PROOF_SHOTS 3

typedef struct {
  float back, lift, side, muzzle_deg, bore_error, punch, punch_roll;
  float shot_yaw, shot_pitch;
  int ammo, idx;
  uint64_t rng;
} recoil_sample_t;

typedef struct {
  int tick;
  v3 dir;
  weapon_t wp;
  uint64_t rng;
} recoil_shot_t;

typedef struct {
  recoil_sample_t sample[RECOIL_PROOF_TICKS + 1];
  recoil_shot_t shot[RECOIL_PROOF_SHOTS];
  weapon_t end_wp;
  uint64_t end_rng;
  int nshot, last_shot_tick, continuous;
} recoil_trace_t;

typedef struct {
  float back, lift, side, muzzle, bore_error, punch, overshoot;
  int back_tick, lift_tick, settle_tick;
} recoil_metric_t;

// Both commands are deliberately transactional. This is more state than either case
// needs in isolation, but it means a proof can be placed in the middle of a --do list
// without donating its synthetic online roster, prediction ring, events or VFX pools
// to the command after it.
typedef struct {
  player_t players[MAX_HUMANS];
  anim_t player_anim[MAX_HUMANS];
  bot_t bots[MAX_BOTS];
  match_t match;
  net_client_t cl;
  cl_pred_t cl_pred[NET_HIST];
  uint64_t rng_gun[MAX_HUMANS];
  unsigned char humans_on[MAX_HUMANS];
  unsigned char cl_alive_prev[MAX_ENTS], cl_body[MAX_ENTS];
  uint8_t net_ping[MAX_ENTS];
  srv_ev_t srv_evlog[NET_EVLOG];
  event_t events[MAX_EVENTS];
  tracer_t tracers[MAX_TRACERS];
  flash_t flashes[MAX_FLASHES];
  particle_t parts[MAX_PARTICLES];
  pose_t fire_anchor_pose;
  long tick, ev_drops, muzzle_fire_tick;
  long fire_anchor_tick;
  int local_ent, online, predicting, prediction_replay, headless, score_held;
  int fire_anchor_src;
  int num_events, ev_peak, muzzle_t, muzzle_fresh, vfx_wpn;
  int srv_evlog_head, srv_evlog_n;
  uint32_t srv_ev_next;
  int cl_corrections, cl_ev_applied, cl_ev_dupes, world_dirty;
  float cl_worst_err_mm;
  v3 vm_muzzle, vm_mesh_muzzle, vm_mdl_o, vm_mdl_x, vm_mdl_y, vm_mdl_z;
  v3 vm_muz_cam;
  long vm_muz_tick;
  fx_build_witness_t fx_build_witness;
  vm_emit_witness_t vm_emit_witness;
  unsigned vm_render_order;
  const float *sim_mv;
  const float (*sim_wp)[WP_COUNT];
} recoil_save_t;

static recoil_save_t g_recoil_save;

static void recoil_save_globals(void) {
  memcpy(g_recoil_save.players, g_players, sizeof g_players);
  memcpy(g_recoil_save.player_anim, g_player_anim, sizeof g_player_anim);
  memcpy(g_recoil_save.bots, g_bots, sizeof g_bots);
  g_recoil_save.match = g_match;
  g_recoil_save.cl = g_cl;
  memcpy(g_recoil_save.cl_pred, g_cl_pred, sizeof g_cl_pred);
  memcpy(g_recoil_save.rng_gun, g_rng_gun, sizeof g_rng_gun);
  memcpy(g_recoil_save.humans_on, g_humans_on, sizeof g_humans_on);
  memcpy(g_recoil_save.cl_alive_prev, g_cl_alive_prev, sizeof g_cl_alive_prev);
  memcpy(g_recoil_save.cl_body, g_cl_body, sizeof g_cl_body);
  memcpy(g_recoil_save.net_ping, g_net_ping, sizeof g_net_ping);
  memcpy(g_recoil_save.srv_evlog, g_srv_evlog, sizeof g_srv_evlog);
  memcpy(g_recoil_save.events, g_events, sizeof g_events);
  memcpy(g_recoil_save.tracers, g_tracers, sizeof g_tracers);
  memcpy(g_recoil_save.flashes, g_flashes, sizeof g_flashes);
  memcpy(g_recoil_save.parts, g_parts, sizeof g_parts);
  g_recoil_save.fire_anchor_pose = g_fire_anchor_pose;
  g_recoil_save.tick = g_tick;
  g_recoil_save.ev_drops = g_ev_drops;
  g_recoil_save.muzzle_fire_tick = g_muzzle_fire_tick;
  g_recoil_save.fire_anchor_tick = g_fire_anchor_tick;
  g_recoil_save.fire_anchor_src = g_fire_anchor_src;
  g_recoil_save.local_ent = g_local_ent;
  g_recoil_save.online = g_online;
  g_recoil_save.predicting = g_predicting;
  g_recoil_save.prediction_replay = g_prediction_replay;
  g_recoil_save.headless = g_headless;
  g_recoil_save.score_held = g_score_held;
  g_recoil_save.num_events = g_num_events;
  g_recoil_save.ev_peak = g_ev_peak;
  g_recoil_save.muzzle_t = g_muzzle_t;
  g_recoil_save.muzzle_fresh = g_muzzle_fresh;
  g_recoil_save.vfx_wpn = g_vfx_wpn;
  g_recoil_save.srv_evlog_head = g_srv_evlog_head;
  g_recoil_save.srv_evlog_n = g_srv_evlog_n;
  g_recoil_save.srv_ev_next = g_srv_ev_next;
  g_recoil_save.cl_corrections = g_cl_corrections;
  g_recoil_save.cl_worst_err_mm = g_cl_worst_err_mm;
  g_recoil_save.cl_ev_applied = g_cl_ev_applied;
  g_recoil_save.cl_ev_dupes = g_cl_ev_dupes;
  g_recoil_save.world_dirty = g_world_dirty;
  g_recoil_save.vm_muzzle = g_vm_muzzle;
  g_recoil_save.vm_mesh_muzzle = g_vm_mesh_muzzle;
  g_recoil_save.vm_mdl_o = g_vm_mdl_o;
  g_recoil_save.vm_mdl_x = g_vm_mdl_x;
  g_recoil_save.vm_mdl_y = g_vm_mdl_y;
  g_recoil_save.vm_mdl_z = g_vm_mdl_z;
  g_recoil_save.vm_muz_cam = g_vm_muz_cam;
  g_recoil_save.vm_muz_tick = g_vm_muz_tick;
  g_recoil_save.fx_build_witness = g_fx_build_witness;
  g_recoil_save.vm_emit_witness = g_vm_emit_witness;
  g_recoil_save.vm_render_order = g_vm_render_order;
  g_recoil_save.sim_mv = g_sim_mv;
  g_recoil_save.sim_wp = g_sim_wp;
}

static void recoil_restore_globals(void) {
  memcpy(g_players, g_recoil_save.players, sizeof g_players);
  memcpy(g_player_anim, g_recoil_save.player_anim, sizeof g_player_anim);
  memcpy(g_bots, g_recoil_save.bots, sizeof g_bots);
  g_match = g_recoil_save.match;
  g_cl = g_recoil_save.cl;
  memcpy(g_cl_pred, g_recoil_save.cl_pred, sizeof g_cl_pred);
  memcpy(g_rng_gun, g_recoil_save.rng_gun, sizeof g_rng_gun);
  memcpy(g_humans_on, g_recoil_save.humans_on, sizeof g_humans_on);
  memcpy(g_cl_alive_prev, g_recoil_save.cl_alive_prev, sizeof g_cl_alive_prev);
  memcpy(g_cl_body, g_recoil_save.cl_body, sizeof g_cl_body);
  memcpy(g_net_ping, g_recoil_save.net_ping, sizeof g_net_ping);
  memcpy(g_srv_evlog, g_recoil_save.srv_evlog, sizeof g_srv_evlog);
  memcpy(g_events, g_recoil_save.events, sizeof g_events);
  memcpy(g_tracers, g_recoil_save.tracers, sizeof g_tracers);
  memcpy(g_flashes, g_recoil_save.flashes, sizeof g_flashes);
  memcpy(g_parts, g_recoil_save.parts, sizeof g_parts);
  g_fire_anchor_pose = g_recoil_save.fire_anchor_pose;
  g_tick = g_recoil_save.tick;
  g_ev_drops = g_recoil_save.ev_drops;
  g_muzzle_fire_tick = g_recoil_save.muzzle_fire_tick;
  g_fire_anchor_tick = g_recoil_save.fire_anchor_tick;
  g_fire_anchor_src = g_recoil_save.fire_anchor_src;
  g_local_ent = g_recoil_save.local_ent;
  g_online = g_recoil_save.online;
  g_predicting = g_recoil_save.predicting;
  g_prediction_replay = g_recoil_save.prediction_replay;
  g_headless = g_recoil_save.headless;
  g_score_held = g_recoil_save.score_held;
  g_num_events = g_recoil_save.num_events;
  g_ev_peak = g_recoil_save.ev_peak;
  g_muzzle_t = g_recoil_save.muzzle_t;
  g_muzzle_fresh = g_recoil_save.muzzle_fresh;
  g_vfx_wpn = g_recoil_save.vfx_wpn;
  g_srv_evlog_head = g_recoil_save.srv_evlog_head;
  g_srv_evlog_n = g_recoil_save.srv_evlog_n;
  g_srv_ev_next = g_recoil_save.srv_ev_next;
  g_cl_corrections = g_recoil_save.cl_corrections;
  g_cl_worst_err_mm = g_recoil_save.cl_worst_err_mm;
  g_cl_ev_applied = g_recoil_save.cl_ev_applied;
  g_cl_ev_dupes = g_recoil_save.cl_ev_dupes;
  g_world_dirty = g_recoil_save.world_dirty;
  g_vm_muzzle = g_recoil_save.vm_muzzle;
  g_vm_mesh_muzzle = g_recoil_save.vm_mesh_muzzle;
  g_vm_mdl_o = g_recoil_save.vm_mdl_o;
  g_vm_mdl_x = g_recoil_save.vm_mdl_x;
  g_vm_mdl_y = g_recoil_save.vm_mdl_y;
  g_vm_mdl_z = g_recoil_save.vm_mdl_z;
  g_vm_muz_cam = g_recoil_save.vm_muz_cam;
  g_vm_muz_tick = g_recoil_save.vm_muz_tick;
  g_fx_build_witness = g_recoil_save.fx_build_witness;
  g_vm_emit_witness = g_recoil_save.vm_emit_witness;
  g_vm_render_order = g_recoil_save.vm_render_order;
  g_sim_mv = g_recoil_save.sim_mv;
  g_sim_wp = g_recoil_save.sim_wp;
}

static void recoil_stage(int local_presentation, int wpn, int ads) {
  memset(g_players, 0, sizeof g_players);
  memset(g_player_anim, 0, sizeof g_player_anim);
  memset(g_bots, 0, sizeof g_bots);
  memset(g_humans_on, 0, sizeof g_humans_on);
  memset(&g_match, 0, sizeof g_match);
  memset(g_events, 0, sizeof g_events);
  memset(g_tracers, 0, sizeof g_tracers);
  memset(g_flashes, 0, sizeof g_flashes);
  memset(g_parts, 0, sizeof g_parts);
  g_num_events = g_ev_peak = 0;
  g_ev_drops = 0;
  g_muzzle_t = g_muzzle_fresh = 0;
  g_muzzle_fire_tick = -1000;
  g_tick = 1000;
  g_online = 0;
  g_predicting = 0;
  g_prediction_replay = 0;
  g_headless = 0;                 // measurements, not the ordinary `bang` trace
  g_local_ent = local_presentation ? 0 : -1;
  g_score_held = 0;
  g_sim_mv = g_cfg.mv;
  g_sim_wp = g_cfg.wp;
  g_humans_on[0] = 1;
  player_t *p = &g_players[0];
  player_spawn(p);
  // Above every generated solid: the proof needs a real hitscan and EV_FIRE, but no
  // target or impact is allowed to make its result depend on the current arena seed.
  p->pos = p->prev_pos = (v3){{0.0f, 20.0f, 0.0f}};
  p->vel = (v3){{0.0f, 0.0f, 0.0f}};
  p->grounded = p->prev_grounded = 0;
  p->yaw = p->pitch = 0.0f;
  p->wp.cur = wpn;
  p->wp.ads_lin = p->wp.ads_s = p->wp.prev_ads_s = ads ? 1.0f : 0.0f;
  p->wp.ads_want = (unsigned char)ads;
  p->vm.last_eye = p->eye;
  anim_reset(&g_player_anim[0], p->pos, p->yaw, p->wp.cur);
  g_rng_gun[0] = rng_gun_seed(1337u, 0);
}

// Angular change of the authoritative barrel from calm aim. The viewmodel is aligned
// to this same axis, so the gameplay state is the stable, render-independent measure.
static float recoil_muzzle_delta(const player_t *p) {
  return gun_spread(&p->wp, p->vel, p->grounded);
}

static void recoil_take_sample(recoil_trace_t *tr, int tick, const v3 *last_dir) {
  const player_t *p = &g_players[0];
  recoil_sample_t *o = &tr->sample[tick];
  o->back = p->vm.kick_back;
  o->lift = p->vm.kick_lift;
  o->side = p->vm.kick_side;
  o->muzzle_deg = recoil_muzzle_delta(p);
  {
    float by, bp;
    gun_bore_angles(&p->wp, p->yaw, p->pitch, &by, &bp);
    v3 fwd = {{cosf(bp) * sinf(by), sinf(bp), -cosf(bp) * cosf(by)}};
    v3 rgt = v3_norm(v3_cross(fwd, (v3){{0, 1, 0}}));
    v3 upv = v3_norm(v3_cross(rgt, fwd));
    vm_pose_t vp = vm_stage_pose(p, &p->vm, &p->wp, 1.0f, 1.0f);
    vm_frame_t vf = vm_stage_springs(&vp, &p->vm, 1.0f,
                                      (v3){{0, 0, 0}}, rgt, upv, fwd);
    anim_t body = g_player_anim[0];
    skel_solve(&body, p->pos, by, bp, p->wp.cur,
               GUN_POSE_BLOCKED, SKEL_POSE_ONLY);
    // Sine-based, never acos-of-dot: near zero the dot saturates at 1.0f and a
    // single ULP of normalization noise already reads as acosf(1 - 2^-24) =
    // 0.0198 deg — twenty times the 0.001 deg contract, from vectors that are
    // bit-exact in direction. |cross| IS the small angle, at ~1e-7 rad noise,
    // so the strict bound below stays honest in float.
    v3 vz = v3_norm(vf.gz), bz = v3_norm(body.pose.gun_f);
    float vm_err = atan2f(v3_len(v3_cross(vz, fwd)), v3_dot(vz, fwd));
    float body_err = atan2f(v3_len(v3_cross(bz, fwd)), v3_dot(bz, fwd));
    o->bore_error = fmaxf(vm_err, body_err) / DEG2RAD;
  }
  o->punch = p->wp.punch;
  o->punch_roll = p->wp.punch_roll;
  if (last_dir) {
    o->shot_yaw = atan2f(last_dir->x, -last_dir->z) / DEG2RAD;
    o->shot_pitch = asinf(f_clamp(last_dir->y, -1.0f, 1.0f)) / DEG2RAD;
  }
  o->ammo = p->wp.ammo[p->wp.cur];
  o->idx = p->wp.idx;
  o->rng = g_rng_gun[0];
}

// One real presentation tick for the staged player's third-person body, exactly as
// player_present_tick drives it. recoil_run and gunwall_proof both run the live
// anim alongside the weapon loop; one copy keeps their idea of "the real path"
// from drifting apart.
static void recoil_present_body(player_t *p) {
  p->wp.ready_want = (unsigned char)player_carry_ready(p);
  int idle_ok = gun_pose_state(&p->wp, &g_player_anim[0], p->vel,
                               p->grounded, p->sliding);
  weapon_anim_tick(&g_player_anim[0], p->pos, player_eye_pos(p), p->vel,
                   p->yaw, p->pitch, p->grounded, p->crouched && !p->sliding,
                   p->tuck_y, p->sliding, p->wp.ads_s, p->lean,
                   p->wp.ready_want, 0, &p->wp, idle_ok);
}

static void recoil_fire(recoil_trace_t *tr, int instant_tick) {
  player_t *p = &g_players[0];
  float back = p->vm.kick_back, lift = p->vm.kick_lift, side = p->vm.kick_side;
  int ev0 = g_num_events;
  gun_shoot(0, &p->wp, player_eye_pos(p), p->yaw, p->pitch,
            p->vel, p->grounded, &g_rng_gun[0]);
  // Production impulses add velocity only. Equality, rather than an epsilon, makes an
  // accidental direct-value write or burst reset a hard failure at the event tick.
  if (p->vm.kick_back != back || p->vm.kick_lift != lift || p->vm.kick_side != side)
    tr->continuous = 0;
  event_t *fire = NULL;
  for (int i = ev0; i < g_num_events; i++)
    if (g_events[i].type == EV_FIRE && g_events[i].src == 0) fire = &g_events[i];
  if (!fire || tr->nshot >= RECOIL_PROOF_SHOTS) {
    tr->continuous = 0;
    return;
  }
  recoil_shot_t *s = &tr->shot[tr->nshot++];
  s->tick = instant_tick;
  s->dir = fire->b;
  s->wp = p->wp;
  s->rng = g_rng_gun[0];
  tr->last_shot_tick = instant_tick;
}

static recoil_trace_t recoil_run(int wpn, int ads, int burst,
                                 int local_presentation) {
  recoil_trace_t tr;
  memset(&tr, 0, sizeof tr);
  tr.continuous = 1;
  recoil_stage(local_presentation, wpn, ads);
  player_t *p = &g_players[0];
  recoil_fire(&tr, 0);
  recoil_take_sample(&tr, 0, tr.nshot ? &tr.shot[tr.nshot - 1].dir : NULL);

  for (int t = 1; t <= RECOIL_PROOF_TICKS; t++) {
    int held = 1;                     // remainder of the initial shot tick
    if (t > 1) {
      p->wp.prev_rec_p = p->wp.rec_p;
      p->wp.prev_rec_y = p->wp.rec_y;
      p->wp.prev_punch = p->wp.punch;
      p->wp.prev_punch_roll = p->wp.punch_roll;
      p->wp.prev_ads_s = p->wp.ads_s;
      p->wp.prev_switch = (float)p->wp.switch_t / GUN_SWITCH_TICKS;
      gun_cool(&p->wp);
      held = burst && tr.nshot < RECOIL_PROOF_SHOTS;
      if (held && p->wp.cool[p->wp.cur] == 0)
        recoil_fire(&tr, t - 1);       // impulse is before this tick's integration
    }
    gun_recover(&p->wp, held);
    g_tick++;
    vm_tick(p);
    // The third-person body runs the REAL presentation path alongside, so the bore
    // gate in recoil_take_sample judges the solved pose the arena would render
    // mid-burst. A weapon-only loop left the body springs at zero and was blind to
    // any anim-state term bending the bore (that blindness hid a presentation
    // spring deflecting round 2..n of a burst).
    recoil_present_body(p);
    recoil_take_sample(&tr, t, tr.nshot ? &tr.shot[tr.nshot - 1].dir : NULL);
  }
  tr.end_wp = p->wp;
  tr.end_rng = g_rng_gun[0];
  return tr;
}

static recoil_metric_t recoil_metrics(const recoil_trace_t *tr) {
  recoil_metric_t m = {0};
  for (int t = 0; t <= RECOIL_PROOF_TICKS; t++) {
    const recoil_sample_t *s = &tr->sample[t];
    if (s->back > m.back) { m.back = s->back; m.back_tick = t; }
    if (s->lift > m.lift) { m.lift = s->lift; m.lift_tick = t; }
    if (fabsf(s->side) > m.side) m.side = fabsf(s->side);
    if (s->muzzle_deg > m.muzzle) m.muzzle = s->muzzle_deg;
    if (s->bore_error > m.bore_error) m.bore_error = s->bore_error;
    if (fabsf(s->punch) > m.punch) m.punch = fabsf(s->punch);
  }
  float over_back = 0.0f, over_lift = 0.0f;
  for (int t = m.back_tick + 1; t <= RECOIL_PROOF_TICKS; t++)
    if (-tr->sample[t].back > over_back) over_back = -tr->sample[t].back;
  for (int t = m.lift_tick + 1; t <= RECOIL_PROOF_TICKS; t++)
    if (-tr->sample[t].lift > over_lift) over_lift = -tr->sample[t].lift;
  m.overshoot = fmaxf(over_back / fmaxf(m.back, 1e-9f),
                      over_lift / fmaxf(m.lift, 1e-9f));
  // Settle is the first four-tick window inside 2% on every physical channel.
  // Four samples reject a zero crossing; 2% is where the authored springs land in
  // the plan's 120--220 ms window.
  for (int t = tr->last_shot_tick + 1; t <= RECOIL_PROOF_TICKS - 3; t++) {
    int quiet = 1;
    for (int j = 0; j < 4; j++) {
      const recoil_sample_t *s = &tr->sample[t + j];
      if (fabsf(s->back) > m.back * 0.02f + 1e-7f ||
          fabsf(s->lift) > m.lift * 0.02f + 1e-7f ||
          fabsf(s->side) > m.side * 0.02f + 1e-7f ||
          s->muzzle_deg > m.muzzle * 0.02f + 0.002f) quiet = 0;
    }
    if (quiet) { m.settle_tick = t; break; }
  }
  return m;
}

static int recoil_gameplay_same(const recoil_trace_t *a, const recoil_trace_t *b) {
  return a->nshot == b->nshot &&
         !memcmp(a->shot, b->shot, sizeof a->shot) &&
         !memcmp(&a->end_wp, &b->end_wp, sizeof a->end_wp) &&
         a->end_rng == b->end_rng;
}

static void recoil_print_samples(const recoil_trace_t *tr, const recoil_metric_t *m,
                                 int wpn, int ads, int burst) {
  static const int T[] = {0, 1, 2, 4, 8, 16};
  const char *wn = wpn == WPN_SR ? "sr" : "ar";
  const char *an = ads ? "ads" : "hip";
  const char *bn = burst ? "burst" : "single";
  for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
    const recoil_sample_t *s = &tr->sample[T[i]];
    printf("recoil %s %s %s T%d back=%.2fmm lift=%.2fdeg side=%+.2fmm "
           "muzzle=%.2fdeg punch=(%.2f %+.2f) shot=(%+.3f %+.3f)\n",
           wn, an, bn, T[i], (double)(s->back * 1000.0f),
           (double)(s->lift / DEG2RAD), (double)(s->side * 1000.0f),
           (double)s->muzzle_deg, (double)s->punch, (double)s->punch_roll,
           (double)s->shot_yaw, (double)s->shot_pitch);
  }
  int st = m->settle_tick;
  const recoil_sample_t *s = &tr->sample[st > 0 ? st : RECOIL_PROOF_TICKS];
  printf("recoil %s %s %s Settle=T%d(+%.1fms) back=%.3fmm lift=%.3fdeg "
         "side=%+.3fmm muzzle=%.3fdeg punch=%.3f\n",
         wn, an, bn, st, (double)((st - tr->last_shot_tick) * 1000.0f * TICK_DT),
         (double)(s->back * 1000.0f), (double)(s->lift / DEG2RAD),
         (double)(s->side * 1000.0f), (double)s->muzzle_deg, (double)s->punch);
}

static void recoil_proof(void) {
  recoil_save_globals();
  int ok = 1;
  recoil_metric_t single[WPN_COUNT][2];
  memset(single, 0, sizeof single);
  static recoil_trace_t a, b, ref;
  for (int wi = 0; wi < WPN_COUNT; wi++) for (int ads = 0; ads <= 1; ads++) {
    for (int burst = 0; burst <= 1; burst++) {
      a = recoil_run(wi, ads, burst, 1);
      b = recoil_run(wi, ads, burst, 1);
      ref = recoil_run(wi, ads, burst, 0);  // same shot path, VM impulse disabled
      recoil_metric_t m = recoil_metrics(&a);
      recoil_print_samples(&a, &m, wi, ads, burst);
      int deterministic = !memcmp(&a, &b, sizeof a);
      int gameplay = recoil_gameplay_same(&a, &ref);
      int bore_exact = a.nshot > 0 && fabsf(a.shot[0].wp.rec_p) < 1e-7f &&
                       fabsf(a.shot[0].wp.rec_y) < 1e-7f;
      int rng_static = a.nshot > 0 && a.end_rng == a.shot[0].rng;
      for (int si = 0; si < a.nshot; si++) {
        float sy = atan2f(a.shot[si].dir.x, -a.shot[si].dir.z) / DEG2RAD;
        float sp = asinf(f_clamp(a.shot[si].dir.y, -1.0f, 1.0f)) / DEG2RAD;
        if (fabsf(sy - a.shot[si].wp.rec_y) > 0.002f ||
            fabsf(sp - a.shot[si].wp.rec_p) > 0.002f)
          bore_exact = 0;
      }
      int shape = a.continuous && a.nshot == (burst ? RECOIL_PROOF_SHOTS : 1) &&
                  bore_exact && rng_static && m.settle_tick &&
                  m.settle_tick - a.last_shot_tick >= 14 &&
                  m.settle_tick - a.last_shot_tick <= 58 && m.overshoot < 0.20f &&
                  m.side > 0.0002f &&
                  m.side <= (wi == WPN_SR ? 0.0041f : 0.0031f) &&
                  m.bore_error < 0.001f &&
                  m.muzzle > (wi == WPN_SR ? 0.8f : 0.12f) &&
                  m.muzzle < (wi == WPN_SR ? 12.5f : 2.0f);
      if (!burst) {
        static const float BACK_LO[WPN_COUNT][2] = {
          [WPN_AR] = {0.009f, 0.007f}, [WPN_SR] = {0.018f, 0.010f},
        };
        static const float BACK_HI[WPN_COUNT][2] = {
          [WPN_AR] = {0.014f, 0.010f}, [WPN_SR] = {0.024f, 0.016f},
        };
        shape &= m.back_tick >= 1 && m.back_tick <= 3 &&
                 m.back >= BACK_LO[wi][ads] && m.back <= BACK_HI[wi][ads] &&
                 fabsf(m.lift) < 1e-7f;
        single[wi][ads] = m;
      } else {
        const recoil_metric_t *one = &single[wi][ads];
        // At the real cooldown a later impulse can land in the previous spring's
        // return phase, so the absolute back peak need not be the final shot's peak.
        // What must remain true is continuity, compression before lift, and a bounded
        // accumulated response rather than a reset or an unbounded stack.
        shape &= m.back > one->back * 0.95f && m.back < one->back * 3.1f &&
                 m.muzzle > one->muzzle * 1.05f && m.muzzle < one->muzzle * 5.0f;
      }
      int case_ok = deterministic && gameplay && shape;
      printf("recoil %s %s %s peak back=%.2fmm@T%d lift=%.2fdeg@T%d "
             "side=%.2fmm muzzle=%.2fdeg bore_err=%.4fdeg punch=%.2f overshoot=%.1f%% "
             "shots=%d continuous=%d deterministic=%d gameplay_rng=%d %s\n",
             wi == WPN_SR ? "sr" : "ar", ads ? "ads" : "hip",
             burst ? "burst" : "single", (double)(m.back * 1000.0f), m.back_tick,
             (double)(m.lift / DEG2RAD), m.lift_tick, (double)(m.side * 1000.0f),
             (double)m.muzzle, (double)m.bore_error, (double)m.punch,
             (double)(m.overshoot * 100.0f),
             a.nshot, a.continuous, deterministic, gameplay,
             case_ok ? "ok" : "FAIL");
      ok &= case_ok;
    }
  }
  recoil_restore_globals();
  printf("recoil %s\n", ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// --- The wall gate ------------------------------------------------------------------
// A shot must never leave a barrel the world owns: gun_shoot refuses when the
// eye->reach bore segment meets material no round can cross, the carriage tucks
// (gunwall_s), and the refusal costs no state. Three impenetrable standoffs per
// weapon prove the reach is per-weapon (0.95 m sits between the AR's and the SR's),
// that a refused trigger moves neither ammo, cooldown, index nor the event channel,
// that a clear muzzle still fires, and that the tuck actually depresses the rendered
// bore. The fourth case is the SAME wall at 0.50 m made penetrable: the gate must
// stand aside — PEN_PATH owns thin cover, and the point-blank wallbang is a designed
// mechanic this gate once silently deleted. The synthetic wall rides in the scratch
// slot past the generated count; both the count AND the slot's contents are
// restored (at MAX_SOLIDS the clamp overwrites a real solid, and restoring only the
// count would leave the synthetic wall in the live arena for the rest of the run).
static void gunwall_proof(void) {
  recoil_save_globals();
  int ok = 1;
  int save_solids = g_num_solids;
  int slot = save_solids >= MAX_SOLIDS ? MAX_SOLIDS - 1 : save_solids;
  solid_t save_slot = g_solids[slot];
  for (int wi = 0; wi < WPN_COUNT; wi++) {
    static const float D[4] = {0.50f, 0.95f, 2.00f, 0.50f};
    static const int PEN[4] = {0, 0, 0, 1};
    for (int di = 0; di < 4; di++) {
      recoil_stage(1, wi, 0);
      player_t *p = &g_players[0];
      v3 eye = player_eye_pos(p);
      // A wall square to the bore (yaw 0 aims along -z), full height around the eye.
      g_num_solids = slot;
      g_solids[g_num_solids++] = (solid_t){
        .min = {{-3.0f, eye.y - 3.0f, -D[di] - 0.4f}},
        .max = {{3.0f, eye.y + 3.0f, -D[di]}},
        .pen = PEN[di]};
      int want_block = !PEN[di] && D[di] < gun_wall_reach(wi);
      int blocked = gun_wall_blocked(eye, p->yaw, p->pitch, &p->wp);
      int ammo0 = p->wp.ammo[wi], ev0 = g_num_events;
      uint64_t rng0 = g_rng_gun[0];
      gun_shoot(0, &p->wp, eye, p->yaw, p->pitch, p->vel, p->grounded,
                &g_rng_gun[0]);
      int fired = p->wp.ammo[wi] != ammo0;
      int evs = g_num_events - ev0;
      int silent = p->wp.ammo[wi] == ammo0 && p->wp.cool[wi] == 0 &&
                   p->wp.idx == 0 && evs == 0 && g_rng_gun[0] == rng0 &&
                   p->wp.rec_p == 0.0f && p->wp.rec_y == 0.0f;
      // The tuck: run the real presentation path and read the carriage. A blocked
      // rifle must settle tucked with its rendered bore depressed; a clear one must
      // stay level on the aim axis.
      for (int t = 0; t < 90; t++) {
        g_tick++;
        recoil_present_body(p);
      }
      float tuck = g_player_anim[0].gunwall_s;
      float bore_y = g_player_anim[0].pose.gun_f.y;
      int case_ok = blocked == want_block &&
                    (blocked ? !fired && silent && tuck > 0.80f && bore_y < -0.25f
                             : fired && evs > 0 && tuck < 0.02f &&
                               fabsf(bore_y) < 0.05f);
      printf("gunwall %s d=%.2f pen=%d reach=%.2f blocked=%d fired=%d silent=%d "
             "tuck=%.2f bore_y=%+.2f %s\n",
             wi == WPN_SR ? "sr" : "ar", (double)D[di], PEN[di],
             (double)gun_wall_reach(wi), blocked, fired, silent, (double)tuck,
             (double)bore_y, case_ok ? "ok" : "FAIL");
      ok &= case_ok;
    }
  }
  g_solids[slot] = save_slot;
  g_num_solids = save_solids;
  recoil_restore_globals();
  printf("gunwall %s\n", ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// --- The rendered SR idle/action gate -----------------------------------------
// This is deliberately a RENDER diagnostic, not another authored pose. It calls the
// production ready gate and vm_build, then reads back the frame, the emitted stock end
// and both published wrists. Alpha=0 is in the matrix because that is the exact frame
// on which interpolation used to resurrect the previous low-ready pose.
typedef struct {
  v3 muzzle;
  float ready, pitch, yaw, roll;
  float stock_eye, wrist_eye[2], stock_wrist, mesh_plan;
  int stock_found, finite;
} vmframe_diag_t;

static vmframe_diag_t vmframe_emit_basis(float ready, v3 eye,
                                         v3 rgt, v3 upv, v3 fwd,
                                         const vm_build_plan_t *plan) {
  vmframe_diag_t d = {.ready = ready, .stock_eye = 1e9f,
                      .stock_wrist = 1e9f};
  g_nprobe = 0;
  g_probe_on = 1;
  vm_build_prepared(eye, rgt, upv, fwd, plan);
  probe(NULL);
  g_probe_on = 0;
  d.muzzle = g_vm_muzzle;
  d.mesh_plan = v3_len(v3_sub(g_vm_mesh_muzzle, plan->muzzle));

  // Signed Tait-Bryan diagnostics relative to the camera. Roll is measured around
  // the weapon's own forward axis against the zero-roll right vector at that aim.
  v3 gz = v3_norm(g_vm_mdl_z), gx = v3_norm(g_vm_mdl_x);
  float fr = v3_dot(gz, rgt), fu = v3_dot(gz, upv), ff = v3_dot(gz, fwd);
  d.pitch = atan2f(fu, sqrtf(fr * fr + ff * ff)) / DEG2RAD;
  d.yaw = atan2f(fr, ff) / DEG2RAD;
  v3 level_r = v3_cross(gz, upv);
  if (v3_dot(level_r, level_r) > 1e-8f) level_r = v3_norm(level_r);
  else level_r = rgt;
  d.roll = atan2f(v3_dot(v3_cross(level_r, gx), gz),
                  f_clamp(v3_dot(level_r, gx), -1.0f, 1.0f)) / DEG2RAD;

  // The back-most vertex of the actual emitted vm_gun range is the stock end. This
  // avoids a copied butt constant and follows future stock geometry automatically.
  float back = 1e9f;
  v3 stock = g_vm_mdl_o;
  for (int i = 0; i < g_nprobe; i++) {
    if (!g_probe[i].name || strcmp(g_probe[i].name, "vm_gun")) continue;
    for (int f = g_probe[i].v0; f + VERT_FLOATS <= g_probe[i].v1;
         f += VERT_FLOATS) {
      v3 q = {{g_scene[f], g_scene[f + 1], g_scene[f + 2]}};
      float z = v3_dot(v3_sub(q, g_vm_mdl_o), gz);
      if (z < back) { back = z; stock = q; d.stock_found = 1; }
    }
  }
  if (d.stock_found) {
    d.stock_eye = v3_len(v3_sub(stock, eye));
    d.stock_wrist = v3_len(v3_sub(stock, g_vm_hand_p[0]));
  }
  for (int h = 0; h < 2; h++) d.wrist_eye[h] = v3_len(v3_sub(g_vm_hand_p[h], eye));
  float values[] = {d.ready, d.pitch, d.yaw, d.roll, d.stock_eye, d.mesh_plan,
                    d.wrist_eye[0], d.wrist_eye[1], d.stock_wrist,
                    d.muzzle.x, d.muzzle.y, d.muzzle.z};
  d.finite = 1;
  for (unsigned i = 0; i < sizeof values / sizeof values[0]; i++)
    if (f_is_nonfinite(values[i])) d.finite = 0;
  return d;
}

static vmframe_diag_t vmframe_build_basis(const player_t *p, float alpha, float ready,
                                          v3 eye, v3 rgt, v3 upv, v3 fwd) {
  vm_build_plan_t plan = vm_plan_make(p, alpha, eye, rgt, upv, fwd, ready);
  return vmframe_emit_basis(ready, eye, rgt, upv, fwd, &plan);
}

static vmframe_diag_t vmframe_build(const player_t *p, float alpha, float ready) {
  static const v3 eye = {{0, 0, 0}}, rgt = {{1, 0, 0}};
  static const v3 upv = {{0, 1, 0}}, fwd = {{0, 0, -1}};
  return vmframe_build_basis(p, alpha, ready, eye, rgt, upv, fwd);
}

static float vmframe_pose_delta(const pose_t *a, const pose_t *b) {
  float d = v3_len(v3_sub(a->gun_o, b->gun_o));
  d = fmaxf(d, v3_len(v3_sub(a->gun_f, b->gun_f)));
  d = fmaxf(d, v3_len(v3_sub(a->wri[0], b->wri[0])));
  d = fmaxf(d, v3_len(v3_sub(a->wri[1], b->wri[1])));
  return d;
}

typedef struct {
  float down, cross;
  int finite;
} vmframe_3p_axis_t;

// Read the signed low-ready angles back from the solved world-space bore. The
// production descriptor remains the sole source of their targets; this inverse only
// expresses the result relative to the entity's aim and world up.
static vmframe_3p_axis_t vmframe_3p_axis(v3 aim, v3 gun) {
  const v3 up = {{0, 1, 0}};
  vmframe_3p_axis_t d = {0};
  aim = v3_norm(aim);
  gun = v3_norm(gun);
  float aim_up = f_clamp(v3_dot(aim, up), -1.0f, 1.0f);
  float gun_up = f_clamp(v3_dot(gun, up), -1.0f, 1.0f);
  v3 aim_h = v3_sub(aim, v3_scale(up, aim_up));
  v3 gun_h = v3_sub(gun, v3_scale(up, gun_up));
  float aim_h2 = v3_dot(aim_h, aim_h), gun_h2 = v3_dot(gun_h, gun_h);
  if (aim_h2 > 1e-8f && gun_h2 > 1e-8f) {
    aim_h = v3_scale(aim_h, 1.0f / sqrtf(aim_h2));
    gun_h = v3_scale(gun_h, 1.0f / sqrtf(gun_h2));
    d.down = (asinf(aim_up) - asinf(gun_up)) / DEG2RAD;
    d.cross = atan2f(v3_dot(v3_cross(aim_h, gun_h), up),
                     f_clamp(v3_dot(aim_h, gun_h), -1.0f, 1.0f)) / DEG2RAD;
    d.finite = !f_is_nonfinite(d.down) && !f_is_nonfinite(d.cross);
  }
  return d;
}

enum {
  VMF_IDLE, VMF_READY, VMF_ADS_T0, VMF_ADS, VMF_RELOAD,
  VMF_BOLT, VMF_SWAP, VMF_SLIDE, VMF_FIRE, VMF_NCASE
};

static const char *const VMF_NAME[VMF_NCASE] = {
  "idle", "ready", "ads-t0", "ads", "reload",
  "bolt", "swap", "slide", "fire"
};

static void vmframe_stage_case(player_t *p, anim_t *a, int which) {
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0, 0, 0}};
  p->vel = (v3){{0, 0, 0}};
  p->grounded = p->prev_grounded = 1;
  p->wp.cur = WPN_SR;
  p->wp.reload_t = p->wp.switch_t = 0;
  p->wp.ads_want = p->wp.ready_want = 0;
  p->wp.ads_lin = p->wp.ads_s = p->wp.prev_ads_s = 0.0f;
  memset(p->wp.cool, 0, sizeof p->wp.cool);
  memset(&p->vm, 0, sizeof p->vm);
  anim_reset(a, p->pos, 0.0f, WPN_SR);
  a->ready_s = a->prev_ready_s = 0.0f;
  a->fire_k = 0.0f;
  switch (which) {
  case VMF_READY:  p->wp.ready_want = 1; break;
  case VMF_ADS_T0: p->wp.ads_want = 1; break;
  case VMF_ADS:
    p->wp.ads_want = 1;
    p->wp.ads_lin = p->wp.ads_s = p->wp.prev_ads_s = 1.0f;
    break;
  case VMF_RELOAD:
    p->wp.reload_t = WPN_DEF[WPN_SR].reload_ticks / 2;
    break;
  case VMF_BOLT:
    p->wp.cool[WPN_SR] = 7;
    p->wp.ready_want = 1;
    break;
  case VMF_SWAP:
    p->wp.switch_t = GUN_SWITCH_TICKS / 2;
    p->wp.prev_switch = 0.5f;
    break;
  case VMF_SLIDE:
    p->sliding = 1;
    p->vm.slide_s = p->vm.prev_slide_s = 1.0f;
    a->slide_s = a->slide_lin = 1.0f;
    break;
  case VMF_FIRE:   a->fire_k = 1.0f; break;
  default: break;
  }
}

// Advance the staged SR through the production carry follower. This deliberately
// leaves the physical player fixed: ready_s is presentation, and its transition must
// not need a movement tick or a second renderer-side clock.
static void vmframe_ready_tick(player_t *p, anim_t *a) {
  int idle_ok = gun_pose_state(&p->wp, a, p->vel, p->grounded, p->sliding);
  g_tick++;
  anim_tick(a, p->pos, p->vel, p->yaw, p->pitch, p->grounded,
            p->crouched && !p->sliding, p->tuck_y, p->sliding, p->wp.ads_s,
            p->lean, p->wp.ready_want, 0, WPN_SR, idle_ok);
}

typedef struct {
  vmframe_diag_t t0, t1;
  v3 pose0, pose1, anchor0, tracer, fx0, fx1;
  v3 pub_cam0, pub_cam1;
  float remap0, remap1, publish0, publish1;
  float vm_depth0, fx_depth0, vm_depth1, fx_depth1;
  float latch, shot_axis, ballistic_anchor;
  long pub_tick0, pub_tick1;
  int fire_events, all_events, map0_ok, map1_ok, stale_rejected;
} vmframe_fire_t;

// The FP muzzle and pose_muzzle belong to different rigs and intentionally sit at
// different depths. The production contract is instead exact projection registration:
// rf_glow_pass keeps VM depth and remaps its lateral camera coordinates so the world
// tracer begins on the pixel occupied by the drawn barrel. Exercise the SAME
// prepare/publish/consume production sequence, emit that already-prepared plan once,
// then independently project/unproject at the retained depth. `remap_err` is the
// resulting world-space miss, not a false FP-vs-3P position comparison.
static int vmframe_render_order(const player_t *p, float alpha,
                                vmframe_diag_t *diag, v3 *mapped,
                                v3 *published_cam, long *publish_tick,
                                float *publish_err, float *remap_err,
                                float *vm_depth, float *fx_depth) {
  frame_cam_t c = rf_camera(p, alpha, NULL);
  rf_vm_frame_t vmf;
  int live = rf_vm_publish_anchor(p, alpha, &c, &vmf, mapped);
  if (!vmf.active) return 0;

  v3 rel = v3_sub(vmf.plan.muzzle, c.eye);
  v3 expect_cam = {{v3_dot(rel, vmf.rgt), v3_dot(rel, vmf.upv),
                    v3_dot(rel, vmf.fwd)}};
  *published_cam = g_vm_muz_cam;
  *publish_tick = g_vm_muz_tick;
  *publish_err = v3_len(v3_sub(*published_cam, expect_cam));
  *vm_depth = expect_cam.z;
  *fx_depth = 0.0f;
  *remap_err = 1e9f;
  *diag = vmframe_emit_basis(vmf.plan.pose.ready, c.eye, vmf.rgt,
                            vmf.upv, vmf.fwd, &vmf.plan);
  if (!live || !(expect_cam.z > 0.01f) || f_is_nonfinite(expect_cam.x) ||
      f_is_nonfinite(expect_cam.y) || f_is_nonfinite(expect_cam.z)) {
    *mapped = c.eye;
    return 0;
  }
  v3 fx_rel = v3_sub(*mapped, c.eye);
  v3 fx_cam = {{v3_dot(fx_rel, vmf.rgt), v3_dot(fx_rel, vmf.upv),
                v3_dot(fx_rel, vmf.fwd)}};
  *fx_depth = fx_cam.z;

  // Independent projection equality: convert the VM normalized coordinates back
  // through the world lens at the SAME depth. Aspect cancels because both passes use
  // the same viewport; the vertical-FOV tangent applies to both camera axes.
  float vm_tan = tanf(VM_FOV_DEG * DEG2RAD * 0.5f);
  float world_tan = tanf(c.fovy * 0.5f);
  v3 expect = {{expect_cam.x / (expect_cam.z * vm_tan) *
                               expect_cam.z * world_tan,
                expect_cam.y / (expect_cam.z * vm_tan) *
                               expect_cam.z * world_tan,
                expect_cam.z}};
  *remap_err = v3_len(v3_sub(fx_cam, expect));
  return !f_is_nonfinite(mapped->x) && !f_is_nonfinite(mapped->y) &&
         !f_is_nonfinite(mapped->z) && !f_is_nonfinite(*remap_err) &&
         !f_is_nonfinite(*publish_err) && !f_is_nonfinite(*fx_depth);
}

static vmframe_fire_t vmframe_fire_case(int predicting) {
  recoil_stage(1, WPN_SR, 0);
  player_t *p = &g_players[0];
  anim_t *a = &g_player_anim[0];
  p->grounded = p->prev_grounded = 1;
  p->vel = (v3){{0, 0, 0}};
  p->wp.ads_want = p->wp.ready_want = 0;
  memset(p->wp.cool, 0, sizeof p->wp.cool);
  anim_reset(a, p->pos, p->yaw, WPN_SR);
  a->ready_s = a->prev_ready_s = 0.0f;
  skel_solve(a, p->pos, p->yaw, p->pitch, WPN_SR, 1, SKEL_POSE_ONLY);
  g_predicting = predicting;
  int ev0 = g_num_events;
  gun_shoot(0, &p->wp, player_eye_pos(p), p->yaw, p->pitch,
            p->vel, p->grounded, &g_rng_gun[0]);

  vmframe_fire_t out = {0};
  out.latch = a->fire_k;
  out.pose0 = pose_muzzle(&a->pose, WPN_SR);
  if (!predicting && g_fire_anchor_src == 0 && g_fire_anchor_tick == g_tick)
    out.anchor0 = pose_muzzle(&g_fire_anchor_pose, WPN_SR);
  for (int i = ev0; i < g_num_events; i++)
    if (g_events[i].type == EV_FIRE && g_events[i].src == 0) {
      const event_t *e = &g_events[i];
      out.fire_events++;
      v3 event_dir = v3_norm(e->b);
      out.shot_axis = v3_len(v3_sub(event_dir,
                                    v3_norm(g_fire_anchor_pose.gun_f))) * 1000.0f;
      v3 event_muzzle = v3_sub(e->a, v3_scale(event_dir, e->f));
      out.ballistic_anchor = v3_len(v3_sub(event_muzzle, out.anchor0));
    }
  out.all_events = g_num_events - ev0;
  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0 && g_tracers[i].src == 0) {
      out.tracer = g_tracers[i].a;
      break;
    }
  // A previous-frame value that is still geometrically plausible must be rejected;
  // the production publish+consume sequence below then replaces it at this exact T0.
  g_vm_muz_cam = (v3){{0.05f, -0.03f, 0.40f}};
  g_vm_muz_tick = g_tick - 1;
  frame_cam_t stale_cam = rf_camera(p, 0.0f, NULL);
  rf_vm_frame_t stale_vmf = {0};
  v3 stale_anchor;
  out.stale_rejected = !rf_vm_muzzle_anchor(p, &stale_cam, &stale_vmf,
                                             &stale_anchor);
  out.map0_ok = vmframe_render_order(p, 0.0f, &out.t0, &out.fx0,
                                    &out.pub_cam0, &out.pub_tick0,
                                    &out.publish0, &out.remap0,
                                    &out.vm_depth0, &out.fx_depth0);

  // Advance the real player movement/presentation halves, but not match_tick/vfx_tick:
  // those outer-frame systems spawn default bots and age unrelated global effects,
  // which would make this deliberately transactional proof donate RNG/spawn/VFX state
  // to the next command. Alpha=1 samples the resulting T1 state rather than its T0
  // interpolation twin, so both the rendered FP muzzle and the event-owned body pose
  // have actually recoiled.
  static const int no_held[IN_COUNT] = {0};
  g_tick++;
  player_move_tick(p, 0, no_held, 0.0f, 0.0f);
  // recoil_stage places the actor above all generated solids so hitscan is map
  // independent. This case is explicitly a STANDING hip-fire transition; restore its
  // staged contact after the movement half, otherwise the synthetic height turns T1
  // airborne and the real airborne hard gate correctly masks the carry follower.
  p->grounded = p->prev_grounded = 1;
  p->vel.y = 0.0f;
  player_present_tick(p, 0);
  out.pose1 = pose_muzzle(&a->pose, WPN_SR);
  out.map1_ok = vmframe_render_order(p, 1.0f, &out.t1, &out.fx1,
                                    &out.pub_cam1, &out.pub_tick1,
                                    &out.publish1, &out.remap1,
                                    &out.vm_depth1, &out.fx_depth1);
  return out;
}

static void vmframe_proof(void) {
  recoil_save_globals();
  int home_save = g_home;
  int third_save = g_cfg.third_person;
  int vmorb_save = g_vmorb.on;
  g_home = 0;
  g_cfg.third_person = 0;
  g_vmorb.on = 0;
  g_tick = 1000;
  int ok = 1;

  // SKEL_POSE_ONLY is the shot/event re-anchor path. It may replace pose, but a
  // moving figure's persistent gait state must remain byte-identical: otherwise each
  // EV_FIRE integrates stance/ankle filters once more than an ordinary animation
  // tick. Use deliberately non-equilibrated values so both leaked writes are visible.
  anim_t pure;
  anim_reset(&pure, (v3){{0, 0, 0}}, 0.0f, WPN_AR);
  pure.spd = 4.8f;
  pure.phase = 0.73f;
  pure.bob_lag = 0.69f;
  pure.air_s = pure.slide_s = 0.0f;
  pure.foot[0].t = pure.foot[1].t = 1.0f;
  pure.stance[0] = 0.11f; pure.stance[1] = 0.87f;
  pure.fpitch[0] = 0.31f; pure.fpitch[1] = -0.24f;
  anim_t pure_before = pure;
  skel_solve(&pure, (v3){{0, 0, 0}}, 0.23f, -0.14f, WPN_AR, 1,
             SKEL_POSE_ONLY);
  int pure_pose_changed = memcmp(&pure.pose, &pure_before.pose,
                                 sizeof pure.pose) != 0;
  pose_t pure_pose = pure.pose;
  pure.pose = pure_before.pose;
  int pure_state_same = !memcmp(&pure, &pure_before, sizeof pure);
  pure.pose = pure_pose;
  int pure_ok = pure_pose_changed && pure_state_same;
  printf("vmframe pose_only state=%d pose=%d stance=(%.3f %.3f) "
         "fpitch=(%.3f %.3f) %s\n",
         pure_state_same, pure_pose_changed, (double)pure.stance[0],
         (double)pure.stance[1], (double)pure.fpitch[0],
         (double)pure.fpitch[1], pure_ok ? "ok" : "FAIL");
  ok &= pure_ok;

  // Read the actual 3P bore back from skel_solve at fully lowered and one-tick
  // raising poses. Expected values come from the production SR descriptor, so this
  // proves its authored 20/15 profile without copying either number into the harness.
  const gun_pose_desc_t *sr_pose = gun_pose_desc(WPN_SR);
  const v3 aim = {{0, 0, -1}};
  player_t low_p, raise_p;
  anim_t low_a, raise_a;
  vmframe_stage_case(&low_p, &low_a, VMF_IDLE);
  int low_idle = gun_pose_state(&low_p.wp, &low_a, low_p.vel,
                                  low_p.grounded, low_p.sliding);
  skel_solve(&low_a, low_p.pos, 0.0f, 0.0f, WPN_SR, low_idle,
             SKEL_POSE_ONLY);
  vmframe_3p_axis_t low_axis = vmframe_3p_axis(aim, low_a.pose.gun_f);

  vmframe_stage_case(&raise_p, &raise_a, VMF_READY);
  vmframe_ready_tick(&raise_p, &raise_a);
  int raise_idle = gun_pose_state(&raise_p.wp, &raise_a, raise_p.vel,
                                    raise_p.grounded, raise_p.sliding);
  skel_solve(&raise_a, raise_p.pos, 0.0f, 0.0f, WPN_SR, raise_idle,
             SKEL_POSE_ONLY);
  vmframe_3p_axis_t raise_axis = vmframe_3p_axis(aim, raise_a.pose.gun_f);

  float target_down = sr_pose->carry_down / DEG2RAD;
  float target_cross = sr_pose->carry_cross / DEG2RAD;
  float raise_lobe = 1.0f - raise_a.ready_s;
  float raise_down = target_down * raise_lobe;
  float raise_cross = target_cross * raise_lobe;
  float axis_err = fmaxf(fabsf(low_axis.down - target_down),
                         fabsf(low_axis.cross - target_cross));
  axis_err = fmaxf(axis_err, fabsf(raise_axis.down - raise_down));
  axis_err = fmaxf(axis_err, fabsf(raise_axis.cross - raise_cross));

  // Spatial action gates force ready_pose=1 inside the production solver. Compare each
  // staged pose against the same state with ready_s explicitly raised; even slide's
  // independent muzzle-up rotation must therefore have zero low-ready contribution.
  // Bolt/cooldown and fire are carry intents now: hard-gating either is the arm snap
  // this proof is specifically meant to reject.
  float busy_max = 0.0f;
  int busy_count = 0, busy_ok = 1;
  for (int c = VMF_ADS_T0; c < VMF_NCASE; c++) {
    if (c == VMF_BOLT || c == VMF_FIRE) continue;
    player_t busy_p;
    anim_t busy_a;
    vmframe_stage_case(&busy_p, &busy_a, c);
    int idle = gun_pose_state(&busy_p.wp, &busy_a, busy_p.vel,
                                busy_p.grounded, busy_p.sliding);
    skel_solve(&busy_a, busy_p.pos, 0.0f, 0.0f, WPN_SR, idle,
               SKEL_POSE_ONLY);
    anim_t raised = busy_a;
    raised.ready_s = raised.prev_ready_s = 1.0f;
    skel_solve(&raised, busy_p.pos, 0.0f, 0.0f, WPN_SR, idle,
               SKEL_POSE_ONLY);
    float chord = v3_len(v3_sub(busy_a.pose.gun_f, raised.pose.gun_f));
    float angle = 2.0f * asinf(f_clamp(chord * 0.5f, 0.0f, 1.0f)) / DEG2RAD;
    busy_max = fmaxf(busy_max, angle);
    busy_ok &= idle == GUN_POSE_BLOCKED && angle <= 0.001f;
    busy_count++;
  }
  int axis_ok = low_idle && raise_idle && low_axis.finite && raise_axis.finite &&
                raise_a.ready_s > 0.0f && raise_a.ready_s < 1.0f &&
                axis_err <= 0.02f && busy_ok &&
                busy_count == VMF_NCASE - VMF_ADS_T0 - 2;
  printf("vmframe 3p_axis low=%.3f/%.3fdeg target=%.3f/%.3fdeg "
         "raise=%.3f/%.3fdeg target=%.3f/%.3fdeg ready=%.3f "
         "busy=%d max=%.3fdeg %s\n",
         (double)low_axis.down, (double)low_axis.cross,
         (double)target_down, (double)target_cross,
         (double)raise_axis.down, (double)raise_axis.cross,
         (double)raise_down, (double)raise_cross, (double)raise_a.ready_s,
         busy_count, (double)busy_max, axis_ok ? "ok" : "FAIL");
  ok &= axis_ok;

  static const float ALPHA[] = {0.0f, 0.37f, 1.0f};
  for (int c = 0; c < VMF_NCASE; c++) {
    player_t p;
    anim_t a;
    vmframe_stage_case(&p, &a, c);
    if (c == VMF_READY) vmframe_ready_tick(&p, &a);
    int idle_ok = gun_pose_state(&p.wp, &a, p.vel, p.grounded, p.sliding);
    skel_solve(&a, p.pos, 0.0f, 0.0f, WPN_SR, idle_ok, SKEL_POSE_ONLY);
    anim_t refa = a;
    refa.ready_s = refa.prev_ready_s = 1.0f;
    skel_solve(&refa, p.pos, 0.0f, 0.0f, WPN_SR, 0, SKEL_POSE_ONLY);
    float body_err = vmframe_pose_delta(&a.pose, &refa.pose);
    float vm_err = 0.0f;
    vmframe_diag_t shown = {0};
    int case_ok = 1;
    float prev_ready = -1.0f;
    for (unsigned ai = 0; ai < sizeof ALPHA / sizeof ALPHA[0]; ai++) {
      float alpha = ALPHA[ai];
      float ready = vm_render_ready(&p, &a, alpha);
      vmframe_diag_t live = vmframe_build(&p, alpha, ready);
      vmframe_diag_t ref = vmframe_build(&p, alpha, 1.0f);
      float e = v3_len(v3_sub(live.muzzle, ref.muzzle));
      if (e > vm_err) vm_err = e;
      if (!ai) shown = live;
      case_ok &= live.finite && live.stock_found && live.mesh_plan <= 1e-6f &&
                 ref.mesh_plan <= 1e-6f;
      if (c == VMF_IDLE || c == VMF_BOLT || c == VMF_FIRE)
        case_ok &= ready == 0.0f;
      else if (c == VMF_READY) {
        case_ok &= ready >= 0.0f && ready < 1.0f && ready >= prev_ready;
        prev_ready = ready;
      } else case_ok &= ready == 1.0f;
    }
    if (c == VMF_IDLE)
      case_ok &= idle_ok && vm_err > 0.005f && body_err > 0.02f;
    else if (c == VMF_READY)
      case_ok &= idle_ok && a.ready_s > 0.10f && a.ready_s < 0.30f &&
                 vm_err > 0.005f && body_err > 0.005f;
    else if (c == VMF_BOLT)
      case_ok &= idle_ok == GUN_POSE_IDLE &&
                 vm_err > 0.005f && body_err > 0.02f;
    else if (c == VMF_FIRE)
      case_ok &= idle_ok == GUN_POSE_IDLE &&
                 vm_err > 0.005f && body_err < 1e-6f;
    else
      case_ok &= idle_ok == GUN_POSE_BLOCKED &&
                 vm_err < 1e-6f && body_err < 1e-6f;
    printf("vmframe %-7s ready=%.3f pitch=%+.2f yaw=%+.2f roll=%+.2f "
           "stock_eye=%.1fmm wrist_eye=(%.1f %.1f)mm stock_wrist=%.1fmm "
           "low_delta=(%.3f %.3f)mm mesh/plan=%.3fmm %s\n",
           VMF_NAME[c], (double)shown.ready, (double)shown.pitch,
           (double)shown.yaw, (double)shown.roll,
           (double)(shown.stock_eye * 1000.0f),
           (double)(shown.wrist_eye[0] * 1000.0f),
           (double)(shown.wrist_eye[1] * 1000.0f),
           (double)(shown.stock_wrist * 1000.0f),
           (double)(vm_err * 1000.0f), (double)(body_err * 1000.0f),
           (double)(shown.mesh_plan * 1000.0f),
           case_ok ? "ok" : "FAIL");
    ok &= case_ok;
  }

  // A plain carry intent must travel through ready_s in BOTH directions. Sample the
  // production tick and renderer interpolation rather than duplicating the follower's
  // equation: T0 remains low, the first tick is partial, sixteen ticks are nearly up,
  // and release lowers monotonically without a hard action clamp.
  player_t rp;
  anim_t ra;
  vmframe_stage_case(&rp, &ra, VMF_READY);
  float raise0 = vm_render_ready(&rp, &ra, 1.0f);
  float raise1 = 0.0f, raise4 = 0.0f, raise8 = 0.0f, raise16 = 0.0f;
  float last = raise0;
  int transition_ok = gun_pose_state(&rp.wp, &ra, rp.vel,
                                        rp.grounded, rp.sliding) && raise0 == 0.0f;
  for (int i = 1; i <= 16; i++) {
    vmframe_ready_tick(&rp, &ra);
    float r = vm_render_ready(&rp, &ra, 1.0f);
    transition_ok &= r > last && r < 1.0f &&
                     gun_pose_state(&rp.wp, &ra, rp.vel,
                                      rp.grounded, rp.sliding);
    last = r;
    if (i == 1) raise1 = r;
    if (i == 4) raise4 = r;
    if (i == 8) raise8 = r;
    if (i == 16) raise16 = r;
  }
  rp.wp.ready_want = 0;
  float lower1 = 0.0f, lower30 = 0.0f, lower90 = 0.0f;
  last = raise16;
  for (int i = 1; i <= 90; i++) {
    vmframe_ready_tick(&rp, &ra);
    float r = vm_render_ready(&rp, &ra, 1.0f);
    transition_ok &= r < last && r > 0.0f &&
                     gun_pose_state(&rp.wp, &ra, rp.vel,
                                      rp.grounded, rp.sliding);
    last = r;
    if (i == 1) lower1 = r;
    if (i == 30) lower30 = r;
    if (i == 90) lower90 = r;
  }
  transition_ok &= raise1 > 0.10f && raise1 < 0.30f &&
                   raise16 > 0.90f && lower1 < raise16 &&
                   lower30 > lower90 && lower90 < 0.05f;
  printf("vmframe ready_transition raise=(%.3f %.3f %.3f %.3f %.3f) "
         "lower=(%.3f %.3f %.3f) %s\n",
         (double)raise0, (double)raise1, (double)raise4,
         (double)raise8, (double)raise16, (double)lower1,
         (double)lower30, (double)lower90,
         transition_ok ? "ok" : "FAIL");
  ok &= transition_ok;

  // anim_reset itself promises a shouldered SR on the first visible frame. Its
  // scalar and solved pose must agree at T0, then an actually idle production tick
  // may lower it only through the slow follower. This catches a reset-only snap that
  // a hand-staged 0 -> 1 -> 0 transition cannot see.
  player_t reset_p;
  anim_t reset_a;
  vmframe_stage_case(&reset_p, &reset_a, VMF_IDLE);
  anim_reset(&reset_a, reset_p.pos, 0.0f, WPN_SR);
  float reset0 = vm_render_ready(&reset_p, &reset_a, 1.0f);
  anim_t reset_ref = reset_a;
  reset_ref.ready_s = reset_ref.prev_ready_s = 1.0f;
  skel_solve(&reset_ref, reset_p.pos, 0.0f, 0.0f, WPN_SR, 0,
             SKEL_POSE_ONLY);
  float reset_pose_err = vmframe_pose_delta(&reset_a.pose, &reset_ref.pose);
  float reset1 = 0.0f, reset30 = 0.0f, reset90 = 0.0f;
  float reset_last = reset0;
  int reset_ok = reset0 == 1.0f && reset_pose_err < 1e-6f &&
                 gun_pose_state(&reset_p.wp, &reset_a, reset_p.vel,
                                  reset_p.grounded, reset_p.sliding);
  for (int i = 1; i <= 90; i++) {
    vmframe_ready_tick(&reset_p, &reset_a);
    float r = vm_render_ready(&reset_p, &reset_a, 1.0f);
    reset_ok &= r < reset_last && r > 0.0f &&
                gun_pose_state(&reset_p.wp, &reset_a, reset_p.vel,
                                 reset_p.grounded, reset_p.sliding);
    reset_last = r;
    if (i == 1) reset1 = r;
    if (i == 30) reset30 = r;
    if (i == 90) reset90 = r;
  }
  reset_ok &= reset1 < reset0 && reset30 > reset90 && reset90 < 0.05f;
  printf("vmframe reset_transition ready=%.3f pose=%.3fmm "
         "lower=(%.3f %.3f %.3f) %s\n",
         (double)reset0, (double)(reset_pose_err * 1000.0f),
         (double)reset1, (double)reset30, (double)reset90,
         reset_ok ? "ok" : "FAIL");
  ok &= reset_ok;

  // Paired production shots: the ordinary EV_FIRE path is the reference, the second
  // suppresses every event as online prediction does. Local FP state and its exact
  // VM-FOV -> world-FOV tracer remap must agree at T0/T1. The event-local shouldered
  // pose owns the tracer anchor at T0 and must coincide with the visible live carry
  // pose; at T1 ready_s must have advanced only part way. The 3P poses then MUST
  // diverge because recoil_body_impulse belongs to authoritative EV_FIRE and is
  // intentionally absent from forward prediction.
  // Leave the authoritative case live after the pair: its tracer then drives one
  // complete production render below, allowing fx_build's consumer-side witness to
  // prove the actual render_frame callsite—not merely the publish helper's algebra.
  vmframe_fire_t pred = vmframe_fire_case(1);
  vmframe_fire_t event = vmframe_fire_case(0);
  unsigned fx_serial0 = g_fx_build_witness.serial;
  unsigned emit_serial0 = g_vm_emit_witness.serial;
  unsigned order0 = g_vm_render_order;
  long render_tick = g_tick;
  int tracer_live = 0;
  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0 && g_tracers[i].src == g_local_ent)
      tracer_live++;
  render_frame(&g_players[0], 1.0f, 120, NULL);
  float call_anchor = v3_len(v3_sub(g_fx_build_witness.muzzle, event.fx1));
  float emit_anchor = v3_len(v3_sub(g_vm_emit_witness.muzzle,
                                    event.t1.muzzle));
  float emit_mesh = v3_len(v3_sub(g_vm_emit_witness.mesh_muzzle,
                                  g_vm_emit_witness.muzzle));
  int callsite_ok = g_fx_build_witness.serial == fx_serial0 + 1u &&
                    g_vm_emit_witness.serial == emit_serial0 + 1u &&
                    g_fx_build_witness.order == order0 + 1u &&
                    g_vm_emit_witness.order == order0 + 2u &&
                    g_fx_build_witness.live && tracer_live == 1 &&
                    g_fx_build_witness.tick == render_tick &&
                    g_fx_build_witness.publish_tick == render_tick &&
                    g_vm_emit_witness.tick == render_tick &&
                    g_vm_muz_tick == render_tick && call_anchor <= 0.002f &&
                    emit_anchor <= 1e-6f && emit_mesh <= 1e-6f;
  float vm0 = v3_len(v3_sub(event.t0.muzzle, pred.t0.muzzle));
  float vm1 = v3_len(v3_sub(event.t1.muzzle, pred.t1.muzzle));
  float fx0 = v3_len(v3_sub(event.fx0, pred.fx0));
  float fx1 = v3_len(v3_sub(event.fx1, pred.fx1));
  float pose0 = v3_len(v3_sub(event.pose0, pred.pose0));
  float pose1 = v3_len(v3_sub(event.pose1, pred.pose1));
  float anchor0 = v3_len(v3_sub(event.anchor0, event.tracer));
  float visible_hold0 = v3_len(v3_sub(event.pose0, event.tracer));
  float pub0 = fmaxf(event.publish0, pred.publish0);
  float pub1 = fmaxf(event.publish1, pred.publish1);
  float pub_pair0 = v3_len(v3_sub(event.pub_cam0, pred.pub_cam0));
  float pub_pair1 = v3_len(v3_sub(event.pub_cam1, pred.pub_cam1));
  float pub_step = v3_len(v3_sub(event.pub_cam1, event.pub_cam0));
  float mesh_plan = fmaxf(fmaxf(event.t0.mesh_plan, event.t1.mesh_plan),
                          fmaxf(pred.t0.mesh_plan, pred.t1.mesh_plan));
  float remap0 = fmaxf(event.remap0, pred.remap0);
  float remap1 = fmaxf(event.remap1, pred.remap1);
  int fire_ok = event.fire_events == 1 && pred.all_events == 0 &&
                event.stale_rejected && pred.stale_rejected &&
                event.latch == 1.0f && pred.latch == 1.0f &&
                event.t0.ready == 0.0f && pred.t0.ready == 0.0f &&
                event.t1.ready > 0.10f && event.t1.ready < 0.30f &&
                pred.t1.ready > 0.10f && pred.t1.ready < 0.30f &&
                event.t0.finite && event.t1.finite &&
                pred.t0.finite && pred.t1.finite &&
                event.map0_ok && event.map1_ok && pred.map0_ok &&
                pred.map1_ok && vm0 <= 0.002f && vm1 <= 0.002f &&
                fx0 <= 0.002f && fx1 <= 0.002f &&
                pub0 <= 1e-6f && pub1 <= 1e-6f &&
                pub_pair0 <= 0.002f && pub_pair1 <= 0.002f &&
                pub_step > 0.002f && mesh_plan <= 1e-6f &&
                event.pub_tick0 == event.pub_tick1 - 1 &&
                pred.pub_tick0 == pred.pub_tick1 - 1 &&
                event.pub_tick0 == pred.pub_tick0 &&
                event.pub_tick1 == pred.pub_tick1 &&
                remap0 <= 0.002f && remap1 <= 0.002f &&
                pose0 <= 0.002f && pose1 > 0.002f && anchor0 <= 0.002f &&
                visible_hold0 <= 0.002f && event.shot_axis <= 0.001f &&
                event.ballistic_anchor <= 0.002f;
  printf("vmframe fire sr T0 vm_pair=%.3fmm fx_pair=%.3fmm "
         "publish=%.3fmm pair=%.3fmm remap=%.3fmm "
         "pose_pair=%.3fmm anchor=%.3fmm visible_hold=%.3fmm "
         "bore=%.4fmrad ballistic=%.3fmm "
         "depth_vm/fx=%.1f/%.1fmm "
         "tick=%ld stale=%d\n",
         (double)(vm0 * 1000.0f), (double)(fx0 * 1000.0f),
         (double)(pub0 * 1000.0f), (double)(pub_pair0 * 1000.0f),
         (double)(remap0 * 1000.0f), (double)(pose0 * 1000.0f),
         (double)(anchor0 * 1000.0f), (double)(visible_hold0 * 1000.0f),
         (double)event.shot_axis, (double)(event.ballistic_anchor * 1000.0f),
         (double)(event.vm_depth0 * 1000.0f),
         (double)(event.fx_depth0 * 1000.0f), event.pub_tick0,
         event.stale_rejected && pred.stale_rejected);
  printf("vmframe fire sr T1 vm_pair=%.3fmm fx_pair=%.3fmm remap=%.3fmm "
         "publish=%.3fmm pair=%.3fmm step=%.3fmm mesh/plan=%.3fmm "
         "body_event/pred=%.3fmm depth_vm/fx=%.1f/%.1fmm tick=%ld "
         "ready=%.3f/%.3f latch=%.0f/%.0f suppressed=%d %s\n",
         (double)(vm1 * 1000.0f), (double)(fx1 * 1000.0f),
         (double)(remap1 * 1000.0f), (double)(pub1 * 1000.0f),
         (double)(pub_pair1 * 1000.0f), (double)(pub_step * 1000.0f),
         (double)(mesh_plan * 1000.0f), (double)(pose1 * 1000.0f),
         (double)(event.vm_depth1 * 1000.0f), (double)(event.fx_depth1 * 1000.0f),
         event.pub_tick1,
         (double)pred.t0.ready, (double)pred.t1.ready,
         (double)event.latch, (double)pred.latch, pred.all_events == 0,
         fire_ok ? "ok" : "FAIL");
  printf("vmframe pass publish->fx_build->vm tick=%ld/%ld/%ld "
         "serial=fx%u->%u vm%u->%u order=%u/%u/%u live=%d tracer=%d "
         "anchor=%.3fmm emit=%.3fmm mesh=%.3fmm %s\n",
         g_fx_build_witness.publish_tick, g_fx_build_witness.tick,
         g_vm_emit_witness.tick, fx_serial0, g_fx_build_witness.serial,
         emit_serial0, g_vm_emit_witness.serial, order0,
         g_fx_build_witness.order, g_vm_emit_witness.order,
         g_fx_build_witness.live, tracer_live,
         (double)(call_anchor * 1000.0f), (double)(emit_anchor * 1000.0f),
         (double)(emit_mesh * 1000.0f), callsite_ok ? "ok" : "FAIL");
  ok &= fire_ok && callsite_ok;
  g_home = home_save;
  g_cfg.third_person = third_save;
  g_vmorb.on = vmorb_save;
  recoil_restore_globals();
  printf("vmframe %s\n", ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

typedef struct {
  cl_pred_t pred[58];
  int peak_tick, last_shot_tick, shots;
  float peak;
} netrecoil_ref_t;

// A small production snapshot builder, rather than a call to net_snapshot_build:
// this proof needs the server's entity state at the ACK tick while the live globals
// deliberately contain the client's later predicted endpoint.
static size_t netrecoil_snapshot(unsigned char *buf, size_t cap, uint32_t st,
                                 uint32_t li, const net_ent_t *mine,
                                 const net_ent_t *remote, const event_t ev[2]) {
  np_wr w = {buf, cap, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_SNAPSHOT);
  np_w32(&w, st); np_w32(&w, li);
  np_w32(&w, net_world_hash()); np_w32(&w, g_map_seed); np_w8(&w, 0);
  np_w8(&w, 2);
  net_ent_encode(&w, mine);
  net_ent_encode(&w, remote);
  np_w8(&w, 2);
  for (int i = 0; i < 2; i++) {
    net_ev_t ne;
    net_ev_from_live(&ne, &ev[i], (uint32_t)i + 1);
    net_ev_encode(&w, &ne);
  }
  return w.bad ? 0 : w.at;
}

static int netrecoil_case(int burst) {
  enum { LI = 100, END = 157, NREC = END - LI + 1 };
  static_assert(NREC == 58);
  memset(g_players, 0, sizeof g_players);
  memset(g_player_anim, 0, sizeof g_player_anim);
  memset(g_bots, 0, sizeof g_bots);
  memset(g_humans_on, 0, sizeof g_humans_on);
  memset(&g_match, 0, sizeof g_match);
  memset(&g_cl, 0, sizeof g_cl);
  memset(g_cl_pred, 0, sizeof g_cl_pred);
  memset(g_events, 0, sizeof g_events);
  memset(g_tracers, 0, sizeof g_tracers);
  memset(g_flashes, 0, sizeof g_flashes);
  memset(g_parts, 0, sizeof g_parts);
  g_num_events = g_ev_peak = 0;
  g_ev_drops = 0;
  g_muzzle_t = g_muzzle_fresh = 0;
  g_online = 1;
  g_local_ent = 0;
  g_predicting = 0;
  g_prediction_replay = 0;
  g_headless = 0;
  g_sim_mv = g_cfg.mv;
  g_sim_wp = g_cfg.wp;
  g_cl.state = NC_PLAYING;
  g_cl.my_ent = 0;
  g_cl.synced = 1;
  g_cl.sync_seq = LI;
  g_cl.seq = END + 1;
  g_cl.have_spawn_seq = 1;
  g_cl.spawn_seq = g_spawn_seq[0];
  g_cl.prev_yaw = 0.0f;
  g_cl.have_prev_yaw = 1;
  g_cl_corrections = 0;
  g_cl_worst_err_mm = 0.0f;
  g_cl_ev_applied = g_cl_ev_dupes = 0;

  g_humans_on[0] = 1;
  player_t *p = &g_players[0];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 20.0f, 0.0f}};
  p->vel = (v3){{0.0f, 0.0f, 0.0f}};
  p->grounded = p->prev_grounded = 0;
  p->yaw = p->pitch = 0.0f;
  p->wp.cur = WPN_AR;
  p->vm.last_eye = p->eye;
  anim_reset(&g_player_anim[0], p->pos, p->yaw, p->wp.cur);
  g_rng_gun[0] = rng_gun_seed(1337u, 0);
  player_t anchor = *p;
  g_cl.predpos[LI % NET_HIST] = p->pos;
  // The replay anchor is the state AFTER input LI, including its button history.
  // A valid all-up record makes a later FIRE at LI+3 a genuine press edge.
  g_cl.inlog[LI % NET_HIST] =
      (net_input_t){.tick = LI, .yaw = net_q_yaw(0.0f),
                    .pitch = net_q_pitch(0.0f)};
  g_cl_pred[LI % NET_HIST] =
      (cl_pred_t){.wp = p->wp, .vm = p->vm, .rng = g_rng_gun[0], .yaw = p->yaw};

  netrecoil_ref_t ref;
  memset(&ref, 0, sizeof ref);
  ref.pred[0] = g_cl_pred[LI % NET_HIST];
  int ammo0 = p->wp.ammo[WPN_AR];
  for (int rel = 1; rel < NREC; rel++) {
    uint32_t rt = LI + (uint32_t)rel;
    int held[IN_COUNT] = {0};
    held[IN_FIRE] = burst ? (rel >= 3 && rel <= 23) : rel == 3;
    uint16_t buttons = held[IN_FIRE] ? (uint16_t)(1u << IN_FIRE) : 0;
    g_cl.inlog[rt % NET_HIST] =
        (net_input_t){.tick = rt, .buttons = buttons,
                      .yaw = net_q_yaw(0.0f), .pitch = net_q_pitch(0.0f)};
    int before = p->wp.ammo[WPN_AR];
    g_tick = (long)rt;
    g_predicting = 1;
    player_move_tick(p, 0, held, 0.0f, 0.0f);
    g_predicting = 0;
    if (p->wp.ammo[WPN_AR] != before) ref.last_shot_tick = rel;
    g_cl.predpos[rt % NET_HIST] = p->pos;
    g_cl_pred[rt % NET_HIST] =
        (cl_pred_t){.wp = p->wp, .vm = p->vm, .rng = g_rng_gun[0], .yaw = p->yaw};
    ref.pred[rel] = g_cl_pred[rt % NET_HIST];
    float back = p->vm.kick_back;
    if (back > ref.peak) { ref.peak = back; ref.peak_tick = rel; }
  }
  ref.shots = ammo0 - p->wp.ammo[WPN_AR];
  player_t endpoint = *p;
  uint64_t endpoint_rng = g_rng_gun[0];
  int forward_player_shots = p->shots;
  int forward_match_shots = g_match.shots[0];
  bot_noise_t forward_noise = g_match.noise[BOT_NOISE_SHOT][0];

  // A correction replays the recorded trigger edge to rebuild weapon, RNG and VM
  // history, but it must not replay the local body's one-shot carry latch: that edge
  // was already presented on the original forward-prediction tick and may have fully
  // decayed by the time the snapshot arrives. Exercise net_cl_replay itself with a
  // deliberately settled sentinel, then restore the live endpoint for the encoded
  // snapshot half below.
  anim_t forward_anim = g_player_anim[0];
  g_player_anim[0].fire_k = 0.0f;
  g_player_anim[0].ready_s = 0.271f;
  g_player_anim[0].prev_ready_s = 0.263f;
  anim_t settled_anim = g_player_anim[0];
  *p = anchor;
  // Accounting is lifetime state, not state at the rollback anchor. The production
  // correction keeps the live value while restoring only wp/vm/RNG/yaw from LI.
  p->shots = forward_player_shots;
  g_rng_gun[0] = ref.pred[0].rng;
  net_cl_replay(p, 0, LI);
  int replay_anim_same = !memcmp(&g_player_anim[0], &settled_anim,
                                 sizeof settled_anim) && !g_predicting &&
                         !g_prediction_replay;
  int direct_sidefx_same = p->shots == forward_player_shots &&
                           g_match.shots[0] == forward_match_shots &&
                           !memcmp(&g_match.noise[BOT_NOISE_SHOT][0],
                                   &forward_noise, sizeof forward_noise);
  *p = endpoint;
  g_rng_gun[0] = endpoint_rng;
  g_player_anim[0] = forward_anim;

  // The authoritative state is after LI, displaced by 10 cm in X: above the hard
  // 5 cm reconciliation edge but far below the 2 m desync/teleport branch.
  *p = anchor;
  p->pos.x += 0.10f;
  net_ent_t mine;
  net_ent_from_live(&mine, 0);
  *p = endpoint;

  int rh = 1, rent = ent_of_human(rh);
  g_humans_on[rh] = 1;
  player_t *remote_p = &g_players[rh];
  player_spawn(remote_p);
  remote_p->pos = remote_p->prev_pos = (v3){{3.0f, 0.0f, 6.0f}};
  remote_p->wp.cur = WPN_SR;
  remote_p->wp.ready_want = 1;
  anim_reset(&g_player_anim[rh], remote_p->pos, remote_p->yaw, remote_p->wp.cur);
  net_ent_t remote;
  net_ent_from_live(&remote, rent);

  v3 dir = {{0.0f, 0.0f, -1.0f}};
  event_t echo[2] = {
    {.type = EV_FIRE, .a = v3_add(endpoint.pos, v3_scale(dir, 20.0f)),
     .b = dir, .f = 20.0f, .i = 0, .wpn = WPN_AR, .src = 0, .dst = -1},
    {.type = EV_FIRE, .a = v3_add(remote_p->pos, v3_scale(dir, 20.0f)),
     .b = dir, .f = 20.0f, .i = 4, .wpn = WPN_SR, .src = rent, .dst = -1},
  };
  unsigned char snap[256];
  size_t len = netrecoil_snapshot(snap, sizeof snap, END + 10, LI,
                                  &mine, &remote, echo);
  net_cl_snapshot(snap, (int)len);

  int series_same = len > 0;
  float peak = 0.0f;
  int peak_tick = 0;
  for (int rel = 0; rel < NREC; rel++) {
    const cl_pred_t *got = &g_cl_pred[(LI + (uint32_t)rel) % NET_HIST];
    if (memcmp(got, &ref.pred[rel], sizeof *got)) series_same = 0;
    if (got->vm.kick_back > peak) { peak = got->vm.kick_back; peak_tick = rel; }
  }
  int endpoint_same = !memcmp(&p->wp, &endpoint.wp, sizeof p->wp) &&
                      !memcmp(&p->vm, &endpoint.vm, sizeof p->vm) &&
                      g_rng_gun[0] == endpoint_rng;
  anim_t *ra = &g_player_anim[rh];
  int body_impulse = ra->recoil_back == 0.0f && ra->recoil_lift == 0.0f &&
                     ra->recoil_back_v > 0.5f && ra->recoil_lift_v == 0.0f &&
                     remote_p->wp.rec_p == 0.0f && remote_p->wp.rec_p_v > 1.0f;
  g_tick++;
  client_anim_all();
  int body_moved = ra->recoil_back > 0.004f && ra->recoil_lift == 0.0f &&
                   remote_p->wp.rec_p_v > 1.0f;
  int corrected = g_cl_corrections == 1 && g_cl_worst_err_mm > 50.0f;
  int snapshot_sidefx_same = p->shots == forward_player_shots &&
                             g_match.shots[0] == forward_match_shots &&
                             !memcmp(&g_match.noise[BOT_NOISE_SHOT][0],
                                     &forward_noise, sizeof forward_noise);
  int expected_shots = burst ? 3 : 1;
  int ok = len > 0 && corrected && series_same && endpoint_same &&
           peak == ref.peak && peak_tick == ref.peak_tick &&
           ref.shots == expected_shots && body_impulse && body_moved &&
           replay_anim_same && direct_sidefx_same && snapshot_sidefx_same &&
           forward_player_shots == expected_shots &&
           forward_match_shots == expected_shots &&
           g_cl_ev_applied == 2 && g_cl_ev_dupes == 0;
  printf("netrecoil %s corr=%.1fmm peak=%.2f/%.2fmm@T%d/T%d "
         "end_vm=%d series=%d ammo=%d/%d heat=%.6f rec=(%.6f %.6f) "
         "rng=%016llx echo_once=%d replay_body=%d remote3p=(%d %d) "
         "sidefx=%d/%d counts=%d/%d noise_t=%ld events=%d/%d %s\n",
         burst ? "burst" : "single", (double)g_cl_worst_err_mm,
         (double)(ref.peak * 1000.0f), (double)(peak * 1000.0f),
         ref.peak_tick, peak_tick, endpoint_same, series_same,
         p->wp.ammo[WPN_AR], endpoint.wp.ammo[WPN_AR], (double)p->wp.heat,
         (double)p->wp.rec_p, (double)p->wp.rec_y,
         (unsigned long long)g_rng_gun[0], endpoint_same, replay_anim_same,
         body_impulse, body_moved, direct_sidefx_same, snapshot_sidefx_same,
         forward_player_shots, forward_match_shots, forward_noise.tick,
         g_cl_ev_applied, g_cl_ev_dupes,
         ok ? "ok" : "FAIL");
  return ok;
}

static void netrecoil_proof(void) {
  recoil_save_globals();
  int single = netrecoil_case(0);
  int burst = netrecoil_case(1);
  recoil_restore_globals();
  int ok = single && burst;
  printf("netrecoil cl_pred=%zuB ring=%zuB %s\n", sizeof(cl_pred_t),
         sizeof g_cl_pred, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// A one-entity production snapshot for the respawn half of netdeath. Keeping the
// actual encoder/decoder in the path is what makes the witness cover the branch in
// net_cl_snapshot rather than merely restating its assignments in the harness.
static size_t netdeath_snapshot(unsigned char *buf, size_t cap, uint32_t st,
                                uint32_t li, const net_ent_t *ent) {
  np_wr w = {buf, cap, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_SNAPSHOT);
  np_w32(&w, st); np_w32(&w, li);
  np_w32(&w, net_world_hash()); np_w32(&w, g_map_seed); np_w8(&w, 0);
  np_w8(&w, 1);
  net_ent_encode(&w, ent);
  np_w8(&w, 0);
  return w.bad ? 0 : w.at;
}

// An online death all the way through to a corpse, client half.
static void netdeath_proof(void) {
  // THE ONLINE DEATH PATH, as a gate. Offline a kill runs ent_damage (ragdoll launch)
  // and player_present_tick (corpse handover) on one tick.
  memset(g_corpses, 0, sizeof g_corpses);
  memset(g_player_corpse_q, 0, sizeof g_player_corpse_q);
  memset(g_cl_alive_prev, 0, sizeof g_cl_alive_prev);
  memset(g_cl_body, 0, sizeof g_cl_body);
  memset(g_humans_on, 0, sizeof g_humans_on);
  g_match.nbots = 0;
  int h = 1, ent = ent_of_human(h);
  g_online = 1;
  g_humans_on[h] = 1;
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->prev_pos = p->pos;
  p->vel = (v3){{0.0f, 0.0f, 0.0f}};
  p->alive = 1; p->grounded = 1; p->hp = 100;
  p->wp.cur = WPN_SR;
  anim_reset(&g_player_anim[h], p->pos, p->yaw, p->wp.cur);
  // Kill during a real, non-zero shoulder response. A calm counterfactual proves
  // that the pose being copied actually contains recoil rather than merely equal
  // zero-initialized fields; the corpse then has to receive that solved pose and all
  // three spring values bit-for-bit.
  anim_t *ha = &g_player_anim[h];
  ha->recoil_back = 0.024f;
  ha->recoil_lift = 0.018f;
  ha->recoil_side = -0.0025f;
  skel_solve(ha, p->pos, p->yaw, p->pitch, p->wp.cur, 0, SKEL_POSE_ONLY);
  pose_t handover_pose = ha->pose;
  float handover_back = ha->recoil_back;
  float handover_lift = ha->recoil_lift;
  float handover_side = ha->recoil_side;
  anim_t calm = *ha;
  calm.recoil_back = calm.recoil_lift = calm.recoil_side = 0.0f;
  skel_solve(&calm, p->pos, p->yaw, p->pitch, p->wp.cur, 0, SKEL_POSE_ONLY);
  float recoil_pose_delta = v3_len(v3_sub(handover_pose.gun_o, calm.pose.gun_o)) +
                            v3_len(v3_sub(handover_pose.wri[0], calm.pose.wri[0])) +
                            v3_len(v3_sub(handover_pose.wri[1], calm.pose.wri[1]));
  g_cl_alive_prev[ent] = 1;
  // The wound the server would have sent: a round from the front, chest
  // high — the EV_HIT* the client latches in g_wound.
  g_wound[ent] = (typeof(g_wound[0])){
      v3_add(p->pos, (v3){{0.0f, 1.10f, 0.0f}}),
      v3_add(p->pos, (v3){{0.0f, 1.50f, 4.0f}}), g_tick, 46, 0};
  p->alive = 0;              // <- the snapshot flag going 0, nothing else
  client_anim_all();         // the tick that has to notice it
  int born = 0, recoil_handover = 0; v3 p0 = {{0, 0, 0}};
  for (int c = 0; c < MAX_CORPSES; c++) {
    const corpse_t *cp = &g_corpses[c];
    if (!cp->used) continue;
    born++; p0 = cp->pos;
    if (cp->owner == ent && cp->born == (int)g_tick)
      recoil_handover = !memcmp(&cp->anim.pose, &handover_pose,
                                sizeof handover_pose) &&
                          cp->anim.recoil_back == handover_back &&
                          cp->anim.recoil_lift == handover_lift &&
                          cp->anim.recoil_side == handover_side;
  }
  // ...and it has to FLY. A corpse that is created and never integrated is
  // the same bug one layer down, and it looks identical in a still frame.
  for (int i = 0; i < 240; i++) { g_tick++; vfx_tick(); }
  int live = 0, gnd = 0; float moved = 0.0f, tilt = 0.0f;
  for (int c = 0; c < MAX_CORPSES; c++)
    if (g_corpses[c].used) {
      live++;
      v3 d = v3_sub(g_corpses[c].pos, p0);
      moved = sqrtf(v3_dot(d, d));
      gnd = g_corpses[c].rag.grounded;
      tilt = g_corpses[c].rag.tilt;
    }
  // AND THE OTHER HALF: a bot dies, then RESPAWNS. Offline bot_spawn's memset clears
  // the ragdoll; online nothing does, and scene_draw_bot feeds b->rag through rag_death
  // for a LIVING bot too — so the bot stood back up still collapsed on the floor.
  g_match.nbots = 1;
  bot_t *bt = &g_bots[0];
  memset(bt, 0, sizeof *bt);
  bt->active = 1; bt->alive = 1; bt->hp = 100;
  bt->pos = (v3){{2.0f, 0.0f, 6.0f}};
  bt->prev_pos = bt->pos;
  anim_reset(&bt->anim, bt->pos, bt->yaw, bt->wp.cur);
  g_cl_body[1] = 0;                   // nobody owns this body yet
  g_wound[1] = (typeof(g_wound[0])){
      v3_add(bt->pos, (v3){{0.0f, 1.10f, 0.0f}}),
      v3_add(bt->pos, (v3){{0.0f, 1.50f, 4.0f}}), g_tick, 46, 0};
  bt->alive = 0;                      // the snapshot says he is down
  for (int i = 0; i < 60; i++) { g_tick++; client_anim_all(); }
  float dead_tilt = bt->rag.tilt, dead_top = bt->topple;
  bt->alive = 1;                      // ...and now the snapshot respawns him
  bt->pos = (v3){{-3.0f, 0.0f, -2.0f}};
  client_anim_all();
  float up_tilt = bt->rag.tilt, up_topple = bt->topple;
  printf("netdeath bot dead_tilt=%.2f dead_topple=%.2f "
         "up_tilt=%.2f up_topple=%.2f\n",
         (double)dead_tilt, (double)dead_top,
         (double)up_tilt, (double)up_topple);
  printf("netdeath corpses=%d live=%d moved=%.3f tilt=%.2f grounded=%d queue=%d\n",
         born, live, (double)moved, (double)tilt, gnd, g_player_corpse_q[h]);
  printf("netdeath recoil pose_delta=%.1fmm handover=%d\n",
         (double)(recoil_pose_delta * 1000.0f), recoil_handover);
  // THE OTHER HALF OF "IT LOOKS BROKEN" IS THE RENDER LERP, and no state dump can see
  // it. scene_draw_bot lerps prev_topple->topple and prev_yaw->yaw by the SUB-TICK
  // alpha, and alpha is re-drawn every rendered frame.
  memset(bt, 0, sizeof *bt);
  bt->active = 1; bt->alive = 1; bt->hp = 100;
  bt->pos = bt->prev_pos = (v3){{2.0f, 0.0f, 6.0f}};
  anim_reset(&bt->anim, bt->pos, bt->yaw, bt->wp.cur);
  g_cl_body[1] = 0;
  // TWO snapshots, because one delta is legitimate: after the second the prev_ pair
  // must hold the FIRST one's heading, not the slot's birth value.
  net_ent_t se = {.id = 1, .flags = 1 | 2, .hp = 100,
                  .x = net_q_pos(bt->pos.x), .y = net_q_pos(bt->pos.y),
                  .z = net_q_pos(bt->pos.z), .yaw = net_q_yaw(2.4f)};
  net_cl_apply_ent(&se);
  se.yaw = net_q_yaw(2.5f);
  net_cl_apply_ent(&se);
  float yawgap = fabsf(bt->yaw - bt->prev_yaw);
  bt->alive = 0;                      // ...and down he goes again
  for (int i = 0; i < 60; i++) { g_tick++; client_anim_all(); }
  float sl0 = rag_death(&bt->rag, bt->prev_topple, bt->topple, 0.0f).slack;
  float sl1 = rag_death(&bt->rag, bt->prev_topple, bt->topple, 1.0f).slack;
  // THE POSITION PAIR IS THE THIRD ONE, and it needs a snapshot to show: the server
  // ragdolls its dead bots too and sends them, so this is the record that used to drag
  // the settled body onto a second solution 5 m away while prev_pos held the first — a
  // full-amplitude swing on every rendered frame. posgap is one TICK of corpse motion
  // (0 on a settled one); before the fix it read the whole distance between the two
  // integrators' resting places.
  v3 settled = bt->pos;
  se.flags = 0;                       // still down, and the server says so
  se.x = net_q_pos(settled.x + 5.0f);
  net_cl_apply_ent(&se);
  client_anim_all();
  v3 d = v3_sub(bt->pos, bt->prev_pos);
  v3 dw = v3_sub(bt->pos, settled);
  // JOINING ONTO A BODY THAT IS ALREADY DOWN. There is no death edge to see here, so
  // the launch has to fire on the STATE, and until it does the SERVER owns the position
  // — read as an edge, this client integrated a zeroed rag_t from a position no
  // snapshot was allowed to write and laid the body out at the world origin. `join` is
  // how far the body ends up from where the server put it.
  memset(bt, 0, sizeof *bt);
  memset(g_cl_body, 0, sizeof g_cl_body);
  bt->active = 1; bt->alive = 0;
  se.flags = 0; se.hp = 0;
  se.x = net_q_pos(-12.0f); se.y = net_q_pos(0.0f); se.z = net_q_pos(9.0f);
  net_cl_apply_ent(&se);
  client_anim_all();
  v3 jd = v3_sub(bt->pos, (v3){{-12.0f, 0.0f, 9.0f}});
  float join_dist = sqrtf(v3_dot(jd, jd));
  int join_owned = g_cl_body[1];
  printf("netdeath join dist=%.3f owned=%d\n",
         (double)join_dist, join_owned);
  float slackgap = fabsf(sl1 - sl0);
  float posgap = sqrtf(v3_dot(d, d));
  float drift = sqrtf(v3_dot(dw, dw));
  printf("netdeath lerp slackgap=%.3f yawgap=%.3f posgap=%.3f drift=%.3f\n",
         (double)slackgap, (double)yawgap, (double)posgap, (double)drift);

  // A snapshot respawn is a complete fresh life, not merely a clean VM. Build one
  // authoritative post-input record, poison every local follower the wire does not
  // carry, then drive the real decoder/reconciliation branch. The result must match
  // player_spawn plus the explicit authoritative fields, while lifetime shots, gun
  // RNG and the acknowledged button edge survive.
  int rh = 0, rent = ent_of_human(rh);
  player_t *rp = &g_players[rh];
  player_spawn(rp);
  rp->pos = rp->prev_pos = (v3){{4.0f, 0.60f, -3.0f}};
  rp->vel = (v3){{0.24f, 0.35f, -0.16f}};
  rp->yaw = 0.70f; rp->pitch = -0.20f;
  rp->grounded = rp->prev_grounded = 0;
  rp->crouched = 1; rp->tuck_y = HEIGHT_STAND - HEIGHT_CROUCH;
  rp->lean = 0.37f;
  rp->hp = 87;
  rp->wp.cur = WPN_SR;
  rp->wp.ammo[WPN_SR] = 3;
  rp->wp.ads_want = rp->wp.ready_want = 1;
  g_spawn_seq[rent] = 8;
  net_ent_t respawn_ent;
  net_ent_from_live(&respawn_ent, rent);
  respawn_ent.spawn_seq = 9;
  v3 auth_pos = {{net_dq_pos(respawn_ent.x), net_dq_pos(respawn_ent.y),
                  net_dq_pos(respawn_ent.z)}};
  v3 auth_vel = {{net_dq_vel(respawn_ent.vx), net_dq_vel(respawn_ent.vy),
                  net_dq_vel(respawn_ent.vz)}};
  float auth_yaw = net_dq_yaw(respawn_ent.yaw);
  float auth_pitch = net_dq_pitch(respawn_ent.pitch);
  float auth_lean = net_dq_wish(respawn_ent.lean);

  // Dead-life residue: every one of these values used to cross a spawn-sequence edge
  // because the old branch cleared only weapon_t/vm_t.
  rp->alive = 0;
  rp->hp = 0;
  rp->pos = rp->prev_pos = (v3){{-8.0f, 1.7f, 11.0f}};
  rp->vel = (v3){{-9.0f, 5.0f, 7.0f}};
  rp->eye = 0.35f; rp->prev_eye = 0.41f;
  rp->grounded = 1; rp->prev_grounded = 0;
  rp->crouched = 0; rp->sliding = 1;
  rp->slide_cd = 71; rp->slide_queued = 1; rp->jump_buf = 5;
  rp->tuck_y = 0.17f;
  rp->fov_kick = 0.8f; rp->prev_fov_kick = 0.7f;
  rp->land_dip = 0.09f; rp->prev_land_dip = 0.08f;
  rp->lean_in = -0.8f; rp->lean_clr = 0.2f;
  rp->lean = -0.16f; rp->prev_lean = -0.14f;
  for (int b = 0; b < IN_COUNT; b++) rp->prev_held[b] = b & 1;
  rp->shots = 41;
  rp->respawn_t = 77; rp->flash = 12; rp->flash_head = 1;
  memset(&rp->wp, 0xa5, sizeof rp->wp);
  rp->wp.cur = WPN_AR;
  rp->vm = (vm_t){.sway_p = 0.08f, .lag_x = -0.02f,
                  .kick_back = 0.021f, .kick_back_v = -0.7f,
                  .kick_lift = 0.012f, .kick_lift_v = 0.4f,
                  .kick_side = -0.003f, .kick_side_v = 0.2f,
                  .roll = 0.04f, .last_eye = 0.35f};
  memset(&g_cl, 0, sizeof g_cl);
  g_local_ent = rent;
  g_online = 1;
  g_humans_on[rh] = 1;
  g_cl.state = NC_PLAYING;
  g_cl.my_ent = rent;
  g_cl.synced = 1;
  g_cl.sync_seq = 77;
  g_cl.seq = 78;
  g_cl.have_spawn_seq = 1;
  g_cl.spawn_seq = 8;
  uint16_t ack_buttons = (uint16_t)((1u << IN_FIRE) | (1u << IN_CROUCH) |
                                    (1u << IN_ADS));
  g_cl.inlog[77 % NET_HIST] =
      (net_input_t){.tick = 77, .buttons = ack_buttons,
                    .yaw = respawn_ent.yaw, .pitch = respawn_ent.pitch};
  memset(&g_cl_pred[77 % NET_HIST], 0x5a,
         sizeof g_cl_pred[77 % NET_HIST]);
  uint64_t respawn_rng = 0x9e3779b97f4a7c15ull;
  g_rng_gun[rh] = respawn_rng;
  unsigned char snap[128];
  size_t snap_len = netdeath_snapshot(snap, sizeof snap, 90, 77, &respawn_ent);
  net_cl_snapshot(snap, (int)snap_len);

  weapon_t fresh_wp = {0};
  wpn_fill_mags(&fresh_wp);
  fresh_wp.cur = WPN_SR;
  fresh_wp.ammo[WPN_SR] = respawn_ent.mag;
  fresh_wp.ads_want = fresh_wp.ready_want = 1;
  vm_t fresh_vm = {0};
  fresh_vm.last_eye = EYE_STAND - (HEIGHT_STAND - HEIGHT_CROUCH);
  int buttons_ok = 1;
  for (int b = 0; b < IN_COUNT; b++)
    buttons_ok &= rp->prev_held[b] == !!(ack_buttons & (1u << b));
  int fresh_motion = !memcmp(&rp->pos, &auth_pos, sizeof auth_pos) &&
                     !memcmp(&rp->prev_pos, &auth_pos, sizeof auth_pos) &&
                     !memcmp(&rp->vel, &auth_vel, sizeof auth_vel) &&
                     rp->yaw == auth_yaw && rp->pitch == auth_pitch &&
                     !rp->grounded && !rp->prev_grounded && rp->crouched &&
                     !rp->sliding && rp->slide_cd == 0 && !rp->slide_queued &&
                     rp->jump_buf == 0 &&
                     rp->tuck_y == HEIGHT_STAND - HEIGHT_CROUCH &&
                     rp->eye == fresh_vm.last_eye && rp->prev_eye == fresh_vm.last_eye &&
                     rp->fov_kick == 0.0f && rp->prev_fov_kick == 0.0f &&
                     rp->land_dip == 0.0f && rp->prev_land_dip == 0.0f;
  int fresh_lean = rp->lean_in == auth_lean && rp->lean == auth_lean &&
                   rp->prev_lean == auth_lean && rp->lean_clr == 1.0f;
  int fresh_life = rp->alive && rp->hp == 87 && rp->respawn_t == 0 &&
                   rp->flash == 0 && rp->flash_head == 0 && rp->shots == 41 &&
                   buttons_ok && g_rng_gun[rh] == respawn_rng;
  int respawn_wp = !memcmp(&rp->wp, &fresh_wp, sizeof fresh_wp);
  int respawn_vm = !memcmp(&rp->vm, &fresh_vm, sizeof fresh_vm);
  int respawn_fresh = snap_len > 0 && fresh_motion && fresh_lean && fresh_life &&
                      respawn_wp && respawn_vm;
  printf("netdeath respawn motion=%d lean=%d life=%d wp=%d vm=%d "
         "buttons=%d rng=%d last_eye=%.3f\n",
         fresh_motion, fresh_lean, fresh_life, respawn_wp, respawn_vm,
         buttons_ok, g_rng_gun[rh] == respawn_rng, (double)rp->vm.last_eye);

  // The harder spawn-sequence case has no death edge and no positional error: a
  // dropped death snapshot or a match restart can leave the local slot alive exactly
  // where the new spawn lands. spawn_seq itself must force the fresh-life branch;
  // neither the >2 m desync threshold nor !alive may be its accidental trigger.
  respawn_ent.spawn_seq = 10;
  rp->alive = 1; rp->hp = 5;
  rp->pos = rp->prev_pos = auth_pos;
  rp->vel = (v3){{7.0f, -3.0f, 4.0f}};
  rp->eye = 0.44f; rp->prev_eye = 0.39f;
  rp->grounded = rp->prev_grounded = 1;
  rp->crouched = 0; rp->sliding = 1; rp->slide_cd = 19;
  rp->lean_in = -0.9f; rp->lean_clr = 0.1f;
  rp->lean = rp->prev_lean = -0.09f;
  rp->fov_kick = rp->land_dip = 0.6f;
  rp->flash = 9; rp->flash_head = 1; rp->respawn_t = 33;
  rp->shots = 42;
  memset(&rp->wp, 0xa5, sizeof rp->wp);
  rp->wp.cur = WPN_AR;
  rp->vm = (vm_t){.sway_y = -0.07f, .kick_back = 0.03f,
                  .kick_lift = 0.02f, .last_eye = 0.44f};
  for (int b = 0; b < IN_COUNT; b++) rp->prev_held[b] = !(b & 1);
  g_cl.spawn_seq = 9;
  g_cl.have_spawn_seq = 1;
  g_cl.predpos[77 % NET_HIST] = auth_pos;
  int live_zero_err = !memcmp(&rp->pos, &auth_pos, sizeof auth_pos) &&
                      !memcmp(&g_cl.predpos[77 % NET_HIST], &auth_pos,
                              sizeof auth_pos);
  uint64_t live_edge_rng = 0xd1b54a32d192ed03ull;
  g_rng_gun[rh] = live_edge_rng;
  int corr_before = g_cl_corrections;
  size_t live_snap_len = netdeath_snapshot(snap, sizeof snap, 91, 77,
                                           &respawn_ent);
  net_cl_snapshot(snap, (int)live_snap_len);
  int live_buttons_ok = 1;
  for (int b = 0; b < IN_COUNT; b++)
    live_buttons_ok &= rp->prev_held[b] == !!(ack_buttons & (1u << b));
  int live_edge_fresh = live_zero_err && live_snap_len > 0 &&
                        rp->alive && rp->hp == 87 &&
                        rp->shots == 42 && g_rng_gun[rh] == live_edge_rng &&
                        live_buttons_ok && g_cl_corrections == corr_before &&
                        !memcmp(&rp->pos, &auth_pos, sizeof auth_pos) &&
                        !memcmp(&rp->prev_pos, &auth_pos, sizeof auth_pos) &&
                        !memcmp(&rp->vel, &auth_vel, sizeof auth_vel) &&
                        rp->yaw == auth_yaw && rp->pitch == auth_pitch &&
                        !rp->grounded && !rp->prev_grounded && rp->crouched &&
                        !rp->sliding && rp->slide_cd == 0 &&
                        rp->tuck_y == HEIGHT_STAND - HEIGHT_CROUCH &&
                        rp->eye == fresh_vm.last_eye &&
                        rp->lean == auth_lean && rp->prev_lean == auth_lean &&
                        rp->lean_in == auth_lean && rp->lean_clr == 1.0f &&
                        !rp->flash && !rp->flash_head && !rp->respawn_t &&
                        !memcmp(&rp->wp, &fresh_wp, sizeof fresh_wp) &&
                        !memcmp(&rp->vm, &fresh_vm, sizeof fresh_vm);
  printf("netdeath respawn_live zero_err=%d fresh=%d buttons=%d rng=%d "
         "corr=%d/%d\n",
         live_zero_err, live_edge_fresh, live_buttons_ok,
         g_rng_gun[rh] == live_edge_rng, corr_before, g_cl_corrections);

  int handover_ok = recoil_pose_delta >= 0.010f && recoil_handover;
  int death_ok = dead_tilt > 0.5f && dead_top > 0.5f &&
                 fabsf(up_tilt) < 1e-6f && fabsf(up_topple) < 1e-6f &&
                 born == 1 && live == 1 && moved > 0.10f && tilt > 0.5f &&
                 gnd && !g_player_corpse_q[h] &&
                 join_dist < 0.10f && join_owned &&
                 slackgap < 0.01f && yawgap > 0.05f && yawgap < 0.15f &&
                 posgap < 0.10f && drift < 0.10f;
  int ok = death_ok && handover_ok && respawn_fresh && live_edge_fresh;
  printf("netdeath death=%d recoil=%d respawn=%d %s\n",
         death_ok, handover_ok, respawn_fresh, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// Random and garbage packets into every decoder: no crash, and every accepted
// record in bounds. Run it under ASan.
static void netfuzz_proof(void) {
  // Random and bit-flipped bytes into EVERY decoder — no crash, no wild read. netpack
  // proves the roundtrip of WELL-FORMED output; a public port never gets well-formed
  // output.
  long n = next_n();
  uint64_t rs = 0xf00dface ^ (uint64_t)n * 40503u;
  long accepted_in = 0, accepted_ent = 0, accepted_tele = 0;
  for (long i = 0; i < n; i++) {
    unsigned char buf[200];
    int len = (int)(rng_next(&rs) % (sizeof buf + 1));   // 0..200, incl. short
    for (int b = 0; b < len; b++) buf[b] = (unsigned char)rng_next(&rs);
    // Half the packets get a VALID header so the snapshot/scores decoders
    // run past their magic check into the section parsing — pure random
    // bytes almost never match PROTO+type and the deep paths went unfuzzed.
    if ((i & 1) && len >= 3) {
      buf[0] = NET_PROTO & 0xff; buf[1] = NET_PROTO >> 8;
      buf[2] = (i & 2) ? NP_SNAPSHOT : NP_SCORES;
    }
    // NP_TELE share (every 4th packet). Proto, type AND LENGTH are all
    // forced: net_tele_parse's first test is `n != NET_TELE_LEN`, and a
    // random 0..200 length hits 23 about once in 200 — without the length
    // the field clamps never execute at all.
    // It gets its OWN buffer and its OWN stream on purpose. Forcing
    // len on the shared `buf` would (a) truncate a quarter of the packets
    // to 23 bytes — and (i&3)==3 is exactly HALF the NP_SNAPSHOT share
    // this loop's own comment says was added to reach the deep paths —
    // and (b) read the tail of `buf` past whatever length was drawn, which
    // was never written. The separate stream also leaves in_accepted /
    // ent_accepted byte-identical, so a future netfuzz diff still means
    // what it says.
    if ((i & 3) == 3) {
      // NET_TELE_LEN + 1 bytes, deliberately: the over-length probe below hands the
      // parser 24, and a 23-byte local would be an out-of-bounds read the moment anyone
      // moves the header check above the length check.
      unsigned char tb[NET_TELE_LEN + 1];
      uint64_t trs = rs ^ 0x9e3779b97f4a7c15ull;
      for (int b = 0; b < NET_TELE_LEN + 1; b++) tb[b] = (unsigned char)rng_next(&trs);
      tb[0] = NET_PROTO & 0xff; tb[1] = NET_PROTO >> 8;
      tb[2] = NP_TELE;
      // HALF of the share also lands kind/mode IN RANGE. Random enum bytes survive
      // `kind > 1 || mode > 2` about once in 11000, so a share that left them alone
      // would report tele_accepted=0..2 over 20000 packets and never once run the os/dt
      // clamps or the version sanitizer — the trivial zero this whole share exists to
      // avoid.
      if (i & 4) { tb[7] &= 1; tb[8] = (unsigned char)(tb[8] % 3); }
      net_tele_t tf;
      if (net_tele_parse(tb, NET_TELE_LEN, &tf)) {
        accepted_tele++;
        // Everything the server then prints must be in bounds: mode and os
        // INDEX string tables, dt is clamped, and the version may carry
        // neither a space nor an '=' or it injects fields into server.log.
        if (tf.kind > 1 || tf.mode > 2 || tf.os > 1 ||
            tf.dt > NET_TELE_DT_MAX || tf.ver[12])
          accepted_tele = -1000000;
        for (int k = 0; k < 12; k++)
          if (tf.ver[k] == ' ' || tf.ver[k] == '=') accepted_tele = -1000000;
      }
      // A wrong length must be refused whatever the header says — the one
      // check every other field's safety rests on.
      if (net_tele_parse(tb, NET_TELE_LEN - 1, &tf) ||
          net_tele_parse(tb, NET_TELE_LEN + 1, &tf)) accepted_tele = -1000000;
    }
    np_rd r = {buf, (size_t)len, 0, 0};
    net_input_t in;
    if (net_input_decode(&r, &in)) accepted_in++;
    np_rd r2 = {buf, (size_t)len, 0, 0};
    net_ent_t ne;
    if (net_ent_decode(&r2, &ne)) { accepted_ent++;
      if (ne.id >= MAX_ENTS || ne.weapon >= WPN_COUNT) accepted_ent = -1000000; }
    // The scores reader takes the same hostility: garbage must neither
    // crash nor hand back a row whose id could index out of bounds.
    net_score_t sc[MAX_ENTS]; net_name_t nmf[MAX_HUMANS];
    uint8_t ov, wi; uint16_t fr; int nnf = 0;
    int nsc = net_scores_read(buf, (size_t)len, &ov, &wi, &fr, sc, MAX_ENTS,
                              nmf, MAX_HUMANS, &nnf);
    for (int k = 0; k < nsc; k++)
      if (sc[k].id >= MAX_ENTS) accepted_ent = -1000000;
    for (int k = 0; k < nnf; k++)
      if (nmf[k].id >= MAX_ENTS || nmf[k].name[11]) accepted_ent = -1000000;
    // The snapshot reader drives net_ent_decode AND net_ev_decode with
    // attacker-controlled counts; net_ev_to_live is the ONE guard between
    // a hostile event record and an array index (g_player_anim, WPN_DEF,
    // g_bots) — both used to be outside this battery entirely.
    uint32_t fst, fli, fwh, fsd; uint8_t fqd; int ffull, fnev = 0;
    net_ent_t fents[MAX_ENTS]; net_ev_t fevs[NET_EV_SNAP_MAX];
    int fcnt = net_snapshot_read(buf, (size_t)len, &fst, &fli, &fwh, &fsd,
                                 &fqd, fents, MAX_ENTS, &ffull,
                                 fevs, NET_EV_SNAP_MAX, &fnev);
    for (int k = 0; k < fcnt; k++)
      if (fents[k].id >= MAX_ENTS || fents[k].weapon >= WPN_COUNT)
        accepted_ent = -1000000;
    for (int k = 0; k < fnev; k++) {
      event_t le;
      net_ev_to_live(&le, &fevs[k]);
      if (le.src >= MAX_ENTS || le.dst >= MAX_ENTS ||
          le.wpn < 0 || le.wpn >= WPN_COUNT || le.type > EV_LAND)
        accepted_ent = -1000000;
    }
    // Cookie validation must never index or trap on garbage inputs.
    (void)net_cookie_valid(rng_next(&rs), (uint16_t)rng_next(&rs),
                           rng_next(&rs), (uint32_t)i);
  }
  printf("netfuzz n=%ld no_crash=ok in_accepted=%ld ent_accepted=%ld "
         "tele_accepted=%ld "
         "(all accepted ent ids < MAX_ENTS, weapon < WPN_COUNT; "
         "tele_accepted is n/8 BY CONSTRUCTION - only -1 is a defect)\n",
         n, accepted_in, accepted_ent < 0 ? -1 : accepted_ent,
         accepted_tele < 0 ? -1 : accepted_tele);
}
