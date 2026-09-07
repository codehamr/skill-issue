// =============================================================================
// harness/proofs_net.inc  —  network/recoil/vmframe, canonical defaults and figure adaptation proofs.
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
  int connected = 0;
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
        if (g_cl.state != NC_PLAYING) break;
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
      connected = g_cl.state == NC_PLAYING && g_cl.synced && !g_cl.reject;
      printf("netclient %s ent=%d acts=%s world=%08x server_tick=%u "
             "corrections=%d worst_err=%.1fmm ev_applied=%d ev_dupes=%d "
             "ev_high=%u hp=%d alive=%d pred_ammo=%d pos=(%.2f %.2f %.2f) "
             "ping=%d qdepth=%.1f kills=%d deaths=%d shots=%d hits=%d "
             "fraglimit=%d reject=%d\n",
             connected ? "CONNECTED" : "FAILED", g_cl.my_ent, actbuf,
             g_cl.world, g_cl.server_tick, g_cl_corrections,
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
  if (!connected) exit(1);
}

// --- The carriage round trip -------------------------------------------------
// net_ent_from_live's contract, taken literally: "presentation is deliberately absent
// — the client re-solves it" holds only while every INPUT that solver needs is on the
// wire. The aim blend, carry state, mid-air leg tuck and trigger/recenter intent are
// the values a snapshot
// used to drop on the floor, so every remote figure stood in the low carry forever
// while its (correctly synced) pitch pointed somewhere else entirely.
//
// One entity goes live -> from_live -> encode -> decode -> apply_ent, the carriage is
// wiped in between the way a client that knows nothing has it, and what comes out the
// far side has to be what went in. Then the client's own ramp runs the SAME number of
// ticks the server ran and has to land on the same blend.
typedef struct { int fire_want, ads_want, ready_want; float ads_s, tuck; } carry_t;

static carry_t netcarry_read(int ent) {
  int h = human_of_ent(ent);
  const weapon_t *w = h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp;
  float tuck = h >= 0 ? g_players[h].tuck_y : 0.0f;
  return (carry_t){w->fire_want, w->ads_want, w->ready_want, w->ads_s, tuck};
}

// Erase everything the wire is supposed to carry, so a field that survives by
// ACCIDENT (this is one process — server and client share the globals) cannot pass.
static void netcarry_wipe(int ent) {
  int h = human_of_ent(ent);
  weapon_t *w = h >= 0 ? &g_players[h].wp : &g_bots[ent - 1].wp;
  w->fire_want = w->ads_want = w->ready_want = 0;
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
  int ok = decoded && cl.fire_want == srv.fire_want && cl.ads_want == srv.ads_want &&
           cl.ready_want == srv.ready_want &&
           fabsf(cl.tuck - srv.tuck) < 1e-4f && dads < 0.02f && pose_ok;
  printf("netcarry %s ent=%d fire=%d/%d ads_want=%d/%d ready=%d/%d tuck=%.3f/%.3f "
         "ads_s=%.3f/%.3f d=%.4f pose_ready=%.2f pose_ads=%.2f %s\n",
         label, ent, srv.fire_want, cl.fire_want, srv.ads_want, cl.ads_want,
         srv.ready_want, cl.ready_want,
         (double)srv.tuck, (double)cl.tuck, (double)srv.ads_s, (double)cl.ads_s,
         (double)dads, (double)pose_ready, (double)pose_ads, ok ? "ok" : "FAIL");
  return ok;
}

static void netcarry_proof(void) {
  g_online = 1;
  g_local_ent = 0;               // human 0 is us; everything below is FOREIGN
  g_cl.my_ent = 0;
  g_cl.synced = 1;
  // A HUMAN WHO IS FIRING, AIMING, FIGHTING AND TUCKED — all intent bits at once.
  int h = 1, ent = ent_of_human(h);
  g_humans_on[h] = 1;
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 0;   // airborne: the tuck is legal
  p->pitch = 0.42f;
  p->tuck_y = HEIGHT_STAND - HEIGHT_CROUCH;
  p->wp.ads_want = 1;
  p->wp.fire_want = 1;
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
  b->wp.fire_want = 1;
  b->wp.ready_want = 1;
  anim_reset(&b->anim, b->pos, b->yaw, b->wp.cur);
  ok &= netcarry_case("bot-ads", 1, 12);

  // HIP FIRE — READY WITHOUT ADS, and it is the case that actually tests the READY
  // bit. anim_tick raises the carry on `ready || ads > 0.05`, so with the rifle at the
  // eye the ready argument is redundant and a client that dropped it still looks
  // right; a man shooting from the hip at four metres is where the dropped bit shows.
  p->wp.ads_want = 0;
  p->wp.fire_want = 0;
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
  float back1, side1;
} netfigure_fire_t;

// A foreign shot arrives with the authoritative aim direction, independently of its
// receiver's patrol carry. At T0 the live rifle, event anchor and tracer share that
// direction. Cosmetic compression and lateral recoil begin on the next presentation
// tick; angular recoil belongs to the gameplay barrel alone.
static netfigure_fire_t netfigure_fire_case(int ent, int wpn) {
  int h = human_of_ent(ent);
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 1;
  p->yaw = 0.7f; p->pitch = -0.05f;
  p->wp.cur = wpn;
  p->wp.ads_want = p->wp.ready_want = 0;   // settled patrol intent
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
  v3 slung_axis = v3_norm(a->pose.gun_f);
  float patrol_down = (p->pitch - asinf(f_clamp(slung_axis.y, -1, 1))) / DEG2RAD;
  float patrol_cross = fabsf(wrap_pi(atan2f(slung_axis.x, -slung_axis.z) - p->yaw)) / DEG2RAD;
  // Before the first round recoil is zero, so the ballistic axis is the analytic
  // aim axis. A lowered patrol mesh is presentation and cannot author a shot direction.
  v3 fire_dir = {{sinf(p->yaw) * cosf(p->pitch), sinf(p->pitch),
                  -cosf(p->yaw) * cosf(p->pitch)}};
  v3 fire_eye = v3_add(p->pos, (v3){{0.0f, EYE_STAND, 0.0f}});

  // No old tracer or queued event may satisfy this case accidentally. Clearing VFX
  // also makes the one live tracer below an unambiguous record of the anchor actually
  // consumed by vfx_ev_fire, rather than an inferred copy of it.
  vfx_match_reset();
  g_num_events = 0;
  int shot = 7 + wpn;
  ev_push((event_t){.type = EV_FIRE, .src = ent, .wpn = wpn, .i = shot,
                    .a = v3_add(fire_eye, v3_scale(fire_dir, 20.0f)),
                    .b = fire_dir,
                    .f = 20.0f, .g = p->wp.ads_s});

  int ti = -1;
  for (int i = 0; i < MAX_TRACERS; i++)
    if (g_tracers[i].life > 0 && g_tracers[i].src == ent) { ti = i; break; }
  v3 muz0 = pose_muzzle(&a->pose, wpn);
  int staged = g_fire_anchor_src == ent && g_fire_anchor_tick == g_tick;
  v3 event_muz0 = staged ? pose_muzzle(&g_fire_anchor_pose, wpn) : muz0;
  v3 tracer = ti >= 0 ? g_tracers[ti].a : event_muz0;
  // The foreign event must solve its anchor before VFX consumes it. Anchor, live
  // muzzle and tracer are measured independently so a fallback origin or a stale
  // patrol muzzle cannot satisfy the shot identity.
  float anchor0 = v3_len(v3_sub(tracer, event_muz0));
  float visible0 = v3_len(v3_sub(muz0, muz_slung));
  float axis0 = staged ? netfigure_pose_bore_off(&g_fire_anchor_pose,
                                                  p->yaw, p->pitch)
                       : 180.0f;
  float live_axis0 = v3_len(v3_sub(v3_norm(a->pose.gun_f), fire_dir));
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

  // T0 reposes the patrol rifle to the actual shot while keeping spring displacement
  // zero. T1 carries the gameplay recoil and the first bounded cosmetic displacement.
  // Both authored lift channels are zero: a second muzzle-up spring would duplicate
  // the immediate gameplay recoil already reconstructed from this event.
  int zero0 = fabsf(back0) < 1e-6f && fabsf(lift0) < 1e-6f &&
              fabsf(side0) < 1e-6f;
  int impulse0 = backv0 > 0.5f && liftv0 == 0.0f && fabsf(sidev0) > 0.02f;
  int moved1 = back1 > 0.004f && lift1 == 0.0f && fabsf(side1) > 0.0001f;
  // Both patrol rifles visibly leave the combat axis and have a distinct muzzle.
  // AR additionally carries down and across the chest, rather than satisfying the
  // off-axis distance with a sideways-only or vertical-only rotation.
  int patrol = slung > 20.0f && raised > 0.20f &&
               (wpn != WPN_AR || (patrol_down > 15.0f && patrol_cross > 20.0f));
  float rec_pitch = fmaxf(g_sim_wp[wpn][WP_RECOIL_UP] -
                          g_sim_gun[GP_RECENTER] * TICK_DT, 0.0f);
  float rec_yaw = fmaxf(g_sim_wp[wpn][WP_RECOIL_SIDE] -
                        g_sim_gun[GP_RECENTER] * TICK_DT, 0.0f);
  float want_axis1 = sqrtf(rec_pitch * rec_pitch + rec_yaw * rec_yaw);
  int ok = ti >= 0 && staged && ready_slung < 0.05f && patrol &&
           fabsf(visible0 - raised) < 0.002f && latch0 > 0.9f &&
           anchor0 < 0.002f && axis0 < 0.25f && live_axis0 < 0.004f &&
           zero0 && impulse0 && moved1 && ready1 > 0.10f && ready1 < 0.30f &&
           toward0 < 0.002f && step1 > 0.002f && step1 < 0.12f &&
           fabsf(axis1 - want_axis1) < 0.25f;
  printf("netfigure fire=%s slung=%.2fdeg raise=%.2fm patrol_down=%.2fdeg patrol_cross=%.2fdeg | "
         "T0 anchor=%.2fmm hold=%.2fmm axis=%.2fdeg "
         "kick=(%.2fmm %.2fdeg %+.2fmm) "
         "vel=(%.2f %.2f %+.2f) latch=%.2f | "
         "T1 step=%.2fmm axis=%.2fdeg ready=%.3f "
         "kick=(%.2fmm %.2fdeg %+.2fmm) %s\n",
         wpn == WPN_SR ? "sr" : "ar", (double)slung, (double)raised,
         (double)patrol_down, (double)patrol_cross,
         (double)(anchor0 * 1000.0f), (double)(visible0 * 1000.0f),
         (double)axis0,
         (double)(back0 * 1000.0f), (double)(lift0 / DEG2RAD),
         (double)(side0 * 1000.0f), (double)backv0, (double)liftv0,
         (double)sidev0, (double)latch0, (double)(step1 * 1000.0f),
         (double)axis1, (double)ready1, (double)(back1 * 1000.0f),
         (double)(lift1 / DEG2RAD), (double)(side1 * 1000.0f),
         ok ? "ok" : "FAIL");
  return (netfigure_fire_t){ok, back1, fabsf(side1)};
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
  int ok_weapon = sr.back1 > ar.back1 * 1.4f && sr.side1 > ar.side1 * 1.2f;

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
  printf("weapon_delta back=%.2fx side=%.2fx | reload %d/%d end=%d "
         "arc=%.2f | flash=%d/%d flinch=%d expired=%d | own_reload=%d %s\n",
         (double)(sr.back1 / fmaxf(ar.back1, 1e-6f)),
         (double)(sr.side1 / fmaxf(ar.side1, 1e-6f)),
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
  long event_tick[MAX_EVENTS];
  unsigned char event_feedback[MAX_EVENTS];
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
  const float *sim_gun;
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
  memcpy(g_recoil_save.event_tick, g_event_tick, sizeof g_event_tick);
  memcpy(g_recoil_save.event_feedback, g_event_feedback, sizeof g_event_feedback);
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
  g_recoil_save.sim_gun = g_sim_gun;
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
  memcpy(g_event_tick, g_recoil_save.event_tick, sizeof g_event_tick);
  memcpy(g_event_feedback, g_recoil_save.event_feedback, sizeof g_event_feedback);
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
  g_sim_gun = g_recoil_save.sim_gun;
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
  g_sim_gun = g_cfg.gun;
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

// A CALM WEAPON TABLE FOR A FIXTURE MEASURING SOMETHING ELSE: the named columns are
// zeroed in a copy and g_sim_wp is pointed at it, so a reading is the standing geometry
// rather than a lottery on whichever breath phase or cone draw the run landed on. Two
// invariants ride along. The pointer is part of the recoil_save/restore set, so the
// arena gets its own table back when the proof ends; and a fixture is left pointing at
// THIS static until its next recoil_stage re-points it at g_cfg.wp, which is why the
// table outlives the call. Pass a negative column to zero only one.
static void recoil_wp_zero(int col_a, int col_b) {
  static float wp[WPN_COUNT][WP_COUNT];
  memcpy(wp, g_cfg.wp, sizeof wp);
  for (int i = 0; i < WPN_COUNT; i++) {
    if (col_a >= 0) wp[i][col_a] = 0.0f;
    if (col_b >= 0) wp[i][col_b] = 0.0f;
  }
  g_sim_wp = wp;
}

// Compact contract proof for the intentionally compact gun model.
static void recoil_proof(void) {
  recoil_save_globals();
  int ok = 1;
  float action_ref = -1.0f;

  for (int wi = 0; wi < WPN_COUNT; wi++) {
    recoil_stage(1, wi, 1);
    player_t *p = &g_players[0];
    p->grounded = p->prev_grounded = 1;
    p->sliding = 0;

    v3 before = gun_bore_dir(&p->wp, p->yaw, p->pitch);
    int ammo0 = p->wp.ammo[wi], ev0 = g_num_events;
    gun_shoot(0, &p->wp, player_eye_pos(p), p->yaw, p->pitch,
              p->grounded, p->sliding, &g_rng_gun[0]);

    const event_t *fire = NULL;
    for (int i = ev0; i < g_num_events; i++)
      if (g_events[i].type == EV_FIRE && g_events[i].src == 0) fire = &g_events[i];

    float up = g_sim_wp[wi][WP_RECOIL_UP];
    float side = g_sim_wp[wi][WP_RECOIL_SIDE];
    int first = fire && v3_dot(before, fire->b) > 0.999999f &&
                p->wp.ammo[wi] == ammo0 - 1 &&
                fabsf(p->wp.rec_p - up) < 1e-6f &&
                fabsf(p->wp.rec_y + side) < 1e-6f;

    weapon_t alt = {.cur = wi};
    gun_recoil_kick(&alt, 0, wi);
    gun_recoil_kick(&alt, 1, wi);
    int alternate = fabsf(alt.rec_p - 2.0f * up) < 1e-6f &&
                    fabsf(alt.rec_y) < 1e-6f;
    gun_recoil_kick(&alt, 2, wi);
    alternate &= fabsf(alt.rec_p - 3.0f * up) < 1e-6f &&
                 fabsf(alt.rec_y + side) < 1e-6f;

    float hold_p = p->wp.rec_p, hold_y = p->wp.rec_y;
    gun_recoil_step(&p->wp, 1);
    int held = p->wp.fire_want && p->wp.rec_p == hold_p && p->wp.rec_y == hold_y;
    float step = g_sim_gun[GP_RECENTER] * TICK_DT;
    gun_recoil_step(&p->wp, 0);
    float want_p = fmaxf(hold_p - step, 0.0f);
    float want_y = fabsf(hold_y) <= step ? 0.0f
                                         : hold_y - copysignf(step, hold_y);
    int linear = !p->wp.fire_want && p->wp.idx == 0 &&
                 fabsf(p->wp.rec_p - want_p) < 1e-6f &&
                 fabsf(p->wp.rec_y - want_y) < 1e-6f;
    int return_ticks = 1;
    while ((p->wp.rec_p != 0.0f || p->wp.rec_y != 0.0f) && return_ticks < 2000) {
      gun_recoil_step(&p->wp, 0);
      return_ticks++;
    }
    int zero = p->wp.rec_p == 0.0f && p->wp.rec_y == 0.0f;

    weapon_t sw = {.cur = wi};
    sw.ads_s = 0.0f;
    float hip = gun_spread(&sw, 1, 0);
    sw.ads_s = 0.5f;
    float half = gun_spread(&sw, 1, 0);
    sw.ads_s = 1.0f;
    float ground = gun_spread(&sw, 1, 0);
    float jump = gun_spread(&sw, 0, 0);
    float slide = gun_spread(&sw, 1, 1);
    int spread = fabsf(hip - g_sim_wp[wi][WP_HIP_SPREAD]) < 1e-6f &&
                 fabsf(half - hip * 0.5f) < 1e-6f &&
                 ground == 0.0f && fabsf(jump - slide) < 1e-6f &&
                 fabsf(jump - g_sim_gun[GP_ACTION_SPREAD]) < 1e-6f;
    if (action_ref < 0.0f) action_ref = jump;
    else spread &= jump == action_ref;

    int row = first && alternate && held && linear && zero && spread;
    printf("recoil %s first=%d alt=%d hold=%d linear=%d zero=%d ticks=%d "
           "spread=%.2f/%.2f/%.2f/%.2f %s\n",
           WPN_DEF[wi].prefix, first, alternate, held, linear, zero, return_ticks,
           (double)hip, (double)ground, (double)jump, (double)slide,
           row ? "ok" : "FAIL");
    ok &= row;
  }

  recoil_restore_globals();
  printf("recoil %s\n", ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

static void recoil_present_body(player_t *p) {
  p->wp.ready_want = (unsigned char)player_carry_ready(p);
  int idle_ok = gun_pose_state(&p->wp, &g_player_anim[0], p->vel,
                               p->grounded, p->sliding);
  weapon_anim_tick(&g_player_anim[0], p->pos, player_eye_pos(p), p->vel,
                   p->yaw, p->pitch, p->grounded, p->crouched && !p->sliding,
                   p->tuck_y, p->sliding, p->wp.ads_s, p->lean,
                   p->wp.ready_want, 0, &p->wp, idle_ok);
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
      gun_shoot(0, &p->wp, eye, p->yaw, p->pitch, p->grounded, p->sliding,
                &g_rng_gun[0]);
      int fired = p->wp.ammo[wi] != ammo0;
      int evs = g_num_events - ev0;
      int silent = p->wp.ammo[wi] == ammo0 && p->wp.cool[wi] == 0 &&
                   p->wp.idx == 0 && evs == 0 && g_rng_gun[0] == rng0 &&
                   p->wp.rec_p == 0.0f && p->wp.rec_y == 0.0f;
      // The tuck: run the real presentation path and read the carriage. A blocked
      // rifle must settle tucked with its rendered bore depressed; a clear one must
      // stay level on the aim axis. The barrel recovers alongside, exactly as
      // net_cl_present_advance recovers a figure nobody here simulates: the round
      // that just left added its fixed kick to the bore, and a carriage reading
      // taken before that has recovered is a reading of the shot, not of the tuck.
      for (int t = 0; t < 90; t++) {
        g_tick++;
        gun_recoil_step(&p->wp, 0);
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

// --- The reticle contract -----------------------------------------------------------
// Measured, not asserted: an AIMED round must arrive ON the eye line. The player aims
// with one axis and the only reading that settles the promise is where the round
// actually lands, so each case stands an impenetrable face in the round's way and
// reports the impact MINUS the ANALYTIC intersection of the eye ray with that same
// face. The hip rows measure the same geometry through the stance's cone and are
// graded against its size; the ADS rows are the promise itself, and their cone is
// zero by construction rather than by tolerance.
// TWO SWEEPS, because the two ways this can be wrong fail differently. THREE DISTANCES
// separate a PARALLEL offset (a muzzle that is not the eye) from an ANGULAR one (a
// barrel that is not the aim): the first is constant in d, the second grows with it.
// TWO AIMS put the level axis beside a tilted one, because on the level axis every
// sine is zero and every cosine is one — a swapped sin/cos or a flipped sign in the
// bore's own axis reads exactly zero there, and only the tilted pair holds that
// assignment honest. The face rides in the scratch slot past the generated count, so
// the reading is independent of the arena seed; both the count AND the slot's contents
// are restored (at MAX_SOLIDS the clamp overwrites a real solid, and restoring only
// the count would leave the synthetic face in the live arena for the rest of the run).
static int boresight_case(int wpn, int ads, float yaw, float pitch, float dist,
                          int slot) {
  recoil_stage(1, wpn, ads);
  player_t *p = &g_players[0];
  p->grounded = p->prev_grounded = 1;
  p->yaw = yaw;
  p->pitch = pitch;
  v3 eye = player_eye_pos(p);
  // THE REFERENCE RAY IS SPELLED OUT FROM THE ANGLES, deliberately not taken from
  // bore_axis: an analytic check that asks the helper under test for its own answer
  // agrees with it through any sign error either of them makes.
  float hz = cosf(pitch);
  v3 aim = {{sinf(yaw) * hz, sinf(pitch), -cosf(yaw) * hz}};
  // The panel stands square across -z with its ENTRY FACE through that intersection,
  // so `want` is the point the round has to arrive on and the row is the miss measured
  // on the face. The z face is always the one entered first: the slab is 0.5 m deep
  // and the aim crosses under a metre of x or y over that depth, against a face 6 m
  // across. The fixture stands 20 m up (recoil_stage), so a tilted aim meets the panel
  // and never the arena floor.
  v3 want = v3_add(eye, v3_scale(aim, dist));
  g_solids[slot] = (solid_t){
      .min = {{want.x - 3.0f, want.y - 3.0f, want.z - 0.5f}},
      .max = {{want.x + 3.0f, want.y + 3.0f, want.z}},
      .pen = 0};
  g_num_solids = slot + 1;

  // Sixty ticks of the real weapon and presentation path settle the ADS blend and
  // carry pose, so the round leaves a calm rifle and the reading
  // is the standing geometry rather than a transient.
  int held[IN_COUNT] = {0};
  held[IN_ADS] = ads;
  for (int t = 0; t < 60; t++) {
    weapon_tick(p, 0, held);
    for (int i = 0; i < IN_COUNT; i++) p->prev_held[i] = held[i];
    recoil_present_body(p);
    g_tick++;
  }
  held[IN_FIRE] = 1;
  int ev0 = g_num_events;
  weapon_tick(p, 0, held);

  const event_t *fire = NULL;
  for (int i = ev0; i < g_num_events; i++)
    if (g_events[i].type == EV_FIRE && g_events[i].src == 0) fire = &g_events[i];
  // k == 1 is "the round stopped on a solid" — this face is the only solid left in the
  // arena, so a k of anything else means the shot left the 6 x 6 m panel entirely and
  // there is no impact to measure. A tilted aim meets the face obliquely, which makes
  // the same angular error read LARGER on it than on the level axis: the gate is
  // stricter off-axis, never looser.
  int found = fire && fire->k == 1;
  float dy = found ? fire->a.y - want.y : 0.0f;
  float dx = found ? fire->a.x - want.x : 0.0f;
  // THE ADS ROWS ARE THE PROMISE: standing, grounded, cold and fully aimed, gun_spread
  // is hip*(1-1) + action*0 = ZERO by construction, so an aimed round arrives on
  // the eye line to the centimetre. The HIP rows carry the cone and can only be graded
  // against its size: HIP SPREAD is the whole radius here (the fixture is grounded),
  // and it reads on the
  // panel as an angle, so the allowance is that angle over the path to the face. The
  // path is dist/|aim.z| and not dist, because a tilted aim meets a face square across
  // -z obliquely and the same angular error lands further out on it.
  float allow = 0.01f;
  if (!ads) allow += tanf(g_sim_wp[wpn][WP_HIP_SPREAD] * DEG2RAD) * dist /
                     fmaxf(fabsf(aim.z), 0.1f);
  int ok = found && fabsf(dy) < allow && fabsf(dx) < allow;
  printf("boresight %s %s aim=(%+.2f %+.2f) d=%.1f dy=%+.3f dx=%+.3f "
         "allow=%.3f %s\n",
         wpn == WPN_SR ? "sr" : "ar", ads ? "ads" : "hip", (double)yaw,
         (double)pitch, (double)dist, (double)dy, (double)dx, (double)allow,
         ok ? "ok" : "FAIL");
  return ok;
}

static void boresight_proof(void) {
  recoil_save_globals();
  int ok = 1;
  int save_solids = g_num_solids;
  int slot = save_solids >= MAX_SOLIDS ? MAX_SOLIDS - 1 : save_solids;
  solid_t save_slot = g_solids[slot];
  static const float D[3] = {4.5f, 10.5f, 18.5f};
  // Level, then off both axes at once with unequal magnitudes and opposite signs, so a
  // swapped sin/cos, a transposed component and a flipped sign each land somewhere
  // different on the face.
  static const float AIM[2][2] = {{0.0f, 0.0f}, {0.70f, -0.20f}};
  for (int wi = 0; wi < WPN_COUNT; wi++)
    for (int ads = 0; ads < 2; ads++)
      for (int ai = 0; ai < 2; ai++)
        for (int di = 0; di < 3; di++)
          ok &= boresight_case(wi, ads, AIM[ai][0], AIM[ai][1], D[di], slot);
  g_solids[slot] = save_slot;
  g_num_solids = save_solids;
  recoil_restore_globals();
  printf("boresight %s\n", ok ? "ok" : "FAIL");
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
  vm_build_plan_t plan = vm_plan_make(p, alpha, eye, rgt, upv, fwd, fwd, ready);
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
  float latch, shot_axis, ballistic_eye;
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
  // A CONE-FREE RIFLE. What the row below grades is PLUMBING — that the trigger does
  // not move the barrel between taking the angles and pushing the event, and that a, b
  // and f invert back onto the eye. The spread cone throws the round off that barrel ON
  // PURPOSE and by a whole degree, so leaving it in would replace an exact identity
  // with a reading of the dispersion, which recoil already owns. Only hip spread
  // needs removing; the grounded, non-sliding action term is already zero.
  recoil_wp_zero(WP_HIP_SPREAD, -1);
  int ev0 = g_num_events;
  // The barrel as the trigger found it. The fixed kick is added by this very
  // call, so the reference has to be taken before it — after it, the weapon stands
  // one round further on and the round in flight would be graded against a barrel it
  // never left along.
  v3 bore_at_trigger = gun_bore_dir(&p->wp, p->yaw, p->pitch);
  gun_shoot(0, &p->wp, player_eye_pos(p), p->yaw, p->pitch,
            p->grounded, p->sliding, &g_rng_gun[0]);

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
      // THE SIM BORE IS THE REFERENCE, read off the weapon state gun_shoot was handed.
      // That makes it an identity in every direction but one: the trigger may not move
      // the barrel between taking the angles and pushing the event, and this is what
      // holds that (a stray write to rec_* ahead of the trace shows up here
      // and nowhere else on this row — the round's own kick is placed after, and is
      // graded by the recoil proof). The live "did the round go where the player
      // aimed" reading lives in boresight's impact measurement.
      out.shot_axis = v3_len(v3_sub(event_dir, bore_at_trigger)) * 1000.0f;
      // ...AND THE ROUND STARTS AT THE EYE THE FIXTURE HANDED gun_shoot, so what this
      // grades is the TRIPLE'S INVERTIBILITY: a, b and f agree closely enough that
      // `a - b*f` lands back on the origin. That is a contract, not a formality — it
      // is how vfx_ev_fire's fallback, audio's shot placement and the fly-by crack all
      // recover the start, and a `f` measured from anywhere else silently moves three
      // subsystems at once. The barrel tip is presentation, graded as `anchor`.
      v3 event_origin = v3_sub(e->a, v3_scale(event_dir, e->f));
      out.ballistic_eye = v3_len(v3_sub(event_origin, player_eye_pos(p)));
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

  // Air is a distinct presentation state: third-person may retain relaxed carry,
  // while the first-person SR remains fully raised across render interpolation.
  // ADS and reload retain spatial-gate priority even while airborne. Keep these
  // witnesses separate from the five grounded busy cases above.
  static const char *const AIR_NAME[] = {"air", "air-ads", "air-reload"};
  for (int air_case = 0; air_case < 3; air_case++) {
    player_t p;
    anim_t a;
    int which = air_case == 1 ? VMF_ADS : air_case == 2 ? VMF_RELOAD : VMF_IDLE;
    vmframe_stage_case(&p, &a, which);
    p.pos = p.prev_pos = (v3){{0, 1.2f, 0}};
    p.grounded = p.prev_grounded = 0;
    p.vel = (v3){{0, -1, 0}};
    anim_reset(&a, p.pos, p.yaw, WPN_SR);
    a.ready_s = a.prev_ready_s = 0;
    for (int tick = 0; tick < 24; tick++) vmframe_ready_tick(&p, &a);
    // Deliberately stale carry history cannot defeat either first-person air gating
    // or the stronger ADS/reload gate in the third-person solver.
    a.ready_s = a.prev_ready_s = 0;
    int state = gun_pose_state(&p.wp, &a, p.vel, p.grounded, p.sliding);
    int want_state = air_case ? GUN_POSE_BLOCKED : GUN_POSE_AIR;
    anim_t before_pose = a;
    skel_solve(&a, p.pos, p.yaw, p.pitch, WPN_SR, state, SKEL_POSE_ONLY);
    anim_t state_only = a;
    state_only.pose = before_pose.pose;
    int pose_only = !memcmp(&state_only, &before_pose, sizeof state_only);
    anim_t raised = a;
    raised.ready_s = raised.prev_ready_s = 1;
    skel_solve(&raised, p.pos, p.yaw, p.pitch, WPN_SR, state, SKEL_POSE_ONLY);
    float body_delta = vmframe_pose_delta(&a.pose, &raised.pose);
    vmframe_3p_axis_t axis = vmframe_3p_axis(aim, a.pose.gun_f);
    float fp_ready[sizeof ALPHA / sizeof *ALPHA] = {0};
    float fp_delta = 0;
    int air_ok = !p.grounded && a.air_s > 0.5f && state == want_state &&
                 pose_only && axis.finite &&
                 (air_case ? body_delta < 1e-6f : body_delta > 0.02f);
    for (unsigned ai = 0; ai < sizeof ALPHA / sizeof *ALPHA; ai++) {
      fp_ready[ai] = vm_render_ready(&p, &a, ALPHA[ai]);
      vmframe_diag_t live = vmframe_build(&p, ALPHA[ai], fp_ready[ai]);
      vmframe_diag_t full = vmframe_build(&p, ALPHA[ai], 1);
      fp_delta = fmaxf(fp_delta, v3_len(v3_sub(live.muzzle, full.muzzle)));
      air_ok &= fp_ready[ai] == 1 && live.finite && full.finite &&
                live.stock_found && full.stock_found &&
                live.mesh_plan <= 1e-6f && full.mesh_plan <= 1e-6f;
    }
    air_ok &= fp_delta < 1e-6f;
    printf("vmframe %s state=%d expected=%d grounded=%d air=%.3f "
           "fp_ready=%.3f/%.3f/%.3f fp_delta_mm=%.6f "
           "body_delta=%.6f axis=%.3f/%.3fdeg pose_only=%d %s\n",
           AIR_NAME[air_case], state, want_state, p.grounded, (double)a.air_s,
           (double)fp_ready[0], (double)fp_ready[1], (double)fp_ready[2],
           (double)(fp_delta * 1000), (double)body_delta,
           (double)axis.down, (double)axis.cross, pose_only, air_ok ? "ok" : "FAIL");
    ok &= air_ok;
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
  // anchor0 is an identity on the anchor branch — vfx_ev_fire starts the tracer on
  // pose_muzzle(g_fire_anchor_pose) — and the branch is what it grades: the anchor
  // still carried THIS shot's src and tick when VFX ran. A stale or foreign one drops
  // in the eye fallback and this reads a metre. visible_hold0 is the live measurement
  // beside it: the LIVE pose's muzzle against the same tracer, two poses that nothing
  // assigns to each other.
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
                event.ballistic_eye <= 0.002f;
  printf("vmframe fire sr T0 vm_pair=%.3fmm fx_pair=%.3fmm "
         "publish=%.3fmm pair=%.3fmm remap=%.3fmm "
         "pose_pair=%.3fmm anchor=%.3fmm visible_hold=%.3fmm "
         "bore=%.4fmrad ballistic_eye=%.3fmm "
         "depth_vm/fx=%.1f/%.1fmm "
         "tick=%ld stale=%d %s\n",
         (double)(vm0 * 1000.0f), (double)(fx0 * 1000.0f),
         (double)(pub0 * 1000.0f), (double)(pub_pair0 * 1000.0f),
         (double)(remap0 * 1000.0f), (double)(pose0 * 1000.0f),
         (double)(anchor0 * 1000.0f), (double)(visible_hold0 * 1000.0f),
         (double)event.shot_axis, (double)(event.ballistic_eye * 1000.0f),
         (double)(event.vm_depth0 * 1000.0f),
         (double)(event.fx_depth0 * 1000.0f), event.pub_tick0,
         event.stale_rejected && pred.stale_rejected, fire_ok ? "ok" : "FAIL");
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
  g_sim_gun = g_cfg.gun;
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
  // The echoed shot adds the same immediate deterministic step to a foreign barrel.
  float want_rec_p = g_sim_wp[WPN_SR][WP_RECOIL_UP];
  int body_impulse = ra->recoil_back == 0.0f && ra->recoil_lift == 0.0f &&
                     ra->recoil_back_v > 0.5f && ra->recoil_lift_v == 0.0f &&
                     fabsf(remote_p->wp.rec_p - want_rec_p) < 1e-6f;
  g_tick++;
  net_cl_present_advance();
  client_anim_all();
  // The remote snapshot says the trigger is up, so one presentation tick subtracts
  // exactly one global recenter step while the cosmetic body spring starts moving.
  float want_after = fmaxf(want_rec_p - g_sim_gun[GP_RECENTER] * TICK_DT, 0.0f);
  int body_moved = ra->recoil_back > 0.004f && ra->recoil_lift == 0.0f &&
                   fabsf(remote_p->wp.rec_p - want_after) < 1e-6f;
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
         "end_vm=%d series=%d ammo=%d/%d rec=(%.6f %.6f) remote_rec=%.4f "
         "rng=%016llx echo_once=%d replay_body=%d remote3p=(%d %d) "
         "sidefx=%d/%d counts=%d/%d noise_t=%ld events=%d/%d %s\n",
         burst ? "burst" : "single", (double)g_cl_worst_err_mm,
         (double)(ref.peak * 1000.0f), (double)(peak * 1000.0f),
         ref.peak_tick, peak_tick, endpoint_same, series_same,
         p->wp.ammo[WPN_AR], endpoint.wp.ammo[WPN_AR],
         (double)p->wp.rec_p, (double)p->wp.rec_y,
         (double)remote_p->wp.rec_p,
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
  rp->land_dip = 0.6f;
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

// Canonical defaults reach startup, RESET, config files and server-authoritative
// prediction through their real owners. All files and UDP endpoints are private
// fixtures; received tuning must never mutate the player's authored configuration.
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void defaults_expect(int condition, const char *fixture, int *fail) {
  if (condition) return;
  fprintf(stderr, "defaultsproof FAIL fixture=%s\n", fixture);
  *fail = 1;
}
static int defaults_tuning_equal(const config_t *a, const config_t *b) {
  return !memcmp(a->mv, b->mv, sizeof a->mv) &&
      !memcmp(a->gun, b->gun, sizeof a->gun) &&
      !memcmp(a->wp, b->wp, sizeof a->wp) &&
      !memcmp(a->pad, b->pad, sizeof a->pad);
}
static float defaults_varied(const param_def_t *def, float fraction) {
  if (fraction == 0) return def->lo;
  if (fraction == 1) return def->hi;
  return def->lo + (def->hi - def->lo) * fraction;
}
static void defaults_fill_tuning(config_t *c, float fraction) {
  for (int i = 0; i < MV_COUNT; i++) c->mv[i] = defaults_varied(&MV_DEF[i], fraction);
  for (int i = 0; i < GP_COUNT; i++) c->gun[i] = defaults_varied(&GP_DEF[i], fraction);
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) c->wp[w][i] = defaults_varied(&WP_DEF[i], fraction);
  for (int i = 0; i < PAD_COUNT; i++) c->pad[i] = defaults_varied(&PAD_DEF[i], fraction);
}
static void defaults_case_config(int *fail) {
  config_t canonical = config_defaults(), actual = canonical;
  defaults_expect(!canonical.devmode && strlen(TUNING_SHA) == 64 &&
      strlen(TUNING_SCHEMA_SHA) == 64, "first-install-canonical-identity", fail);
  int tables = 1;
  for (int i = 0; i < MV_COUNT; i++) tables &= actual.mv[i] == MV_DEF[i].def;
  for (int i = 0; i < GP_COUNT; i++) tables &= actual.gun[i] == GP_DEF[i].def;
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) tables &= actual.wp[w][i] == WP_DEFAULTS[w][i];
  for (int i = PAD_AS_FRIC; i <= PAD_AS_DIST; i++) tables &= actual.pad[i] == PAD_DEF[i].def;
  defaults_expect(tables, "startup-table-owner", fail);
  defaults_fill_tuning(&actual, 0.31415926f);
  actual.sense = 1.731f; actual.fps_cap = 241; actual.devmode = 1;
  snprintf(actual.name, sizeof actual.name, "CUSTOM");
  for (int group = 0; group < 3; group++) {
    for (int weapon = 0; weapon < (group == 1 ? WPN_COUNT : 1); weapon++) {
      config_t reset = actual, expected = actual;
      if (group == 0) memcpy(expected.mv, canonical.mv, sizeof expected.mv);
      else if (group == 1) {
        memcpy(expected.gun, canonical.gun, sizeof expected.gun);
        memcpy(expected.wp[weapon], canonical.wp[weapon], sizeof expected.wp[weapon]);
      } else for (int i = PAD_AS_FRIC; i <= PAD_AS_DIST; i++) expected.pad[i] = canonical.pad[i];
      config_dev_reset(&reset, group, weapon);
      defaults_expect(defaults_tuning_equal(&reset, &expected) &&
          reset.sense == actual.sense && reset.fps_cap == actual.fps_cap &&
          reset.devmode == 1 && !strcmp(reset.name, actual.name), "dev-reset-selected-group", fail);
    }
  }
  char directory[] = "/tmp/skill-defaults-XXXXXX";
  if (!mkdtemp(directory)) { defaults_expect(0, "private-config-directory", fail); return; }
  char path[128]; snprintf(path, sizeof path, "%s/config.cfg", directory);
  int ephemeral = g_cfg_ephemeral;
  g_cfg_ephemeral = 0;
  config_t loaded;
  config_load_or_create(path, &loaded);
  FILE *created = fopen(path, "r");
  defaults_expect(created && !loaded.devmode && defaults_tuning_equal(&loaded, &canonical),
      "first-install-created-file", fail);
  if (created) fclose(created);
  actual.devmode = 0;
  config_write(path, &actual);
  char before[65], after[65];
  int hashed = native_profile_hash(path, before);
  config_load_or_create(path, &loaded);
  defaults_expect(defaults_tuning_equal(&loaded, &actual) &&
      loaded.sense == actual.sense && loaded.fps_cap == actual.fps_cap &&
      !loaded.devmode && !strcmp(loaded.name, actual.name), "emission-nine-digit-runtime-override", fail);
  defaults_expect(hashed && native_profile_hash(path, after) && !strcmp(before, after),
      "existing-user-file-preserved", fail);
  for (int high = 0; high < 2; high++) {
    config_t boundary = canonical;
    defaults_fill_tuning(&boundary, (float)high);
    config_write(path, &boundary);
    config_load_or_create(path, &loaded);
    defaults_expect(defaults_tuning_equal(&loaded, &boundary),
        high ? "binary32-upper-bound-roundtrip" : "binary32-lower-bound-roundtrip", fail);
  }
  const int frag_input[] = {0, 5, 100, 101, 1000, -1, 4};
  const int frag_expected[] = {0, 5, 100, 0, 0, 5, 5};
  for (int i = 0; i < 7; i++) {
    config_t rules = canonical;
    rules.sp_bots = i == 0 ? 0 : i == 1 ? 51 : 50;
    rules.sp_frag = frag_input[i];
    config_write(path, &rules);
    config_load_or_create(path, &loaded);
    defaults_expect(loaded.sp_bots == (i == 0 ? 1 : 50) &&
        loaded.sp_frag == frag_expected[i], "singleplayer-rule-boundaries-roundtrip", fail);
  }
  g_cfg_ephemeral = ephemeral;
  unlink(path); rmdir(directory);
  config_t again = config_defaults();
  defaults_expect(defaults_tuning_equal(&again, &canonical), "reset-never-rewrites-canonical", fail);
}
static int defaults_packet(int fd, int type, uint32_t *ip, uint16_t *port) {
  unsigned char packet[2048];
  for (int tries = 0; tries < 200; tries++) {
    int size = net_udp_recv(fd, ip, port, packet, sizeof packet);
    if (size >= 3 && packet[0] == (NET_PROTO & 255) && packet[1] == (NET_PROTO >> 8) &&
        packet[2] == type) return 1;
    struct timespec pause = {0, 1000000L}; nanosleep(&pause, NULL);
  }
  return 0;
}
static int defaults_poll_state(nc_state_t state) {
  for (int tries = 0; tries < 200; tries++) {
    net_cl_poll();
    if (g_cl.state == state) return 1;
    if (g_cl.state == NC_FAILED) return 0;
    struct timespec pause = {0, 1000000L}; nanosleep(&pause, NULL);
  }
  return 0;
}

// Real UDP replies exercise the client parser across the version boundary.
// Neither a foreign source, a truncated reply nor a delayed rejection may
// break a live match. Both older and newer servers must explain a refused join.
static void nethandshake_proof(void) {
  recoil_save_globals();
  memset(&g_cl, 0, sizeof g_cl); g_cl.fd = -1;
  int server = net_udp_open(0), foreign = net_udp_open(0);
  struct sockaddr_in address = {.sin_family = AF_INET,
                               .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  socklen_t size = sizeof address;
  int ok = server >= 0 && foreign >= 0 &&
      bind(server, (struct sockaddr *)&address, size) == 0 &&
      getsockname(server, (struct sockaddr *)&address, &size) == 0;
  int cases = 0;
  const nc_state_t states[] = {NC_CONNECTING, NC_JOINING, NC_PLAYING};
  for (int stage = 0; ok && stage < 3; stage++) {
    for (int which = 0; ok && which < 7; which++) {
      uint32_t peer_ip = 0; uint16_t peer_port = 0;
      ok = net_cl_connect(htonl(INADDR_LOOPBACK), ntohs(address.sin_port), "PROTOCOL") &&
          defaults_packet(server, NP_CONNECT, &peer_ip, &peer_port);
      if (!ok) break;
      g_cl.state = states[stage];
      g_cl.last_recv_ms = net_now_ms();
      unsigned char packet[8];
      np_wr out = {packet, sizeof packet, 0, 0};
      np_w16(&out, which == 6 ? NET_PROTO : NET_PROTO + (which == 1 ? 1 : -1));
      np_w8(&out, which == 4 ? NP_COOKIE : NP_REJECT);
      if (which != 2) np_w8(&out, which >= 5 ? NR_FULL : NR_PROTO);
      int sender = which == 3 ? foreign : server;
      ok = net_udp_send(sender, peer_ip, peer_port, packet, (int)out.at) == (int)out.at;
      int rejected = stage < 2 && (which < 2 || which == 6);
      if (rejected) ok &= defaults_poll_state(NC_FAILED);
      else net_cl_poll();
      ok &= g_cl.state == (rejected ? NC_FAILED : states[stage]) &&
          g_cl.reject == (rejected ? (which == 6 ? NR_FULL : NR_PROTO) : 0);
      printf("nethandshake stage=%d reply=%d %s\n", stage, which, ok ? "ok" : "FAIL");
      cases++;
      net_cl_disconnect();
    }
  }
  if (g_cl.fd >= 0) net_cl_disconnect();
  if (server >= 0) net_udp_close(server);
  if (foreign >= 0) net_udp_close(foreign);
  recoil_restore_globals();
  printf("nethandshake cases=%d %s\n", cases, ok && cases == 21 ? "ok" : "FAIL");
  if (!ok || cases != 21) exit(1);
}

static int defaults_accept(int server, uint16_t port, const config_t *tuning, uint32_t seed) {
  uint32_t peer_ip = 0;
  uint16_t peer_port = 0;
  if (!net_cl_connect(htonl(INADDR_LOOPBACK), port, "DEFAULTS") ||
      !defaults_packet(server, NP_CONNECT, &peer_ip, &peer_port)) return 0;
  unsigned char packet[2048];
  np_wr out = {packet, sizeof packet, 0, 0};
  np_w16(&out, NET_PROTO); np_w8(&out, NP_COOKIE); np_w32(&out, 0x11223344u);
  if (net_udp_send(server, peer_ip, peer_port, packet, (int)out.at) != (int)out.at ||
      !defaults_poll_state(NC_JOINING) ||
      !defaults_packet(server, NP_JOIN, &peer_ip, &peer_port)) return 0;
  map_generate(seed); g_map_seed = seed;
  out = (np_wr){packet, sizeof packet, 0, 0};
  np_w16(&out, NET_PROTO); np_w8(&out, NP_ACCEPT);
  np_w8(&out, 0); np_w8(&out, 0); np_w8(&out, 1);
  np_w32(&out, seed); np_w32(&out, net_world_hash()); np_w32(&out, 0x12345678u);
  for (int i = 0; i < MV_COUNT; i++) np_wf(&out, tuning->mv[i]);
  for (int i = 0; i < GP_COUNT; i++) np_wf(&out, tuning->gun[i]);
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) np_wf(&out, tuning->wp[w][i]);
  return !out.bad && net_udp_send(server, peer_ip, peer_port, packet, (int)out.at) == (int)out.at &&
      defaults_poll_state(NC_PLAYING);
}
static int defaults_first_snapshot(int server) {
  player_t *player = &g_players[0];
  player_spawn(player);
  player->pos = player->prev_pos = (v3){{0, 100, 0}};
  player->vel = (v3){{0, 0, 0}};
  player->grounded = player->prev_grounded = 0;
  player->yaw = player->pitch = 0;
  g_humans_on[0] = 1;
  net_ent_t entity; net_ent_from_live(&entity, 0);
  unsigned char packet[2048]; np_wr out = {packet, sizeof packet, 0, 0};
  np_w16(&out, NET_PROTO); np_w8(&out, NP_SNAPSHOT);
  np_w32(&out, 10); np_w32(&out, 0);
  np_w32(&out, net_world_hash()); np_w32(&out, g_map_seed); np_w8(&out, 0);
  np_w8(&out, 1); net_ent_encode(&out, &entity); np_w8(&out, 0);
  struct sockaddr_in address = {0}; socklen_t size = sizeof address;
  if (getsockname(g_cl.fd, (struct sockaddr *)&address, &size) != 0 || out.bad ||
      net_udp_send(server, htonl(INADDR_LOOPBACK), ntohs(address.sin_port), packet, (int)out.at) != (int)out.at)
    return 0;
  for (int tries = 0; tries < 200; tries++) {
    net_cl_poll();
    if (g_cl.synced) return g_online && g_local_ent == 0;
    struct timespec pause = {0, 1000000L}; nanosleep(&pause, NULL);
  }
  return 0;
}
static void defaults_prediction(int *fail) {
  int solids = g_num_solids;
  g_num_solids = 0;
  player_t anchor = g_players[0];
  uint64_t rng = g_rng_gun[0];
  int held[IN_COUNT] = {0}; held[IN_FWD] = 1;
  for (int i = 0; i < 16; i++) net_cl_tick(held, 0, 0, 0, 0);
  v3 actual = g_players[0].pos, velocity = g_players[0].vel;
  g_players[0] = anchor; g_rng_gun[0] = rng;
  g_predicting = 1;
  for (int i = 0; i < 16; i++) player_move_tick(&g_players[0], 0, held, 0, 0);
  g_predicting = 0;
  defaults_expect(v3_len(v3_sub(actual, g_players[0].pos)) < 1e-6f &&
      v3_len(v3_sub(velocity, g_players[0].vel)) < 1e-6f,
      "prediction-uses-received-server-tuning", fail);
  g_players[0] = anchor; g_rng_gun[0] = rng;
  g_sim_mv = g_cfg.mv; g_sim_gun = g_cfg.gun; g_sim_wp = g_cfg.wp;
  g_predicting = 1;
  for (int i = 0; i < 16; i++) player_move_tick(&g_players[0], 0, held, 0, 0);
  g_predicting = 0;
  defaults_expect(v3_len(v3_sub(actual, g_players[0].pos)) > 0.001f,
      "prediction-local-tuning-failing-control", fail);
  g_sim_mv = g_cl.srv_mv; g_sim_gun = g_cl.srv_gun; g_sim_wp = g_cl.srv_wp;
  g_num_solids = solids;
}
static void defaults_case_authority(int *fail) {
  recoil_save_globals();
  static proof_world_t world;
  proof_world_save(&world);
  config_t local_saved = g_cfg, canonical = config_defaults();
  uint32_t seed = g_map_seed;
  typeof(g_pad) pad_saved = g_pad;
  typeof(g_human_name) names_saved;
  memcpy(names_saved, g_human_name, sizeof names_saved);
  uint16_t fraglimit_saved = g_net_fraglimit;
  memset(&g_cl, 0, sizeof g_cl); g_cl.fd = -1;
  g_cfg = canonical; defaults_fill_tuning(&g_cfg, 0.2f);
  g_cfg.sense = 1.739f; g_cfg.fps_cap = 237;
  snprintf(g_cfg.name, sizeof g_cfg.name, "LOCAL");
  config_t local = g_cfg;
  int server = net_udp_open(0);
  struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  socklen_t size = sizeof address;
  int opened = server >= 0 && bind(server, (struct sockaddr *)&address, size) == 0 &&
      getsockname(server, (struct sockaddr *)&address, &size) == 0;
  defaults_expect(opened, "private-loopback-server", fail);
  for (int connection = 0; opened && connection < 2; connection++) {
    config_t remote = canonical;
    defaults_fill_tuning(&remote, connection ? 0.8f : 0.6f);
    int joined = defaults_accept(server, ntohs(address.sin_port), &remote, seed);
    defaults_expect(joined, connection ? "reconnect-actual-accept" : "join-actual-accept", fail);
    if (!joined) break;
    defaults_expect(g_sim_mv == g_cl.srv_mv && g_sim_gun == g_cl.srv_gun &&
        g_sim_wp == g_cl.srv_wp && !memcmp(g_sim_mv, remote.mv, sizeof remote.mv) &&
        !memcmp(g_sim_gun, remote.gun, sizeof remote.gun) &&
        !memcmp(g_sim_wp, remote.wp, sizeof remote.wp), "server-table-authority", fail);
    defaults_expect(defaults_tuning_equal(&g_cfg, &local) && g_cfg.sense == local.sense &&
        g_cfg.fps_cap == local.fps_cap && !strcmp(g_cfg.name, local.name) &&
        !memcmp(&g_pad, &pad_saved, sizeof g_pad), "received-tuning-never-authors-local", fail);
    int synced = defaults_first_snapshot(server);
    defaults_expect(synced, "actual-first-snapshot", fail);
    if (synced) defaults_prediction(fail);
    net_cl_disconnect();
    defaults_expect(g_cl.state == NC_OFF && g_cl.fd == -1 &&
        g_sim_mv == g_cfg.mv && g_sim_gun == g_cfg.gun && g_sim_wp == g_cfg.wp &&
        defaults_tuning_equal(&g_cfg, &local), "disconnect-restores-local-owner", fail);
  }
  if (g_cl.fd >= 0) net_cl_disconnect();
  if (server >= 0) net_udp_close(server);
  config_t again = config_defaults();
  defaults_expect(defaults_tuning_equal(&again, &canonical), "server-never-rewrites-canonical", fail);
  map_generate(seed); g_map_seed = seed;
  g_cfg = local_saved; g_pad = pad_saved;
  memcpy(g_human_name, names_saved, sizeof names_saved);
  g_net_fraglimit = fraglimit_saved;
  recoil_restore_globals();
  proof_world_restore(&world);
}
static void defaults_proof(void) {
  int fail = 0;
  defaults_case_config(&fail);
  defaults_case_authority(&fail);
  printf("defaultsproof canonical=%s schema=%s startup=reset emission=roundtrip authority=join-predict-reconnect %s\n",
      TUNING_SHA, TUNING_SCHEMA_SHA, fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

// --- Figure adaptation: rendered geometry versus immutable simulation owners ----
// These probes reuse the recoil transaction above. The emitted triangle stream is
// the visibility witness; hit registration remains the real analytic and history
// path. Census changes are compared with the preserved same-flags render build.
typedef struct {
  int k, kcap, cull, probe, nprobe, fp;
  v3 eye;
  long lean_scans, scene_drops;
  float low[WPN_COUNT];
} figadapt_render_save_t;

static figadapt_render_save_t figadapt_render_save(void) {
  figadapt_render_save_t out = {g_fig_k, g_fig_kcap, g_cam_cull, g_probe_on,
    g_nprobe, g_fig_fp, g_cam_eye, g_lean_step_scans, g_scene_drops, {0}};
  memcpy(out.low, g_gun_low, sizeof out.low);
  return out;
}

static void figadapt_render_restore(const figadapt_render_save_t *sv) {
  g_fig_k = sv->k; g_fig_kcap = sv->kcap; g_cam_cull = sv->cull;
  g_probe_on = sv->probe; g_nprobe = 0; g_fig_fp = sv->fp;
  g_cam_eye = sv->eye; g_lean_step_scans = sv->lean_scans;
  g_scene_drops = sv->scene_drops;
  memcpy(g_gun_low, sv->low, sizeof sv->low);
  scene_reset();
}

static void figadapt_solve(player_t *p, anim_t *a) {
  p->wp.ready_want = (unsigned char)player_carry_ready(p);
  int state = gun_pose_state(&p->wp, a, p->vel, p->grounded, p->sliding);
  skel_solve(a, p->pos, p->yaw, p->pitch, p->wp.cur, state, SKEL_POSE_ONLY);
  a->prev_pose = a->pose;
}

static void figadapt_stage(player_t *p, anim_t *a, int wpn, float yaw,
                            int crouch, float lean, float ads, int slide) {
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0, 20, 0}};
  p->yaw = yaw; p->pitch = 0;
  p->vel = (v3){{0, 0, 0}};
  p->grounded = p->prev_grounded = 1;
  p->crouched = crouch || slide; p->sliding = slide;
  p->eye = p->prev_eye = p->crouched ? EYE_CROUCH : EYE_STAND;
  p->lean = lean;
  p->wp.cur = wpn; p->wp.switch_t = 0;
  p->wp.ads_s = p->wp.ads_lin = p->wp.prev_ads_s = ads;
  p->wp.ads_want = ads > 0.5f;
  anim_reset(a, p->pos, yaw, wpn);
  for (int tick = 0; tick < 120; tick++)
    anim_tick(a, p->pos, p->vel, yaw, 0.0f, 1, crouch, 0.0f, slide,
              ads, lean, 1, 0, wpn, GUN_POSE_IDLE);
  figadapt_solve(p, a);
}

static void figadapt_draw_at(const player_t *p, const anim_t *a, int view,
                              int tier, gunanim_t ga, float alpha) {
  g_cam_cull = 0; g_fig_kcap = tier;
  g_nprobe = 0; g_probe_on = 1; g_fig_fp = 0;
  fig_contact_patch_reset();
  if (view) {
    g_cam_eye = v3_add(p->pos, (v3){{0, 1, 0}});
    scene_reset();
    scene_draw_figure(a, p->pos, p->yaw, NULL, alpha, 0.0f,
                      (v3){{1, 1, 1}}, p->wp.cur, ga);
  } else {
    v3 eye = {{0, 0, 0}}, right = {{1, 0, 0}}, up = {{0, 1, 0}};
    v3 forward = {{0, 0, -1}};
    vm_build_plan_t plan = vm_plan_make(p, alpha, eye, right, up, forward,
        forward, vm_render_ready(p, a, alpha));
    plan.pose.ga = ga;
    plan.frame = vm_stage_springs(&plan.pose, &p->vm, alpha, eye, right, up,
                                   forward, forward);
    plan.muzzle = vm_plan_muzzle(&plan.pose, &plan.frame);
    vm_build_prepared(eye, right, up, forward, &plan);
  }
  g_probe_on = 0;
}

static void figadapt_draw(const player_t *p, const anim_t *a, int view,
                           int tier, gunanim_t ga) {
  figadapt_draw_at(p, a, view, tier, ga, 1.0f);
}

// The query is ray/triangle presence, not parity: shared edges count as visible
// and overlapping clothing pieces cannot cancel each other out.
static int fighit_triangle(v3 o, v3 d, v3 a, v3 b, v3 c) {
  double e1[3], e2[3], q[3], p[3], s[3];
  for (int k = 0; k < 3; k++) {
    e1[k] = (double)b.e[k] - a.e[k];
    e2[k] = (double)c.e[k] - a.e[k];
    s[k] = (double)o.e[k] - a.e[k];
  }
  p[0] = d.y * e2[2] - d.z * e2[1];
  p[1] = d.z * e2[0] - d.x * e2[2];
  p[2] = d.x * e2[1] - d.y * e2[0];
  double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
  if (fabs(det) < 1e-12) return 0;
  double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) / det;
  q[0] = s[1] * e1[2] - s[2] * e1[1];
  q[1] = s[2] * e1[0] - s[0] * e1[2];
  q[2] = s[0] * e1[1] - s[1] * e1[0];
  double v = (d.x * q[0] + d.y * q[1] + d.z * q[2]) / det;
  double t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) / det;
  return t > 0 && u >= -1e-7 && v >= -1e-7 && u + v <= 1.0000001;
}

static int fighit_family(const char *name) {
  if (!name) return 0;
  if (!strcmp(name, "skull") || !strcmp(name, "nose") || !strcmp(name, "ear"))
    return 3;
  if (!strcmp(name, "torso") || !strcmp(name, "uarm")) return 2;
  if (!strcmp(name, "helm_shell") || !strcmp(name, "carrier") ||
      !strcmp(name, "pack") || !strcmp(name, "goggle")) return 1;
  return 0;
}

static unsigned fighit_visible(v3 origin, v3 direction) {
  unsigned visible = 0;
  for (int i = 0; i < g_nprobe; i++) {
    const probe_t *p = &g_probe[i];
    int family = fighit_family(p->name);
    if (!family || (visible & (1u << family))) continue;
    for (int f = p->v0; f + 3 * VERT_FLOATS <= p->v1; f += 3 * VERT_FLOATS)
      if (fighit_triangle(origin, direction, SCENE_V(f),
                           SCENE_V(f + VERT_FLOATS), SCENE_V(f + 2 * VERT_FLOATS))) {
        visible |= 1u << family;
        break;
      }
  }
  return visible;
}

static v3 fighit_world(v3 local, v3 position, float yaw) {
  float c = cosf(yaw), s = sinf(yaw);
  return v3_add(position, (v3){{local.x * c - local.z * s,
                              local.y, local.x * s + local.z * c}});
}

static void fighit_proof(void) {
  static const struct { const char *name; int crouch, slide; float lean, ads, reload; } POSE[] = {
    {"stand", 0, 0, 0, 0, 0}, {"crouch", 1, 0, 0, 0, 0},
    {"lean-left", 0, 0, -1, 1, 0}, {"lean-right", 0, 0, 1, 1, 0},
    {"ads", 0, 0, 0, 1, 0}, {"reload", 0, 0, 0, 0, 0.35f},
    {"slide", 0, 1, 0, 0, 0}
  };
  static const int TIER[] = {16, 12, 8, 6};
  static const float YAW[] = {0, 45, 90, 145, 180};
  static hist_t history[MAX_ENTS][NET_HIST];
  memcpy(history, g_hist, sizeof history);
  long hist_tick = g_hist_tick;
  recoil_save_globals();
  figadapt_render_save_t render = figadapt_render_save();
  recoil_stage(1, WPN_AR, 0);
  int fail = 0, rows = 0, rays = 0, histories = 0, visible_hits = 0;
  int hit_errors = 0, history_errors = 0, tier_errors = 0, census_errors = 0;
  int yaw_errors = 0;
  uint64_t yaw_hash[sizeof TIER / sizeof *TIER] = {0};
  int triangle_control =
      fighit_triangle((v3){{0, 0, -1}}, (v3){{0, 0, 1}},
        (v3){{-1, -1, 0}}, (v3){{1, -1, 0}}, (v3){{0, 1, 0}}) &&
     !fighit_triangle((v3){{2, 0, -1}}, (v3){{0, 0, 1}},
        (v3){{-1, -1, 0}}, (v3){{1, -1, 0}}, (v3){{0, 1, 0}});
  fail |= !triangle_control;
  for (int weapon = 0; weapon < WPN_COUNT; weapon++)
    for (unsigned pose = 0; pose < sizeof POSE / sizeof *POSE; pose++)
      for (unsigned yi = 0; yi < sizeof YAW / sizeof *YAW; yi++)
        for (unsigned tier = 0; tier < sizeof TIER / sizeof *TIER; tier++) {
          player_t *p = &g_players[0];
          anim_t *a = &g_player_anim[0];
          g_tick = 1000;
          figadapt_stage(p, a, weapon, YAW[yi] * DEG2RAD,
                         POSE[pose].crouch, POSE[pose].lean, POSE[pose].ads,
                         POSE[pose].slide);
          if (POSE[pose].reload > 0)
            p->wp.reload_t = (int)((1.0f - POSE[pose].reload) *
                                  wpn_reload_ticks(weapon, 0));
          figadapt_solve(p, a);
          figadapt_draw(p, a, 1, TIER[tier], gun_anim(&p->wp));
          tier_errors += g_fig_k != TIER[tier] || g_scene_drops != render.scene_drops;
          unsigned census[4] = {0};
          for (int n = 0; n < g_nprobe; n++)
            census[fighit_family(g_probe[n].name)] += g_probe[n].v1 > g_probe[n].v0;
          census_errors += !census[1] || !census[2] || !census[3];
          player_t frozen = *p;
          g_tick = 2000; net_hist_record();
          g_tick = 2001; net_hist_record();
          p->pos.x += 8.0f;  // historical traces must not use the live actor
          int miss[2] = {0}, empty[2] = {0}, gear[2] = {0};
          float outside[2] = {0};
          uint64_t hash = UINT64_C(1469598103934665603);
          for (int family = 2; family <= 3; family++)
            for (int height = 0; height < 3; height++)
              for (int axis = 0; axis < 2; axis++)
                for (int step = -40; step <= 40; step++) {
                  float lateral = (float)step * 0.02f + 0.005f;
                  float lat = lean_eye_off(frozen.lean);
                  float drop = lean_eye_drop(frozen.lean);
                  float y = family == 3
                    ? frozen.eye - drop - 0.08f + (float)height * 0.10f
                    : (frozen.eye - 0.12f - drop * LEAN_BODY_K) *
                        (0.45f + (float)height * 0.22f);
                  v3 local = axis ? (v3){{-2, y, lateral}} : (v3){{lateral, y, -2}};
                  v3 local_d = axis ? (v3){{1, 0, 0}} : (v3){{0, 0, 1}};
                  v3 origin = fighit_world(local, frozen.pos, frozen.yaw);
                  v3 direction = fighit_world(local_d, (v3){{0, 0, 0}}, frozen.yaw);
                  float centre = axis ? 0 : lat * (family == 3 ? 1.0f : LEAN_BODY_K);
                  float extent = family == 3 ? 0.13f : axis ? 0.22f : 0.28f;
                  // At full lean the lower head overlaps the torso in height.
                  // The nearer entry wins even when this is a head-height row;
                  // assuming the row's label is the hit part would hide that edge.
                  int head = y >= frozen.eye - 0.12f - drop &&
                             y <= frozen.eye + 0.18f - drop &&
                             fabsf(lateral - (axis ? 0.0f : lat)) < 0.13f;
                  int body = y >= 0 &&
                             y <= frozen.eye - 0.12f - drop * LEAN_BODY_K &&
                             fabsf(lateral - (axis ? 0.0f : lat * LEAN_BODY_K)) <
                               (axis ? 0.22f : 0.28f);
                  float head_entry = axis ? lat - 0.13f : -0.13f;
                  float body_entry = axis ? lat * LEAN_BODY_K - 0.28f : -0.22f;
                  int expected = head && (!body || head_entry <= body_entry) ? 3 : body ? 2 : 0;
                  int part = 0, rewound_part = 0;
                  float distance = ent_trace_box(frozen.pos, frozen.eye, frozen.yaw,
                      frozen.lean, origin, direction, &part);
                  float rewound = net_hist_trace(0, 2000, 0.5f, origin, direction,
                                                  &rewound_part);
                  int classified = distance > 0 ? part : 0;
                  if (classified != expected && hit_errors < 4)
                    printf("fighit oracle mismatch pose=%s y=%g q=%g axis=%d wanted=%d got=%d\n",
                        POSE[pose].name, (double)y, (double)lateral, axis, expected, classified);
                  hit_errors += classified != expected;
                  history_errors += (rewound > 0 ? rewound_part : 0) != classified ||
                                    fabsf(rewound - distance) > 1e-4f;
                  unsigned visible = fighit_visible(origin, direction);
                  int skin = (visible & (1u << family)) != 0;
                  int hit = classified == family;
                  visible_hits += skin && hit;
                  miss[family - 2] += skin && !hit;
                  empty[family - 2] += !skin && hit;
                  gear[family - 2] += !skin && (visible & 2u) != 0;
                  if (skin && !hit) outside[family - 2] = fmaxf(outside[family - 2],
                      fabsf(lateral - centre) - extent);
                  hash = (hash ^ (uint64_t)(visible | (unsigned)classified << 4)) *
                         UINT64_C(1099511628211);
                  rays++; histories++;
                }
          // Rotating both the actor and its ray grid must preserve the visible
          // surface and hit classification, including each silhouette sample.
          if (yi == 0) yaw_hash[tier] = hash;
          else yaw_errors += yaw_hash[tier] != hash;
          printf("fighit %s %s yaw=%.0f k=%d body=%d/%d/%.1f head=%d/%d/%.1f "
                 "gear=%d/%d hash=%016llx\n", WPN_DEF[weapon].name, POSE[pose].name,
                 (double)YAW[yi], TIER[tier], miss[0], empty[0], (double)(outside[0] * 1000),
                 miss[1], empty[1], (double)(outside[1] * 1000), gear[0], gear[1],
                 (unsigned long long)hash);
          rows++;
        }
  fail |= rows != 280 || rays != 272160 || histories != rays || !visible_hits ||
          hit_errors || history_errors || tier_errors || census_errors || yaw_errors;
  memcpy(g_hist, history, sizeof history); g_hist_tick = hist_tick;
  recoil_restore_globals();
  figadapt_render_restore(&render);
  printf("fighit summary rows=%d rays=%d history=%d visible_hit=%d triangle_control=%d "
         "yaw_errors=%d step_mm=20 errors=%d/%d/%d/%d %s\n", rows, rays, histories,
         visible_hits, triangle_control, yaw_errors, hit_errors, history_errors,
         tier_errors, census_errors,
         fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

typedef struct {
  v3 wrist, centre, axis, lo, hi;
  int vertices, valid;
} figmotion_sample_t;

static figmotion_sample_t figmotion_sample(int view, int hand) {
  const fig_hand_diag_t *d = &g_fig_hand_diag[hand];
  figmotion_sample_t out = {d->wrist, d->contact_center, d->contact_axis,
                           {{1e9f, 1e9f, 1e9f}}, {{-1e9f, -1e9f, -1e9f}}, 0, 1};
  vmtrig_hand_range_t ranges[PROBE_MAX];
  int n = vmtrig_collect_hands(ranges, view);
  for (int i = 0; i < n; i++) if (ranges[i].hand == hand)
    for (int f = ranges[i].r[0]; f < ranges[i].r[1]; f += VERT_FLOATS) {
      v3 p = SCENE_V(f);
      out.valid &= !vmtrig_v3_nonfinite(p);
      for (int k = 0; k < 3; k++) {
        out.lo.e[k] = fminf(out.lo.e[k], p.e[k]);
        out.hi.e[k] = fmaxf(out.hi.e[k], p.e[k]);
      }
      out.vertices++;
    }
  out.valid &= out.vertices > 0 && d->palm_valid &&
      !vmtrig_v3_nonfinite(out.wrist) && !vmtrig_v3_nonfinite(out.centre) &&
      !vmtrig_v3_nonfinite(out.axis) && fabsf(v3_len(out.axis) - 1.0f) < 1e-3f;
  return out;
}

static float figmotion_gap(const figmotion_sample_t *a, const figmotion_sample_t *b) {
  return fmaxf(v3_len(v3_sub(a->wrist, b->wrist)),
      fmaxf(v3_len(v3_sub(a->centre, b->centre)),
        fmaxf(v3_len(v3_sub(a->lo, b->lo)), v3_len(v3_sub(a->hi, b->hi)))));
}

static float figmotion_proxy_depth(int view, int *hand, int *kind_out,
                                    const char **child) {
  vmtrig_hand_range_t ranges[PROBE_MAX];
  int n = vmtrig_collect_hands(ranges, view);
  float depth = 0;
  *hand = *kind_out = -1; *child = "none";
  for (int i = 0; i < n; i++)
    for (int f = ranges[i].r[0]; f < ranges[i].r[1]; f += VERT_FLOATS)
      for (int kind = GUN_CONTACT_RECEIVER; kind <= GUN_CONTACT_CHASSIS; kind++) {
        const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[kind];
        for (int box = 0; box < set->count; box++) {
          float candidate = -gun_contact_sd(SCENE_V(f), &set->box[box]);
          if (candidate > depth) {
            depth = candidate; *hand = ranges[i].hand;
            *kind_out = kind; *child = ranges[i].name;
          }
        }
      }
  return depth;
}

static int figmotion_exact_contact(int weapon, int view, gunanim_t ga,
                                    float brace, float *depth, uint64_t *work_total) {
  vmtrig_hand_range_t ranges[PROBE_MAX];
  int count = vmtrig_collect_hands(ranges, view);
  fig_hand_contact_t fire = fig_hand_contact_pose(FIG_HAND_FIRE, ga.hand, ga.bhand, 0);
  fig_hand_contact_t support = fig_hand_contact_pose(FIG_HAND_SUPPORT, ga.hand, ga.bhand, 0);
  if (view) support = fig_hand_brace_contact(support, fig_hand_brace_weight(brace));
  float h, w, ph, pw;
  int cross, proxy_cross, hk, wk, ck, hp, wp, cp, hm, wm, cm;
  int pairs, missing, overflow, unclassified;
  vmtrig_work_t work = {0};
  vmtrig_forbidden(weapon, vmtrig_contact_mask(fire), vmtrig_contact_mask(support),
      ranges, count, &h, &w, &cross, &ph, &pw, &proxy_cross,
      &hk, &wk, &ck, &hp, &wp, &cp, &hm, &wm, &cm,
      &work, &pairs, &missing, &overflow, &unclassified);
  if (!view && wk == GUN_CONTACT_STOCK && wp >= 0 && wm >= 0 &&
      g_fig_k == 12 && ga.hand >= 0.10f && ga.hand < 0.20f) {
    const gun_mesh_range_t *mesh = &g_gun_contact_mesh[wk].range[wm];
    for (int f = mesh->v0; f < mesh->v1; f += VERT_FLOATS) {
      v3 point = SCENE_V(f);
      float distance = -vmtrig_mesh_sd(point, ranges[wp].r, &work);
      if (distance >= w - 1e-5f) {
        v3 relative = v3_sub(point, g_vm_mdl_o);
        printf("figmotion stock_vertex world=%.6f/%.6f/%.6f local=%.6f/%.6f/%.6f depth_mm=%.3f\n",
            (double)point.x, (double)point.y, (double)point.z,
            (double)v3_dot(relative, g_vm_mdl_x),
            (double)v3_dot(relative, g_vm_mdl_y),
            (double)v3_dot(relative, g_vm_mdl_z), (double)(distance * 1000));
        break;
      }
    }
  }
  if (weapon == WPN_AR && view && hk == GUN_CONTACT_RECEIVER && hp >= 0 && hm >= 0 &&
      g_fig_k == 16 && ga.hand >= 0.10f && ga.hand < 0.20f) {
    const gun_mesh_range_t *mesh = &g_gun_contact_mesh[hk].range[hm];
    int range[2] = {mesh->v0, mesh->v1};
    for (int f = ranges[hp].r[0]; f < ranges[hp].r[1]; f += VERT_FLOATS) {
      v3 point = SCENE_V(f);
      float distance = -vmtrig_mesh_sd(point, range, &work);
      if (distance >= h - 1e-5f) {
        v3 relative = v3_sub(point, g_gun_body_c);
        printf("figmotion ar_vertex local=%.6f/%.6f/%.6f depth_mm=%.3f vertex=%d\n",
            (double)(v3_dot(relative, v3_norm(g_gun_body_e[0])) / GUN_GIRTH_3P),
            (double)(v3_dot(relative, v3_norm(g_gun_body_e[1])) / GUN_GIRTH_3P + 0.0085f),
            (double)(v3_dot(relative, v3_norm(g_gun_body_e[2])) / GUN_GIRTH_3P + 0.0700f),
            (double)(distance * 1000), (f - ranges[hp].r[0]) / VERT_FLOATS);
        break;
      }
    }
  }
  if (weapon == WPN_AR && view && wk == GUN_CONTACT_RECEIVER && wp >= 0 && wm >= 0 &&
      g_fig_k == 12 && ga.hand >= 0.10f && ga.hand < 0.20f) {
    const gun_mesh_range_t *mesh = &g_gun_contact_mesh[wk].range[wm];
    v3 low = {{1e6f, 1e6f, 1e6f}}, high = {{-1e6f, -1e6f, -1e6f}};
    for (int f = mesh->v0; f < mesh->v1; f += VERT_FLOATS) {
      v3 relative = v3_sub(SCENE_V(f), g_gun_body_c);
      v3 point = {{v3_dot(relative, v3_norm(g_gun_body_e[0])) / GUN_GIRTH_3P,
                  v3_dot(relative, v3_norm(g_gun_body_e[1])) / GUN_GIRTH_3P + 0.0085f,
                  v3_dot(relative, v3_norm(g_gun_body_e[2])) / GUN_GIRTH_3P + 0.0700f}};
      for (int j = 0; j < 3; j++) {
        low.e[j] = fminf(low.e[j], point.e[j]);
        high.e[j] = fmaxf(high.e[j], point.e[j]);
      }
    }
    printf("figmotion ar_receiver mesh=%d local_min=%.6f/%.6f/%.6f max=%.6f/%.6f/%.6f\n",
           wm, (double)low.x, (double)low.y, (double)low.z,
           (double)high.x, (double)high.y, (double)high.z);
  }
  *depth = fmaxf(*depth, fmaxf(fmaxf(h, w), fmaxf(ph, pw)));
  *work_total += work.n;
  int fail = h > VMTRIG_OPPOSITE_LIMIT || w > VMTRIG_OPPOSITE_LIMIT ||
      ph > VMTRIG_OPPOSITE_LIMIT || pw > VMTRIG_OPPOSITE_LIMIT ||
      cross || proxy_cross || missing || overflow || unclassified || work.overflow;
  if (fail && ph > 0.0f && weapon == WPN_SR && g_fig_k == 6) {
    for (int i = 0; i < count; i++)
      for (int f = ranges[i].r[0]; f < ranges[i].r[1]; f += VERT_FLOATS)
        for (int kind = GUN_CONTACT_RECEIVER; kind <= GUN_CONTACT_CHASSIS; kind++) {
          const gun_contact_proxy_set_t *set = &g_gun_contact_proxy[kind];
          for (int box = 0; box < set->count; box++) {
            float d = -gun_contact_sd(SCENE_V(f), &set->box[box]);
            if (d >= ph - 1e-6f)
              printf("figmotion proxy_vertex hand=%d child=%s vertex=%d kind=%d box=%d depth_mm=%.4f\n",
                  ranges[i].hand, ranges[i].name, (f - ranges[i].r[0]) / VERT_FLOATS,
                  kind, box, (double)(d * 1000));
          }
        }
  }
  if (fail && weapon == WPN_SR && ck >= 0 && cm >= 0 && g_fig_k <= 8) {
    const gun_mesh_range_t *mesh = &g_gun_contact_mesh[ck].range[cm];
    v3 low = {{1e6f, 1e6f, 1e6f}}, high = {{-1e6f, -1e6f, -1e6f}};
    float scale = view ? GUN_GIRTH_3P : 1.0f;
    for (int f = mesh->v0; f < mesh->v1; f += VERT_FLOATS) {
      v3 relative = v3_sub(SCENE_V(f), g_gun_body_c);
      v3 point = {{v3_dot(relative, v3_norm(g_gun_body_e[0])) / scale,
                  v3_dot(relative, v3_norm(g_gun_body_e[1])) / scale + 0.0130f,
                  v3_dot(relative, v3_norm(g_gun_body_e[2])) / scale + 0.0575f}};
      for (int j = 0; j < 3; j++) {
        low.e[j] = fminf(low.e[j], point.e[j]); high.e[j] = fmaxf(high.e[j], point.e[j]);
      }
    }
    printf("figmotion sr_mesh kind=%d mesh=%d min=%.6f/%.6f/%.6f max=%.6f/%.6f/%.6f\n",
           ck, cm, (double)low.x, (double)low.y, (double)low.z,
           (double)high.x, (double)high.y, (double)high.z);
  }
  if (fail)
    printf("figmotion contact_fail %s view=%d k=%d hand=%.4f bhand=%.4f bolt=%.4f brace=%.4f "
           "depth_mm=%.3f/%.3f/%.3f/%.3f cross=%d/%d kind=%d/%d/%d "
           "part=%d/%d/%d mesh=%d/%d/%d child=%s/%s/%s census=%d/%d/%d\n", WPN_DEF[weapon].name, view, g_fig_k,
           (double)ga.hand, (double)ga.bhand, (double)ga.bolt, (double)brace, (double)(h * 1000), (double)(w * 1000),
           (double)(ph * 1000), (double)(pw * 1000), cross, proxy_cross, hk, wk, ck,
           hp, wp, cp, hm, wm, cm, hp >= 0 ? ranges[hp].name : "none",
           wp >= 0 ? ranges[wp].name : "none", cp >= 0 ? ranges[cp].name : "none",
           missing, overflow, unclassified);
  return fail;
}

static int figmotion_helpers(void) {
  int fail = 0;
  float saved_girth = g_gun_girth;
  v3 origin = {{0, 0, 0}}, right = {{1, 0, 0}}, up = {{0, 1, 0}};
  v3 forward = {{0, 0, -1}};
  for (int weapon = 0; weapon < WPN_COUNT; weapon++)
    for (int view = 0; view < 2; view++) {
      g_gun_girth = view ? GUN_GIRTH_3P : 1.0f;
      int fore = view ? GH_FORE3 : GH_FORE;
      v3 c, a, expected_c, expected_a; float r, expected_r;
      gun_hold(weapon, fore, origin, right, up, forward,
               &expected_c, &expected_a, &expected_r);
      gun_reload_hold(weapon, fore, origin, right, up, forward, 0, 0,
                      &c, &a, &r);
      fail |= v3_len(v3_sub(c, expected_c)) > 1e-6f ||
              v3_len(v3_sub(a, expected_a)) > 1e-6f || r != expected_r;
      v3 previous = c;
      for (int step = 1; step <= 1000; step++) {
        gun_reload_hold(weapon, fore, origin, right, up, forward,
                        (float)step / 1000.0f, 0, &c, &a, &r);
        fail |= vmtrig_v3_nonfinite(c) || vmtrig_v3_nonfinite(a) ||
                f_is_nonfinite(r) || r <= 0 || fabsf(v3_len(a) - 1.0f) > 1e-3f ||
                v3_len(v3_sub(c, previous)) > 0.002f;
        previous = c;
      }
      gun_hold(weapon, GH_RELD, origin, right, up, forward,
               &expected_c, &expected_a, &expected_r);
      fail |= v3_len(v3_sub(c, expected_c)) > 1e-6f ||
              v3_len(v3_sub(a, expected_a)) > 1e-6f || fabsf(r - expected_r) > 1e-6f;
    }
  static const v3 DIR[] = {{{0, 0, 0}}, {{0, 1, 0}}, {{0, 0, -1}},
                           {{1, 0, 0}}, {{0, 0, 1}}, {{1e-10f, 0, -1e-10f}}};
  float previous_weight = 0;
  for (int step = 0; step <= 1000; step++) {
    float weight = fig_hand_brace_weight((float)step / 1000.0f);
    fail |= f_is_nonfinite(weight) || weight < previous_weight || weight < 0 || weight > 1;
    previous_weight = weight;
    for (unsigned direction = 0; direction < sizeof DIR / sizeof *DIR; direction++) {
      v3 c = {{0.1f, 0.2f, 0.3f}}, a = {{-1, 0, 0}};
      float r = 0.025f;
      fig_hand_brace_target(origin, DIR[direction], right, weight, &c, &a, &r);
      fail |= vmtrig_v3_nonfinite(c) || vmtrig_v3_nonfinite(a) ||
              f_is_nonfinite(r) || r < 0.0129f || r > 0.0251f ||
              fabsf(v3_len(a) - 1.0f) > 1e-3f;
    }
  }
  fig_hand_contact_t held = fig_hand_contact_pose(FIG_HAND_SUPPORT, 0.4f, 0, 0);
  fig_hand_contact_t partial = fig_hand_brace_contact(held, 0.5f);
  fail |= partial.from != held.from || partial.to != held.to || partial.mix != held.mix ||
      fabsf(fig_hand_contact_weight(partial, FIG_HAND_CONTACT_FORE) - 0.30f) > 1e-6f ||
      fabsf(fig_hand_contact_weight(partial, FIG_HAND_CONTACT_MAG) - 0.20f) > 1e-6f ||
      fabsf(fig_hand_contact_weight(partial, FIG_HAND_CONTACT_NONE) - 0.50f) > 1e-6f;
  for (int mix_step = 1; mix_step < 4; mix_step++) {
    float mix = (float)mix_step * 0.25f;
    v3 left = {{-0.95001f, sqrtf(1.0f - 0.95001f * 0.95001f), 0}};
    v3 right_of_branch = {{-0.94999f, sqrtf(1.0f - 0.94999f * 0.94999f), 0}};
    v3 a = fig_hand_axis_blend(right, left, up, mix);
    v3 b = fig_hand_axis_blend(right, right_of_branch, up, mix);
    fail |= vmtrig_v3_nonfinite(a) || vmtrig_v3_nonfinite(b) ||
            v3_len(v3_sub(a, b)) > 0.001f;
  }
  g_gun_girth = saved_girth;
  return fail;
}

// Fixed 120 Hz player movement, weapon and presentation integrators are replayed
// against unequal render cadence. Interpolation uses each frame's exact rational
// remainder; 240 FPS therefore really renders between ticks, and 23 FPS groups
// alternate naturally. No presentation callback may change sim or event state.
static int figmotion_cadence(void) {
  static const int FPS[] = {23, 30, 60, 120, 240};
  static proof_world_t world;
  proof_world_save(&world);
  static mark_t marks[MAX_MARKS];
  memcpy(marks, g_marks, sizeof marks);
  int mark_head = g_mark_head;
  uint64_t rng_vfx = g_rng_vfx;
  uint8_t spawn[MAX_ENTS]; memcpy(spawn, g_spawn_seq, sizeof spawn);
  int corpse_q[MAX_HUMANS]; memcpy(corpse_q, g_player_corpse_q, sizeof corpse_q);
  int fail = 0, total_renders = 0, total_pose_only = 0;
  char reference[65] = {0};
  unsigned reference_events[EV_LAND + 1] = {0};
  for (unsigned cadence = 0; cadence < sizeof FPS / sizeof *FPS; cadence++) {
    recoil_stage(1, WPN_AR, 0);
    g_num_solids = 0;  // the shared arena floor remains; bullets cannot edit scenery
    g_rng_bot = UINT64_C(0x31415926);
    memset(g_spawn_seq, 0, sizeof spawn);
    memset(g_player_corpse_q, 0, sizeof corpse_q);
    g_rng_vfx = rng_vfx;
    memcpy(g_marks, marks, sizeof marks); g_mark_head = mark_head;
    player_t *p = &g_players[0];
    anim_t *a = &g_player_anim[0];
    p->pos = p->prev_pos = (v3){{0, 0, 0}};
    p->grounded = p->prev_grounded = 1;
    p->wp.ammo[WPN_AR] = 4; p->wp.ammo[WPN_SR] = 1;
    anim_reset(a, p->pos, p->yaw, WPN_AR);
    sha256_t sha; sha256_init(&sha);
    unsigned events[EV_LAND + 1] = {0};
    int tick = 0, renders = 0, pose_only = 0, state_fail = 0, queue_fail = 0;
    int slide_ticks = 0, air_ticks = 0, dead_ticks = 0;
    for (int frame = 0; frame < FPS[cadence] * 6; frame++) {
      int numerator = (frame + 1) * TICK_HZ;
      int target = numerator / FPS[cadence];
      while (tick < target) {
        int held[IN_COUNT] = {0};
        held[IN_FWD] = tick < 65 || (tick >= 210 && tick < 385) ||
                           (tick >= 525 && tick < 590);
        held[IN_RIGHT] = tick >= 240 && tick < 270;
        held[IN_LEFT] = tick >= 270 && tick < 295;
        held[IN_FIRE] = (tick >= 10 && tick < 60) || tick == 110 || tick == 130 ||
                         tick == 400 || (tick >= 550 && tick < 565);
        held[IN_RELOAD] = tick == 70 || tick == 150 || tick == 220 || tick == 630;
        held[IN_SWAP] = tick == 90 || tick == 185 || tick == 675;
        held[IN_ADS] = (tick >= 230 && tick < 250) || (tick >= 270 && tick < 290) ||
                        (tick >= 420 && tick < 460);
        held[IN_LEAN_L] = tick >= 240 && tick < 270;
        held[IN_LEAN_R] = tick >= 270 && tick < 295;
        held[IN_CROUCH] = tick >= 300 && tick < 335;
        held[IN_JUMP] = tick == 360;
        if (tick == 410)
          anim_hit(a, p->pos, v3_add(p->pos, (v3){{2, 1, 2}}),
                   v3_add(p->pos, (v3){{0, 1.2f, 0}}), 0);
        if (tick == 480) { p->alive = 0; p->respawn_t = 30; }
        g_num_events = 0;
        g_tick++;
        player_move_tick(p, 0, held, 0, 0);
        player_present_tick(p, 0);
        slide_ticks += p->sliding != 0;
        air_ticks += p->grounded == 0;
        dead_ticks += !p->alive;
        char state[65]; fixture_state_hash(state);
        sha256_update(&sha, state, 64);
        sha256_update(&sha, &p->wp, sizeof p->wp);
        for (int e = 0; e < g_num_events; e++) {
          const event_t *event = &g_events[e];
          if ((unsigned)event->type > EV_LAND) { fail = 1; continue; }
          events[event->type]++;
          int fields[] = {event->type, event->i, event->k, event->wpn,
                          event->src, event->dst};
          sha256_update(&sha, fields, sizeof fields);
          sha256_update(&sha, &event->a, sizeof event->a);
          sha256_update(&sha, &event->b, sizeof event->b);
          sha256_update(&sha, &event->f, sizeof event->f);
          sha256_update(&sha, &event->g, sizeof event->g);
          sha256_update(&sha, &event->pen, sizeof event->pen);
        }
        tick++;
      }
      float alpha = (float)(numerator % FPS[cadence]) / (float)FPS[cadence];
      player_t before_player = *p;
      anim_t before_anim = *a;
      int event_count = g_num_events;
      event_t event_copy[MAX_EVENTS]; memcpy(event_copy, g_events, sizeof event_copy);
      char before_hash[65], after_hash[65]; fixture_state_hash(before_hash);
      if (p->alive) {
        anim_t pure = *a;
        pose_t original_pose = pure.pose;
        int idle = gun_pose_state(&p->wp, a, p->vel, p->grounded, p->sliding);
        skel_solve(&pure, p->pos, p->yaw, p->pitch, p->wp.cur, idle, SKEL_POSE_ONLY);
        pure.pose = original_pose;
        state_fail += memcmp(&pure, a, sizeof pure) != 0;
        pose_only++;
        figadapt_draw_at(p, a, 0, FIG_HAND_DETAIL_K, gun_anim(&p->wp), alpha);
        figadapt_draw_at(p, a, 1, FIG_HAND_DETAIL_K, gun_anim(&p->wp), alpha);
      }
      fixture_state_hash(after_hash);
      state_fail += strcmp(before_hash, after_hash) != 0 ||
                    memcmp(p, &before_player, sizeof before_player) != 0 ||
                    memcmp(a, &before_anim, sizeof before_anim) != 0;
      queue_fail += g_num_events != event_count ||
                    memcmp(event_copy, g_events, sizeof event_copy) != 0;
      renders++;
    }
    char digest[65]; sha256_hex(&sha, digest);
    if (!cadence) {
      memcpy(reference, digest, sizeof reference);
      memcpy(reference_events, events, sizeof events);
    }
    int same = !strcmp(reference, digest) && !memcmp(reference_events, events, sizeof events);
    fail |= !same || state_fail || queue_fail || tick != 720 || renders != FPS[cadence] * 6 ||
            !events[EV_FIRE] || events[EV_RELOAD] < 2 || events[EV_RELOAD_CANCEL] < 2 ||
            events[EV_RESPAWN] != 1 || !slide_ticks || !air_ticks || !dead_ticks;
    printf("figmotion cadence fps=%d ticks=%d renders=%d pose_only=%d fire=%u reload=%u "
           "cancel=%u respawn=%u slide=%d air=%d dead=%d errors=%d/%d same=%d hash=%s\n",
           FPS[cadence], tick, renders, pose_only, events[EV_FIRE], events[EV_RELOAD],
           events[EV_RELOAD_CANCEL], events[EV_RESPAWN], slide_ticks, air_ticks, dead_ticks,
           state_fail, queue_fail, same, digest);
    total_renders += renders; total_pose_only += pose_only;
  }
  memcpy(g_spawn_seq, spawn, sizeof spawn);
  memcpy(g_player_corpse_q, corpse_q, sizeof corpse_q);
  memcpy(g_marks, marks, sizeof marks); g_mark_head = mark_head; g_rng_vfx = rng_vfx;
  proof_world_restore(&world);
  printf("figmotion cadence_summary frames=%d pose_only=%d %s\n", total_renders,
         total_pose_only, fail ? "FAIL" : "ok");
  return fail;
}

// Both endpoint measurements come from actual FP/3P emitted hands. The tiny
// interval around hand=.5 catches the historical renderer branch even if a pure
// helper is smooth. Full tick/cancel maxima remain explicit measurements.
static void figmotion_proof(void) {
  recoil_save_globals();
  figadapt_render_save_t render = figadapt_render_save();
  recoil_stage(1, WPN_AR, 0);
  static const int TIER[] = {16, 12, 8, 6};
  int fail = figmotion_helpers(), rows = 0, threshold_fail = 0, finite_fail = 0;
  int pose_fail = 0, event_fail = 0, cancel_fail = 0, cancellations = 0;
  int proxy_fail = 0, exact_fail = 0, exact_rows = 0, bolt_rows = 0;
  float proxy_max = 0, exact_max = 0;
  uint64_t exact_work = 0;
  float threshold_max = 0, brace_max = 0;
  for (int weapon = 0; weapon < WPN_COUNT; weapon++)
    for (int view = 0; view < 2; view++)
      for (unsigned tier = 0; tier < sizeof TIER / sizeof *TIER; tier++) {
        player_t *p = &g_players[0];
        anim_t *a = &g_player_anim[0];
        figadapt_stage(p, a, weapon, 0, 0, 0, 0, 0);
        gunanim_t ga = gun_anim(&p->wp);
        ga.hand = 0.4999f;
        figadapt_draw(p, a, view, TIER[tier], ga);
        figmotion_sample_t lo = figmotion_sample(view, FIG_HAND_SUPPORT);
        ga.hand = 0.5001f;
        figadapt_draw(p, a, view, TIER[tier], ga);
        figmotion_sample_t hi = figmotion_sample(view, FIG_HAND_SUPPORT);
        float gap = figmotion_gap(&lo, &hi);
        printf("figmotion boundary %s view=%d k=%d wrist=%.3f centre=%.3f lo=%.3f hi=%.3f axis=%.6f\n",
            WPN_DEF[weapon].name, view, TIER[tier],
            (double)(v3_len(v3_sub(lo.wrist, hi.wrist)) * 1000),
            (double)(v3_len(v3_sub(lo.centre, hi.centre)) * 1000),
            (double)(v3_len(v3_sub(lo.lo, hi.lo)) * 1000),
            (double)(v3_len(v3_sub(lo.hi, hi.hi)) * 1000),
            (double)v3_len(v3_sub(lo.axis, hi.axis)));
        threshold_max = fmaxf(threshold_max, gap);
        finite_fail += !lo.valid || !hi.valid;
        threshold_fail += gap > 0.002f || v3_len(v3_sub(lo.axis, hi.axis)) > 0.02f;
        if (weapon == WPN_SR) {
          gunanim_t bolt = {0};
          bolt.bolt = 0.5f; bolt.cyc = 0.5f; bolt.bhand = 0.3499f;
          figadapt_draw(p, a, view, TIER[tier], bolt);
          figmotion_sample_t fire_lo = figmotion_sample(view, FIG_HAND_FIRE);
          bolt.bhand = 0.3501f;
          figadapt_draw(p, a, view, TIER[tier], bolt);
          figmotion_sample_t fire_hi = figmotion_sample(view, FIG_HAND_FIRE);
          finite_fail += !fire_lo.valid || !fire_hi.valid;
          threshold_fail += figmotion_gap(&fire_lo, &fire_hi) > 0.002f ||
              v3_len(v3_sub(fire_lo.axis, fire_hi.axis)) > 0.02f;
          printf("figmotion bolt_style view=%d k=%d wrist_mm=%.3f centre_mm=%.3f "
                 "bounds_mm=%.3f\n", view, TIER[tier],
              (double)(v3_len(v3_sub(fire_lo.wrist, fire_hi.wrist)) * 1000),
              (double)(v3_len(v3_sub(fire_lo.centre, fire_hi.centre)) * 1000),
              (double)(fmaxf(v3_len(v3_sub(fire_lo.lo, fire_hi.lo)),
                             v3_len(v3_sub(fire_lo.hi, fire_hi.hi))) * 1000));
        }
        float tick_max = 0, fire_tick_max = 0, cancel_max = 0;
        float row_proxy = 0;
        int proxy_tick = -1, proxy_tac = -1, proxy_hand = -1, proxy_kind = -1;
        const char *proxy_child = "none";
        int tick_samples = 0;
        for (int tactical = 0; tactical < 2; tactical++) {
          int duration = wpn_reload_ticks(weapon, tactical);
          p->wp.reload_tac = tactical;
          p->wp.reload_t = duration;
          figmotion_sample_t previous = {0}, previous_fire = {0};
          unsigned exact_seen = 0, bolt_up = 0, bolt_down = 0;
          float previous_bolt = 0.0f;
          static const float EXACT_HAND[] = {0.10f, 0.25f, 0.50f, 0.75f, 0.90f};
          for (int tick = 0; tick <= duration; tick++) {
            ga = gun_anim(&p->wp);
            figadapt_solve(p, a);
            anim_t before = *a;
            player_t before_player = *p;
            int before_events = g_num_events;
            figadapt_draw(p, a, view, TIER[tier], ga);
            figmotion_sample_t now = figmotion_sample(view, FIG_HAND_SUPPORT);
            figmotion_sample_t fire_now = figmotion_sample(view, FIG_HAND_FIRE);
            finite_fail += !now.valid || !fire_now.valid;
            int sample_hand, sample_kind; const char *sample_child;
            float proxy_depth = figmotion_proxy_depth(view, &sample_hand,
                                                        &sample_kind, &sample_child);
            if (proxy_depth > row_proxy) {
              row_proxy = proxy_depth; proxy_tick = tick; proxy_tac = tactical;
              proxy_hand = sample_hand; proxy_kind = sample_kind; proxy_child = sample_child;
            }
            proxy_max = fmaxf(proxy_max, proxy_depth);
            proxy_fail += proxy_depth > VMTRIG_OPPOSITE_LIMIT;
            for (unsigned station = 0; station < sizeof EXACT_HAND / sizeof *EXACT_HAND; station++)
              if (!(exact_seen & (1u << station)) && ga.hand >= EXACT_HAND[station]) {
                exact_seen |= 1u << station;
                exact_fail += figmotion_exact_contact(weapon, view, ga, 0, &exact_max, &exact_work);
                exact_rows++;
              }
            if (weapon == WPN_SR) {
              for (unsigned station = 0; station < sizeof EXACT_HAND / sizeof *EXACT_HAND; station++) {
                unsigned bit = 1u << station;
                int rising = ga.bhand > previous_bolt && ga.bhand >= EXACT_HAND[station] && !(bolt_up & bit);
                int falling = ga.bhand < previous_bolt && ga.bhand <= EXACT_HAND[station] && !(bolt_down & bit);
                if (rising || falling) {
                  if (rising) bolt_up |= bit;
                  else bolt_down |= bit;
                  exact_fail += figmotion_exact_contact(weapon, view, ga, 0, &exact_max, &exact_work);
                  exact_rows++; bolt_rows++;
                }
              }
              previous_bolt = ga.bhand;
            }
            pose_fail += memcmp(a, &before, sizeof before) != 0 ||
                         memcmp(p, &before_player, sizeof before_player) != 0;
            event_fail += g_num_events != before_events;
            if (tick) {
              tick_max = fmaxf(tick_max, figmotion_gap(&previous, &now));
              fire_tick_max = fmaxf(fire_tick_max, figmotion_gap(&previous_fire, &fire_now));
            }
            previous = now; previous_fire = fire_now;
            // A real reload cancellation changes weapon identity and starts the
            // raise gate. Its displacement is not a same-weapon continuity error.
            if (ga.hand > 0.01f) {
              player_t save_player = *p;
              anim_t save_anim = *a;
              int save_events = g_num_events;
              int held[IN_COUNT] = {0}; held[IN_SWAP] = 1;
              p->prev_held[IN_SWAP] = 0;
              g_num_events = 0;
              weapon_tick(p, 0, held);
              cancel_fail += g_num_events != 1 || g_events[0].type != EV_RELOAD_CANCEL ||
                  g_events[0].wpn != weapon || p->wp.cur == weapon ||
                  p->wp.reload_t != 0 || p->wp.switch_t != GUN_SWITCH_TICKS;
              vm_pose_t cancelled_pose = vm_stage_pose(p, &p->vm, &p->wp, 1.0f, 0.0f);
              cancel_fail += cancelled_pose.sw < 0.999f;
              skel_solve(a, p->pos, p->yaw, p->pitch, p->wp.cur,
                         GUN_POSE_BLOCKED, SKEL_POSE_ONLY);
              a->prev_pose = a->pose;
              figadapt_draw(p, a, view, TIER[tier], gun_anim(&p->wp));
              figmotion_sample_t stop = figmotion_sample(view, FIG_HAND_SUPPORT);
              finite_fail += !stop.valid;
              cancel_max = fmaxf(cancel_max, figmotion_gap(&now, &stop));
              *p = save_player; *a = save_anim; g_num_events = save_events;
              cancellations++;
            }
            gun_reload_tick(&p->wp);
            tick_samples++;
          }
          if (weapon == WPN_SR && (bolt_up != 31u || bolt_down != 31u)) exact_fail++;
        }
        if (view) {
          ga = gun_anim(&p->wp);
          ga.hand = 0.4f;  // reload + slide is legal and keeps both contact endpoints
          a->was_slide = 1;
          a->slide_dir = a->slide_pd = (v3){{0, 0, -1}};
          a->slide_v = 1.0f;
          for (int direction = 0; direction < 8; direction++) {
            float theta = (float)direction * F_TAU / 8.0f;
            a->slide_dir = a->slide_pd = (v3){{sinf(theta), 0, -cosf(theta)}};
            a->slide_s = 0.4999f;
            skel_solve(a, p->pos, p->yaw, p->pitch, weapon, GUN_POSE_IDLE, SKEL_POSE_ONLY);
            a->prev_pose = a->pose;
            figadapt_draw(p, a, 1, TIER[tier], ga);
            lo = figmotion_sample(1, FIG_HAND_SUPPORT);
            a->slide_s = 0.5001f;
            skel_solve(a, p->pos, p->yaw, p->pitch, weapon, GUN_POSE_IDLE, SKEL_POSE_ONLY);
            a->prev_pose = a->pose;
            figadapt_draw(p, a, 1, TIER[tier], ga);
            hi = figmotion_sample(1, FIG_HAND_SUPPORT);
            gap = figmotion_gap(&lo, &hi);
            brace_max = fmaxf(brace_max, gap);
            finite_fail += !lo.valid || !hi.valid;
            threshold_fail += gap > 0.002f || v3_len(v3_sub(lo.axis, hi.axis)) > 0.02f;
            if (direction == 0) {
              exact_fail += figmotion_exact_contact(weapon, 1, ga, a->slide_s,
                                                     &exact_max, &exact_work);
              exact_rows++;
            }
          }
        }
        printf("figmotion proxy %s view=%d k=%d max_mm=%.3f tick=%d tactical=%d "
               "hand=%d kind=%d child=%s\n", WPN_DEF[weapon].name, view, TIER[tier],
               (double)(row_proxy * 1000), proxy_tick, proxy_tac, proxy_hand, proxy_kind, proxy_child);
        printf("figmotion %s view=%d k=%d ticks=%d tick_max_mm=%.3f fire_tick_max_mm=%.3f swap_displacement_mm=%.3f\n",
               WPN_DEF[weapon].name, view, TIER[tier], tick_samples,
               (double)(tick_max * 1000), (double)(fire_tick_max * 1000), (double)(cancel_max * 1000));
        rows++;
      }
  fail |= figmotion_cadence();
  fail |= rows != 16 || threshold_fail || finite_fail || pose_fail || event_fail ||
          cancel_fail || !cancellations || proxy_fail || exact_fail || exact_rows != 328 || bolt_rows != 160;
  recoil_restore_globals();
  figadapt_render_restore(&render);
  printf("figmotion contacts exact_rows=%d bolt_rows=%d work=%llu proxy_max_mm=%.3f exact_max_mm=%.3f "
         "errors=%d/%d\n", exact_rows, bolt_rows, (unsigned long long)exact_work,
         (double)(proxy_max * 1000), (double)(exact_max * 1000), proxy_fail, exact_fail);
  printf("figmotion summary rows=%d reload_boundary_mm=%.3f brace_boundary_mm=%.3f "
         "cancellations=%d errors=%d/%d/%d/%d/%d %s\n", rows, (double)(threshold_max * 1000),
         (double)(brace_max * 1000), cancellations, threshold_fail, finite_fail,
         pose_fail, event_fail, cancel_fail,
         fail ? "FAIL" : "ok");
  if (fail) exit(1);
}

// Feedback proofs use real event admission, packet decoding, drawing and mixing.
static int feedback_cases, feedback_failed;
static void feedback_expect(int okay, const char *name) {
  feedback_cases++;
  if (!okay) { feedback_failed++; printf("feedbackproof FAIL case=%s\n", name); }
}
static int feedback_damage_count(void) {
  int count = 0;
  for (int i = 0; i < DMG_SLOTS; i++) count += g_dmg[i].life > 0;
  return count;
}
static void feedback_stage(int me) {
  audio_trace_end(); audiocheck_reset(); audio_feedback_reset(); vfx_match_reset();
  g_local_ent = me; g_online = 0; g_predicting = g_prediction_replay = 0;
  g_num_events = 0; g_tick = 100; g_headless = 1; g_num_solids = 0;
  memset(&g_match, 0, sizeof g_match); g_match.nbots = 2; g_cfg.sp_bots = 2;
  memset(&g_cl, 0, sizeof g_cl); memset(g_cl_pred, 0, sizeof g_cl_pred);
  memset(g_humans_on, 0, sizeof g_humans_on);
  int human = human_of_ent(me); g_humans_on[human] = 1;
  player_t *p = &g_players[human]; player_spawn(p);
  p->pos = p->prev_pos = (v3){{0,0,0}};
  p->yaw = p->pitch = 0; p->grounded = p->prev_grounded = 1;
  anim_reset(&g_player_anim[human], p->pos, 0, WPN_AR);
  for (int i = 0; i < 2; i++) {
    memset(&g_bots[i], 0, sizeof g_bots[i]);
    g_bots[i].active = g_bots[i].alive = 1; g_bots[i].hp = 100;
    g_bots[i].eye = EYE_STAND; g_bots[i].grounded = 1;
    g_bots[i].pos = g_bots[i].prev_pos = (v3){{(float)i*3,0,-20}};
    anim_reset(&g_bots[i].anim, g_bots[i].pos, 0, WPN_AR);
  }
  g_listen = player_eye_pos(p); g_listen_r = (v3){{1,0,0}};
  g_menu_open = 0; g_home = 0; g_hud_visible = 1; g_cfg.show_fps = 0;
  g_cfg.third_person = 0; g_vmorb.on = 0;
}
static event_t feedback_hit(int src, int dst, int head, int hp, v3 from) {
  return (event_t){.type=head ? EV_HIT_HEAD : EV_HIT, .src=src,.dst=dst,
    .i=25,.k=hp,.a={{0,1.5f,-20}},.b=from};
}
static void feedback_push(event_t e) {
  int headless = g_headless; g_headless = 0; ev_push(e); g_headless = headless;
}
static int feedback_cues(int id) {
  int count = 0;
  for (int i = 0; i < g_audio_trace_n; i++) count += g_audio_trace[i].id == id;
  return count;
}
static void feedback_drain(void) { audio_frame_drain(local_player()); }
static void feedback_geometry(void) {
  frame_cam_t camera = {.eye={{0,0,0}}};
  static const v3 origin[] = {{{0,0,-10}},{{10,0,-10}},{{10,0,0}},{{10,0,10}},
    {{0,0,10}},{{-10,0,10}},{{-10,0,0}},{{-10,0,-10}}};
  static const float expect_x[] = {0,.7071068f,1,.7071068f,0,-.7071068f,-1,-.7071068f};
  static const float expect_y[] = {-1,-.7071068f,0,.7071068f,1,.7071068f,0,-.7071068f};
  for (int i = 0; i < 8; i++) {
    float a = 0; int valid = rf_damage_bearing(&camera,origin[i],&a);
    feedback_expect(valid && fabsf(sinf(a)-expect_x[i]) < 1e-5f &&
      fabsf(-cosf(a)-expect_y[i]) < 1e-5f, "cardinal-diagonal-sign");
  }
  camera.yaw = F_PI*.5f; float a = 0;
  feedback_expect(rf_damage_bearing(&camera,origin[0],&a) && sinf(a)<-.999f,
    "turn-after-damage");
  camera.yaw = F_PI-.0001f; float aa = 0, ab = 0;
  int okay = rf_damage_bearing(&camera,origin[0],&aa); camera.yaw = -F_PI-.0001f;
  okay &= rf_damage_bearing(&camera,origin[0],&ab);
  feedback_expect(okay && fabsf(sinf(aa)-sinf(ab))<1e-5f && fabsf(cosf(aa)-cosf(ab))<1e-5f,
    "yaw-wrap");
  camera.yaw = 0; camera.pitch = F_PI*.25f;
  feedback_expect(rf_damage_bearing(&camera,(v3){{0,10,-10}},&a) && cosf(a)>.999f,
    "pitched-front");
  feedback_expect(rf_damage_bearing(&camera,(v3){{0,-10,10}},&a) && cosf(a)<-.999f,
    "pitched-rear");
  camera.pitch = 0; camera.roll = F_PI*.5f;
  feedback_expect(rf_damage_bearing(&camera,(v3){{0,-10,0}},&a) && sinf(a)>.999f,
    "rolled-camera-right");
  camera.roll = 0;
  feedback_expect(!rf_damage_bearing(&camera,(v3){{0,10,0}},&a), "vertical-no-invented-bearing");
  feedback_expect(!rf_damage_bearing(&camera,camera.eye,&a), "zero-no-invented-bearing");
  uint32_t badbits[] = {UINT32_C(0x7fc00000),UINT32_C(0x7f800000),UINT32_C(0xff800000)};
  for (int i=0;i<3;i++) {
    v3 bad=origin[0]; memcpy(&bad.x,&badbits[i],sizeof bad.x);
    feedback_expect(!rf_damage_bearing(&camera,bad,&a), "nonfinite-origin-rejected");
  }
  camera.eye=(v3){{-3e38f,0,0}};
  feedback_expect(rf_damage_bearing(&camera,(v3){{3e38f,0,0}},&a) && sinf(a)>.999f,
    "large-finite-normalisation");
  camera.eye=(v3){{3,2,1}};
  feedback_expect(rf_damage_bearing(&camera,(v3){{3,2,-9}},&a) && cosf(a)>.999f,
    "translated-camera");

  feedback_stage(0); camera=(frame_cam_t){.eye={{0,0,0}},.fovy=F_PI*.5f,.aspect=16.0f/9};
  event_t hit=feedback_hit(1,0,0,50,(v3){{0,0,-10}}); hit.a=(v3){{0,0,0}};
  feedback_push(hit); hit.src=2;hit.b.x=.5f;feedback_push(hit);
  feedback_expect(feedback_damage_count()==2,"nearby-distinct-attackers");
  hit.src=1;hit.b.x=8;feedback_push(hit);
  feedback_expect(feedback_damage_count()==2,"same-moving-source-refresh");
  for(int i=0;i<DMG_SLOTS;i++) g_dmg[i]=(typeof(g_dmg[0])){origin[2*i],i+1,DMG_TICKS};
  int width=R.width,height=R.height; static const int size[][2]={{640,360},{700,300},{1024,768},{1280,720},{1280,800},{2560,1080}};
  for(size_t k=0;k<sizeof size/sizeof *size;k++) {
    R.width=size[k][0];R.height=size[k][1];ui_begin();long drops=g_ui_drops;
    rf_damage_marks(&camera);int good=g_ui_nverts==DMG_SLOTS*UI_DAMAGE_SEGMENTS*12 && g_ui_drops==drops;
    for(int v=0;v<g_ui_nverts;v++) {
      float x=g_ui_vb[v*9]/g_uis,y=g_ui_vb[v*9+1]/g_uis;
      float dx=(x-g_ui_vw*.5f)/(g_ui_vw*.5f),dy=(y-g_ui_vh*.5f)/(g_ui_vh*.5f);
      good &= !f_is_nonfinite(x)&&!f_is_nonfinite(y)&&x>=0&&y>=0&&x<=g_ui_vw&&y<=g_ui_vh&&dx*dx+dy*dy>.45f;
    }
    feedback_expect(good,"actual-damage-vertices-peripheral-bounded");
  }
  R.width=width;R.height=height;
  // The surfaceless context has no drawable default framebuffer. Exercise the
  // real HUD on the harness target, then restore the caller's draw destination.
  GLint old_draw=0, old_view[4];
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
  glGetIntegerv(GL_VIEWPORT,old_view);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,R.present_fbo);
  glViewport(0,0,width,height);
  feedback_expect(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,
    "hud-fixture-complete-target");
  rf_hud_pass(local_player(),&camera,120,0);int with=g_ui_nverts;
  memset(g_dmg,0,sizeof g_dmg);rf_hud_pass(local_player(),&camera,120,0);
  feedback_expect(with-g_ui_nverts==DMG_SLOTS*UI_DAMAGE_SEGMENTS*12,"real-hud-emits-damage-owner");
  feedback_expect(glGetError()==GL_NO_ERROR,"hud-fixture-real-draw-no-gl-error");
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,(GLuint)old_draw);
  glViewport(old_view[0],old_view[1],old_view[2],old_view[3]);
  GLint restored_draw=0, restored_view[4];
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&restored_draw);
  glGetIntegerv(GL_VIEWPORT,restored_view);
  feedback_expect(restored_draw==old_draw&&!memcmp(old_view,restored_view,sizeof old_view),
    "hud-fixture-restores-draw-destination");
  float previous=1;
  for(int life=DMG_TICKS;life>=0;life--) {
    float ink=vfx_damage_ink(life);
    feedback_expect(ink>=0&&ink<=previous && (life<=UI_DAMAGE_FADE || ink==1),"hold-monotone-fade");previous=ink;
  }
  feedback_expect(vfx_damage_ink(0)==0,"fade-expires-exactly");
}
static void feedback_events(void) {
  float pcm[400*2];
  for(int me_index=0;me_index<2;me_index++) {
    int me=me_index?ent_of_human(MAX_HUMANS-1):0;
    feedback_stage(me);feedback_expect(audio_trace_begin(),"trace-begin");
    event_t hit=feedback_hit(me,1,0,50,g_listen);
    for(int replay=0;replay<2;replay++) {
      g_predicting=1;g_prediction_replay=replay;feedback_push(hit);feedback_drain();
      feedback_expect(!g_hitmark_arm&&!g_hitmark_t&&!g_feed[0].life&&!g_audio_trace_n,"prediction-replay-silent");
    }
    g_predicting=g_prediction_replay=0;
    event_t miss={.type=EV_FIRE,.src=me,.wpn=WPN_AR,.i=1,.a={{0,1.5f,-20}},.b={{0,0,-1}},.f=20};
    feedback_push(miss);feedback_drain();
    feedback_expect(!g_hitmark_arm&&!g_hitmark_t&&!g_feed[0].life,"miss-not-confirmed");
    audio_trace_end();audiocheck_reset();feedback_expect(audio_trace_begin(),"trace-rebegin");
    feedback_push(hit);feedback_drain();
    feedback_expect(g_hitmark_arm>0 && g_hitmark_pend==HM_BODY && feedback_cues(SFX_HIT)==1,"own-body-confirmation");
    hit.src=1;hit.dst=2;feedback_push(hit);feedback_drain();
    feedback_expect(feedback_cues(SFX_HIT)==1 && !feedback_damage_count(),"foreign-pair-not-local");
    hit.src=1;hit.dst=me;hit.b=(v3){{-10,1.5f,0}};hit.a=g_listen;
    feedback_push(hit);feedback_drain();audio_mix(pcm,400,2);
    int hurt=0;for(int v=0;v<MAX_VOICES;v++)if(g_voices[v].active&&g_voices[v].id==SFX_HURT)hurt|=g_voices[v].gl>g_voices[v].gr;
    feedback_expect(hurt && feedback_damage_count()==1,"hurt-from-left-origin");
    audio_trace_end();audiocheck_reset();audio_trace_begin();hit.b.x=10;
    feedback_push(hit);feedback_drain();audio_mix(pcm,400,2);hurt=0;
    for(int v=0;v<MAX_VOICES;v++)if(g_voices[v].active&&g_voices[v].id==SFX_HURT)hurt|=g_voices[v].gr>g_voices[v].gl;
    feedback_expect(hurt,"hurt-from-right-origin");
    feedback_push((event_t){.type=EV_DRYFIRE,.src=me,.wpn=WPN_AR});
    feedback_expect(feedback_damage_count()==1,"dryfire-is-not-life-reset");
    g_hitmark_arm=20;g_hitmark_pend=HM_KILL;g_hitmark_t=10;g_ch_kick=3;
    audio_play(SFX_HIT,.5f,1,0,.1f);feedback_push((event_t){.type=EV_RESPAWN,.dst=me});feedback_drain();audio_mix(pcm,400,2);
    feedback_expect(!g_hitmark_arm&&!g_hitmark_t&&!feedback_damage_count()&&!g_dmg_t&&!g_ch_kick,
      "respawn-clears-personal-feedback");
    feedback_expect(!audiocheck_live(SFX_HIT,0,0),"respawn-cancels-old-confirmation");
    hit=feedback_hit(me,1,1,50,g_listen);feedback_push(hit);feedback_drain();audio_mix(pcm,400,2);
    feedback_expect(g_hitmark_pend==HM_HEAD&&audiocheck_live(SFX_HIT_HEAD,0,0)==1,"new-life-cue-survives");
    g_hitmark_arm=20;g_dmg[0].life=DMG_TICKS;net_cl_reset_life(local_player(),100);audio_mix(pcm,400,2);
    feedback_expect(!g_hitmark_arm&&!feedback_damage_count()&&!audiocheck_live(SFX_HIT_HEAD,0,0),
      "snapshot-respawn-clears-personal-feedback");
  }
}
static size_t feedback_snapshot(unsigned char *buf,size_t cap,uint32_t st,
                                uint8_t spawn,const event_t *events,int count,uint32_t first_id) {
  np_wr w={buf,cap,0,0};np_w16(&w,NET_PROTO);np_w8(&w,NP_SNAPSHOT);
  np_w32(&w,st);np_w32(&w,100);np_w32(&w,net_world_hash());np_w32(&w,g_map_seed);np_w8(&w,0);
  np_w8(&w,3);net_ent_t entity;
  net_ent_from_live(&entity,g_local_ent);entity.spawn_seq=spawn;net_ent_encode(&w,&entity);
  for(int i=1;i<=2;i++){net_ent_from_live(&entity,i);net_ent_encode(&w,&entity);}
  np_w8(&w,(uint8_t)count);
  for(int i=0;i<count;i++){net_ev_t event;net_ev_from_live(&event,&events[i],first_id+(uint32_t)i);net_ev_encode(&w,&event);}
  return w.bad?0:w.at;
}
static void feedback_packets(void) {
  for(int me_index=0;me_index<2;me_index++) {
    int me=me_index?ent_of_human(MAX_HUMANS-1):0;feedback_stage(me);
    g_online=1;g_cl.state=NC_PLAYING;g_cl.my_ent=me;g_cl.synced=1;
    g_cl.seq=101;g_cl.sync_seq=100;g_cl.have_spawn_seq=1;g_cl.spawn_seq=1;
    g_cl_ev_applied=g_cl_ev_dupes=0;
    feedback_expect(audio_trace_begin(),"packet-trace-begin");
    event_t events[3]={
      {.type=EV_FIRE,.src=me,.wpn=WPN_AR,.i=7,.a={{0,1.5f,-20}},.b={{0,0,-1}},.f=20},
      feedback_hit(me,1,1,0,g_listen),
      {.type=EV_KILL,.src=me,.dst=1,.k=1}};
    unsigned char packet[1024];size_t n=feedback_snapshot(packet,sizeof packet,101,1,events,3,10);
    net_cl_snapshot(packet,(int)n);feedback_drain();
    feedback_expect(n&&g_cl_ev_applied==3&&g_cl.ev_high==12&&g_hitmark_pend==HM_KILL_HEAD&&
      g_feed[0].src==me&&g_feed[0].dst==1&&g_feed[0].hs&&g_feed[0].life>0&&
      feedback_cues(SFX_KILL_HEAD)==1&&feedback_cues(SFX_HIT_HEAD)==0,"packet-lethal-outcome-once");
    int count=g_audio_trace_n,life=g_feed[0].life,arm=g_hitmark_arm;
    net_cl_snapshot(packet,(int)n);feedback_drain();
    feedback_expect(g_cl_ev_applied==3&&g_cl_ev_dupes==3&&g_audio_trace_n==count&&
      g_feed[0].life==life&&!g_feed[1].life&&g_hitmark_arm==arm,"packet-redundancy-no-reconfirmation");
    events[2]=(event_t){.type=EV_IMPACT,.src=2,.a={{20,0,20}},.b={{0,1,0}},.i=-1};
    n=feedback_snapshot(packet,sizeof packet,102,1,events,3,11);
    net_cl_snapshot(packet,(int)n);feedback_drain();
    feedback_expect(g_cl_ev_applied==4&&g_cl_ev_dupes==5&&g_cl.ev_high==13&&
      feedback_cues(SFX_KILL_HEAD)==1&&!g_feed[1].life&&g_hitmark_arm==arm,
      "later-event-with-old-redundant-outcomes");
    n=feedback_snapshot(packet,sizeof packet,103,2,NULL,0,0);
    net_cl_snapshot(packet,(int)n);float pcm[800];audio_mix(pcm,400,2);
    feedback_expect(!g_hitmark_arm&&!g_hitmark_t&&!feedback_damage_count()&&g_feed[0].life>0&&
      !audiocheck_live(SFX_KILL_HEAD,0,0),"real-snapshot-spawn-edge-no-event");
    g_cl.ev_high=UINT32_MAX-1;events[0]=feedback_hit(me,2,0,50,g_listen);
    n=feedback_snapshot(packet,sizeof packet,104,2,events,1,1);net_cl_snapshot(packet,(int)n);feedback_drain();
    feedback_expect(g_cl.ev_high==1&&g_hitmark_pend==HM_BODY&&feedback_cues(SFX_HIT)==1,"event-id-wrap");
  }
}
static void feedback_queued_spawn(void) {
  float pcm[800];
  for (int me_index=0;me_index<2;me_index++) {
    int me=me_index?ent_of_human(MAX_HUMANS-1):0;feedback_stage(me);audio_trace_begin();
    g_online=1;g_cl.state=NC_PLAYING;g_cl.my_ent=me;g_cl.synced=1;
    g_cl.seq=101;g_cl.sync_seq=100;g_cl.have_spawn_seq=1;g_cl.spawn_seq=1;
    event_t old=feedback_hit(me,1,0,50,g_listen);feedback_push(old);
    unsigned char packet[1024];size_t n=feedback_snapshot(packet,sizeof packet,101,2,NULL,0,0);
    net_cl_snapshot(packet,(int)n);
    event_t fresh=feedback_hit(me,2,1,50,g_listen);feedback_push(fresh);
    feedback_drain();audio_mix(pcm,400,2);
    feedback_expect(n&&feedback_cues(SFX_HIT)==0&&feedback_cues(SFX_HIT_HEAD)==1&&
      !audiocheck_live(SFX_HIT,0,0)&&audiocheck_live(SFX_HIT_HEAD,0,0)==1,
      "queued-old-hit-spawn-snapshot-new-hit-survives");
  }
}

static void feedback_timing(void) {
  static const int age[]={0,1,4,8};float pcm[800];
  for(int later_local=0;later_local<2;later_local++)
  for(int weapon=0;weapon<WPN_COUNT;weapon++)for(int kind=0;kind<4;kind++)for(int ai=0;ai<4;ai++) {
    feedback_stage(0);audio_trace_begin();
    event_t fire={.type=EV_FIRE,.src=0,.wpn=weapon,.i=11,.a={{0,1.5f,-20}},.b={{0,0,-1}},.f=20};
    feedback_push(fire);event_t hit=feedback_hit(0,1,kind&1,kind>=2?0:50,g_listen);feedback_push(hit);
    if(kind>=2)feedback_push((event_t){.type=EV_KILL,.src=0,.dst=1,.k=kind&1});
    // A later shot cannot alter the confirmation already attached to its earlier event.
    fire.src=later_local?0:2;fire.wpn=1-weapon;fire.i=12;feedback_push(fire);
    for(int t=0;t<age[ai];t++){g_tick++;vfx_tick();}
    int marker_ticks=g_hitmark_arm;
    feedback_drain();
    int cue=kind==0?SFX_HIT:kind==1?SFX_HIT_HEAD:kind==2?SFX_KILL:SFX_KILL_HEAD;
    for(int t=0;t<24;t++)audio_mix(pcm,400,2);
    const audio_trace_t *trace=NULL;
    for(int i=0;i<g_audio_trace_n;i++)if(g_audio_trace[i].id==cue)trace=&g_audio_trace[i];
    float visual=(float)marker_ticks/TICK_HZ;
    float sound=trace&&trace->first_source!=AUDIO_TRACE_UNKNOWN?(float)trace->first_source/SFX_HZ:-1;
    int okay=trace&&trace->source_tick==100&&feedback_cues(cue)==1&&trace->event.src==0&&trace->event.dst==1&&
      trace->event.type==(kind>=2?EV_KILL:(kind&1)?EV_HIT_HEAD:EV_HIT)&&
      fabsf(sound-visual)<=TICK_DT+1.0f/SFX_HZ;
    if(!okay)printf("feedbackproof timing weapon=%d kind=%d age=%d marker=%.6f audio=%.6f\n",weapon,kind,age[ai],(double)visual,(double)sound);
    feedback_expect(okay,"confirmation-source-marker-frame-age");
  }
}

static void feedback_queue_lifecycle(void) {
  feedback_stage(0);audio_trace_begin();
  long drops=g_ev_drops;
  for(int i=0;i<MAX_EVENTS;i++) {
    g_tick=100+i;feedback_push((event_t){.type=EV_RESPAWN,.dst=1});
  }
  long first=g_event_tick[0],last=g_event_tick[MAX_EVENTS-1];
  feedback_push((event_t){.type=EV_RESPAWN,.dst=2});
  feedback_expect(g_num_events==MAX_EVENTS&&g_ev_drops==drops+1&&g_event_tick[0]==first&&
    g_event_tick[MAX_EVENTS-1]==last&&g_event_feedback[0]&&g_event_feedback[MAX_EVENTS-1],"full-event-sidecar-no-overwrite");
  feedback_drain();feedback_expect(!g_num_events&&!g_audio_event_age,"drain-context-cleared");
  g_tick=900;event_t hit=feedback_hit(0,1,0,50,g_listen);feedback_push(hit);
  feedback_expect(g_num_events==1&&g_event_tick[0]==900&&g_event_feedback[0],"event-slot-reuse-fresh-time");
  feedback_drain(); audio_mix_admit();
  feedback_expect(g_audio_trace_n==1&&g_audio_trace[0].device_delay==2160&&!g_audio_event_age,
    "new-slot-no-inherited-age");
  int life=g_hitmark_arm;long tick=g_tick;
  g_menu_open=1;app_t app={0};frame_input_t input={0};
  app_frame(&app,local_player(),1.0/30.0,&input);
  feedback_expect(g_tick==tick&&g_hitmark_arm==life&&!g_audio_event_age,"pause-keeps-sim-age");
  g_menu_open=0;
  vfx_local_reset();audio_feedback_reset();
  feedback_push(hit);feedback_drain();audio_mix_admit();
  feedback_expect(audiocheck_live(SFX_HIT,0,0)==1,"new-after-feedback-reset-survives");
  leave_match();
  int fresh = g_home && !g_hitmark_arm && !feedback_damage_count() && !g_audio_event_age;
  for (int i=0;i<g_num_events;i++) fresh &= g_events[i].type == EV_RESPAWN;
  feedback_expect(fresh,"leave-cleanup-clears-old-event-context");
}
static void feedback_actual_frame_ticks(void) {
  float pcm[800];int frozen=g_bots_frozen;g_bots_frozen=1;
  for(int weapon=0;weapon<WPN_COUNT;weapon++)for(int n=1;n<=8;n*=2) {
    feedback_stage(0);audio_trace_begin();player_t*p=local_player();
    g_bots[0].pos = g_bots[0].prev_pos = (v3){{0,0,-3}};
    g_bots[0].hp = 500; p->pitch = atan2f(1.1f-player_eye_pos(p).y,3.0f);
    p->wp.cur=weapon;p->wp.ads_s=p->wp.prev_ads_s=1;p->wp.ads_want=1;
    int held[IN_COUNT]={0};held[IN_ADS]=1;
    for(int t=0;t<n;t++){held[IN_FIRE]=t==0;player_tick(p,held);}
    int marker_ticks=g_hitmark_arm;feedback_drain();
    for(int t=0;t<24;t++)audio_mix(pcm,400,2);
    const audio_trace_t *confirm=NULL;int cues=0;
    for(int i=0;i<g_audio_trace_n;i++)if(sfx_prio(g_audio_trace[i].id)==SFX_P_LEAD){confirm=&g_audio_trace[i];cues++;}
    float sound=confirm&&confirm->first_source!=AUDIO_TRACE_UNKNOWN?(float)confirm->first_source/SFX_HZ:-1;
    float visual=(float)marker_ticks/TICK_HZ;
    int okay=p->shots==1&&cues==1&&confirm->event.src==0&&fabsf(sound-visual)<=TICK_DT+1.0f/SFX_HZ;
    if(!okay)printf("feedbackproof realticks weapon=%d ticks=%d shots=%d cues=%d visual=%.6f audio=%.6f\n",weapon,n,p->shots,cues,(double)visual,(double)sound);
    feedback_expect(okay,"actual-player-ticks-confirmation-age");
  }
  g_bots_frozen=frozen;
}

static void feedback_late_packet(void) {
  float pcm[800];
  for (int remote=0;remote<2;remote++) {
    int me=remote?ent_of_human(MAX_HUMANS-1):0;feedback_stage(me);g_tick=9000;
    g_online=1;g_cl.state=NC_PLAYING;g_cl.my_ent=me;g_cl.synced=1;
    g_cl.seq=101;g_cl.sync_seq=100;g_cl.have_spawn_seq=1;g_cl.spawn_seq=1;
    audio_trace_begin();event_t hit=feedback_hit(me,1,0,50,g_listen);
    unsigned char packet[1024];size_t n=feedback_snapshot(packet,sizeof packet,101,1,&hit,1,10);
    net_cl_snapshot(packet,(int)n);feedback_drain();
    for(int i=0;i<12;i++)audio_mix(pcm,400,2);
    const audio_trace_t *cue=NULL;
    for(int i=0;i<g_audio_trace_n;i++)if(g_audio_trace[i].id==SFX_HIT)cue=&g_audio_trace[i];
    feedback_expect(n&&cue&&cue->source_tick==9000&&cue->device_delay==2160&&
      cue->first_source==2160&&g_hitmark_arm>0,"late-packet-uses-local-admission-time");
  }
}

static void feedback_boundaries(void) {
  float pcm[800];
  feedback_stage(0);audio_trace_begin();
  event_t hit=feedback_hit(1,0,0,100,(v3){{8,1,0}});hit.i=0;
  feedback_push(hit);feedback_drain();audio_mix(pcm,400,2);
  feedback_expect(!feedback_cues(SFX_HURT)&&!feedback_damage_count()&&!g_dmg_t,
    "zero-damage-not-hurt-confirmed");
  feedback_stage(0);audio_trace_begin();
  hit=feedback_hit(1,0,0,80,(v3){{0,0,0}});hit.a=hit.b;
  feedback_push(hit);feedback_expect(g_dmg_t>0&&!feedback_damage_count(),"unknown-segment-wash-no-bearing");
  feedback_stage(0);audio_trace_begin();
  feedback_push(feedback_hit(0,1,0,50,g_listen));
  feedback_push(feedback_hit(1,0,0,50,(v3){{8,1,0}}));
  ent_damage(0,1000,2,1,(v3){{-8,1,0}},g_listen);feedback_drain();audio_mix(pcm,400,2);
  feedback_expect(!local_player()->alive&&!g_hitmark_arm&&!g_hitmark_t&&!feedback_damage_count()&&!g_dmg_t&&
    !audiocheck_live(SFX_HIT,0,0)&&audiocheck_live(SFX_HURT,0,0)==1&&g_feed[0].dst==0,
    "actual-death-clears-old-feedback-preserves-death-cue-feed");
  player_respawn(local_player(),0);
  feedback_push(feedback_hit(0,2,1,50,player_eye_pos(local_player())));
  feedback_drain();audio_mix(pcm,400,2);
  feedback_expect(local_player()->alive&&!audiocheck_live(SFX_HURT,0,0)&&audiocheck_live(SFX_HIT_HEAD,0,0)==1,
    "actual-respawn-following-hit-same-frame");

  feedback_stage(0);audio_trace_begin();
  feedback_push((event_t){.type=EV_FIRE,.src=1,.wpn=WPN_AR,.i=77,.a={{0,1,-12}},.b={{0,0,-1}},.f=20});
  feedback_push((event_t){.type=EV_STEP,.src=2,.a={{2,0,-2}},.f=4});
  feedback_push(feedback_hit(0,1,0,50,g_listen));
  audio_feedback_reset();feedback_push(feedback_hit(0,2,1,50,g_listen));
  feedback_drain();audio_mix(pcm,400,2);
  int foreign_fire=0,foreign_step=0;
  for(int i=0;i<g_audio_trace_n;i++) {
    foreign_fire+=g_audio_trace[i].event.type==EV_FIRE&&g_audio_trace[i].event.src==1;
    foreign_step+=g_audio_trace[i].event.type==EV_STEP&&g_audio_trace[i].event.src==2;
  }
  feedback_expect(foreign_fire>0&&foreign_step>0&&feedback_cues(SFX_HIT)==0&&feedback_cues(SFX_HIT_HEAD)==1,
    "life-filter-retains-foreign-fire-step-and-new-hit");
  feedback_stage(0);audio_trace_begin();
  feedback_push(feedback_hit(0,1,0,50,g_listen));
  proof_world_t *world=malloc(sizeof *world);if(!world)fatal("feedback proof save");
  proof_world_save(world);g_num_events=0;g_tick=999;
  feedback_push((event_t){.type=EV_RESPAWN,.dst=1});audio_feedback_reset();
  proof_world_restore(world);free(world);
  feedback_expect(g_num_events==1&&g_event_tick[0]==100&&g_event_feedback[0]==1,
    "pending-event-world-snapshot-restores-sidecars");
  feedback_drain();audio_mix_admit();
  feedback_expect(feedback_cues(SFX_HIT)==1&&g_audio_trace[g_audio_trace_n-1].device_delay==2160,
    "restored-pending-event-actual-delay");
}

static void feedbackproof(void) {
  feedback_cases=feedback_failed=0;recoil_save_globals();
  int home=g_home,menu=g_menu_open,vmorb=g_vmorb.on,num_solids=g_num_solids;
  int hud=g_hud_visible,fps=g_cfg.show_fps,third=g_cfg.third_person,bots=g_cfg.sp_bots;
  float volume=g_cfg.vol;uint64_t rng_audio=g_rng_audio;
  uint32_t seed=g_map_seed; uint64_t rng_map=g_rng, rng_bot=g_rng_bot;
  g_num_solids=0;
  feedback_geometry();feedback_events();feedback_packets();feedback_queued_spawn();feedback_timing();feedback_queue_lifecycle();feedback_actual_frame_ticks();feedback_late_packet();feedback_boundaries();
  audio_trace_end();audiocheck_reset();vfx_local_reset();
  g_home=home;g_menu_open=menu;g_vmorb.on=vmorb;g_num_solids=num_solids;
  g_cfg.sp_bots=bots;g_hud_visible=hud;g_cfg.show_fps=fps;g_cfg.third_person=third;g_cfg.vol=volume;g_rng_audio=rng_audio;
  g_map_seed=seed; map_generate(seed); g_rng=rng_map; g_rng_bot=rng_bot;
  recoil_restore_globals();
  printf("feedbackproof cases=%d failed=%d %s\n",feedback_cases,feedback_failed,feedback_failed?"FAIL":"ok");
  if(feedback_failed)exit(1);
}

// Real queue regression: an irrecoverable gap below the backlog threshold must
// resume within the reordering grace, without extra moves or invented buttons.
static void netloss_proof(void) {
  net_conn_t saved[MAX_HUMANS]; memcpy(saved, g_conns, sizeof saved);
  int budgets[MAX_HUMANS]; memcpy(budgets, g_srv_in_budget, sizeof budgets);
  int held[MAX_HUMANS][IN_COUNT]; memcpy(held, g_human_held, sizeof held);
  int move[MAX_HUMANS]; memcpy(move, g_human_move, sizeof move);
  float wish[MAX_HUMANS][2]; memcpy(wish, g_human_wish, sizeof wish);
  long view[MAX_HUMANS]; memcpy(view, g_human_view, sizeof view);
  player_t player = g_players[1];
  unsigned drops = g_srv_dropped_inputs, trims = g_srv_trim_drops;
  int catchup = g_srv_catchup, failed = 0, cases = 0;
  for (int mode = 0; mode < 5; mode++) {
    memset(g_conns, 0, sizeof saved); memset(g_srv_in_budget, 0, sizeof budgets);
    net_conn_t *c = &g_conns[0]; c->used = 1; c->human = 1;
    g_srv_catchup = mode == 3;
    uint32_t first = mode == 4 ? UINT32_MAX - 1 : 100;
    net_input_t input = {.tick = first};
    net_srv_queue_input(c, 1, &input); net_srv_consume_inputs();
    input.tick = first + (mode == 2 ? 5u : 2u);
    input.buttons = (uint16_t)(1u << IN_JUMP);
    net_srv_queue_input(c, 1, &input);
    int waits = 0;
    do { net_srv_consume_inputs(); if (!g_human_move[1]) waits++; }
    while (!g_human_move[1] && waits <= NET_IN_GAP_WAIT + 1);
    int okay = waits == NET_IN_GAP_WAIT && c->last_input_tick == input.tick &&
               g_human_held[1][IN_JUMP];
    cases++; failed += !okay;
    printf("netloss gap mode=%d waits=%d recovered=%d\n", mode, waits, okay);
    uint32_t next = c->in_next;
    for (int i = 0; i < 20; i++) net_srv_consume_inputs();
    cases++; failed += c->in_next != next || g_human_move[1];
  }
  memset(g_conns, 0, sizeof saved); memset(g_srv_in_budget, 0, sizeof budgets);
  net_conn_t *c = &g_conns[0]; c->used = 1; c->human = 1; g_srv_catchup = 0;
  net_input_t input = {.tick = 0};
  net_srv_queue_input(c, 1, &input); net_srv_consume_inputs();
  input.tick = 2; net_srv_queue_input(c, 1, &input); net_srv_consume_inputs();
  input.tick = 1; input.buttons = (uint16_t)(1u << IN_FIRE);
  net_srv_queue_input(c, 1, &input); net_srv_consume_inputs();
  cases++; failed += c->last_input_tick != 1 || !g_human_held[1][IN_FIRE];
  net_srv_consume_inputs();
  cases++; failed += c->last_input_tick != 2 || g_human_held[1][IN_FIRE];
  memcpy(g_conns, saved, sizeof saved); memcpy(g_srv_in_budget, budgets, sizeof budgets);
  memcpy(g_human_held, held, sizeof held); memcpy(g_human_move, move, sizeof move);
  memcpy(g_human_wish, wish, sizeof wish); memcpy(g_human_view, view, sizeof view);
  g_players[1] = player; g_srv_dropped_inputs = drops; g_srv_trim_drops = trims;
  g_srv_catchup = catchup;
  printf("netloss cases=%d failed=%d %s\n", cases, failed, failed ? "FAIL" : "ok");
  if (failed) exit(1);
}
