// =============================================================================
// harness/proofs_net.inc  —  netclient/netcarry/netdeath/netfuzz_proof.
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
  anim_reset(&g_player_anim[h], p->pos, p->yaw);
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
  anim_reset(&b->anim, b->pos, b->yaw);
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
static float netfigure_bore_off_aim(int ent) {
  const anim_t *a = ent_anim(ent);
  int h = human_of_ent(ent);
  float yaw   = h >= 0 ? g_players[h].yaw   : g_bots[ent - 1].yaw;
  float pitch = h >= 0 ? g_players[h].pitch : g_bots[ent - 1].pitch;
  v3 aim = {{sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch)}};
  v3 gf = v3_norm(a->pose.gun_f);
  return acosf(f_clamp(v3_dot(gf, aim), -1.0f, 1.0f)) * (180.0f / F_PI);
}

static void netfigure_proof(void) {
  g_online = 1;
  g_local_ent = 0;
  g_cl.my_ent = 0;
  g_cl.synced = 1;
  g_humans_on[0] = 1;
  player_spawn(&g_players[0]);
  int h = 1, ent = ent_of_human(h);
  g_humans_on[h] = 1;
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = p->prev_pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->alive = 1; p->hp = 100; p->grounded = 1;
  p->yaw = 0.7f; p->pitch = -0.05f;
  p->wp.cur = WPN_AR;
  p->wp.ads_want = p->wp.ready_want = 0;   // PATROL: he is holding nothing down
  anim_reset(&g_player_anim[h], p->yaw ? p->pos : p->pos, p->yaw);
  anim_t *A = ent_anim(ent);
  weapon_t *W = ent_weapon(ent);
  for (int i = 0; i < 40; i++) { g_tick++; net_cl_present_advance(); client_anim_all(); }
  float slung = netfigure_bore_off_aim(ent);

  // 1. THE SHOT. The round leaves along the aim axis on the tick the trigger breaks, so
  // the rifle has to be ON that axis on that same tick — not 20 ticks later, which is
  // what the carry ramp alone delivered (measured: 36.96 deg still off at the shot).
  v3 aim = {{sinf(p->yaw) * cosf(p->pitch), sinf(p->pitch), -cosf(p->yaw) * cosf(p->pitch)}};
  v3 muz_slung = pose_muzzle(&A->pose, p->wp.cur);
  ev_push((event_t){.type = EV_FIRE, .src = ent, .wpn = p->wp.cur,
                    .a = v3_add(p->pos, v3_scale(aim, 20.0f)), .b = aim});
  float kick = A->fire_k;
  // WHERE THE TRACER AND THE FLASH JUST ANCHORED. vfx_ev_fire reads the pose from
  // inside that push, so this is the muzzle the shot's VFX were pinned to — and it has
  // to be the RAISED rifle's, not the pose the previous tick left behind. Raising the
  // carriage without re-solving moved the rifle up and left the flash at the hip,
  // 0.60 m away: a visibly detached star, which is why the shot re-solves its shooter.
  v3 muz_shot = pose_muzzle(&A->pose, p->wp.cur);
  g_tick++; net_cl_present_advance(); client_anim_all();
  float shot_off = netfigure_bore_off_aim(ent);
  v3 muz_drawn = pose_muzzle(&A->pose, p->wp.cur);
  float anchor = v3_len(v3_sub(muz_shot, muz_drawn));   // spawn vs what is drawn
  float raised = v3_len(v3_sub(muz_slung, muz_drawn));  // how far the rifle travelled
  int ok_shot = slung > 20.0f && shot_off < 0.5f && kick > 0.9f &&
                raised > 0.20f && anchor < 0.05f;

  // 2. THE RELOAD, and it has to END. A clock stamped by the event and never counted
  // down is a magazine change that begins and hangs — which is what a client did with
  // reload_t before net_cl_present_advance ran gun_reload_tick on foreign weapons.
  int dur = wpn_reload_ticks(W->cur, 0);
  ev_push((event_t){.type = EV_RELOAD, .src = ent, .a = p->pos, .wpn = W->cur, .k = 0});
  int started = W->reload_t;
  // ...and the POSE has to move with it: gun_anim is what the figure's hands read.
  gunanim_t mid = {0};
  for (int i = 0; i < dur; i++) {
    if (i == dur / 3) mid = gun_anim(W, 1.0f);
    g_tick++; net_cl_present_advance(); client_anim_all();
  }
  int ok_reload = started == dur && W->reload_t == 0 &&
                  (fabsf(mid.arc) > 0.01f || fabsf(mid.mag) > 0.01f);

  // 3. THE ROUND LANDING: the hit flash and the flinch, both off the same EV_HIT, and
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

  // 4. OUR OWN BODY IS NOT A FOREIGN ONE. The local player's reload is PREDICTED ticks
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

  // 5. THE CARRY PREDICATE ITSELF — the server-side half, and the one thing on this
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

  int ok = ok_shot && ok_reload && ok_hit && ok_mine && ok_carry;
  printf("netfigure carry patrol=%d reload=%d swap=%d hit=%d fire=%d | ",
         cr_patrol, cr_reload, cr_swap, cr_hit, cr_fire);
  printf("slung=%.2fdeg shot_off=%.2fdeg kick=%.2f raise=%.2fm anchor=%.3fm | "
         "reload %d/%d end=%d "
         "arc=%.2f | flash=%d/%d flinch=%d expired=%d | own_reload=%d %s\n",
         (double)slung, (double)shot_off, (double)kick, (double)raised, (double)anchor,
         started, dur, W->reload_t,
         (double)mid.arc, lit, BOT_FLASH_TICKS, flinched, p->flash == 0,
         me->wp.reload_t, ok ? "ok" : "FAIL");
  if (!ok) exit(1);
}

// An online death all the way through to a corpse, client half.
static void netdeath_proof(void) {
  // THE ONLINE DEATH PATH, as a gate. Offline a kill runs ent_damage (ragdoll launch)
  // and player_present_tick (corpse handover) on one tick.
  int h = 1, ent = ent_of_human(h);
  g_online = 1;
  g_humans_on[h] = 1;
  player_t *p = &g_players[h];
  player_spawn(p);
  p->pos = (v3){{0.0f, 0.0f, 6.0f}};
  p->prev_pos = p->pos;
  p->vel = (v3){{0.0f, 0.0f, 0.0f}};
  p->alive = 1; p->grounded = 1; p->hp = 100;
  anim_reset(&g_player_anim[h], p->pos, p->yaw);
  g_cl_alive_prev[ent] = 1;
  // The wound the server would have sent: a round from the front, chest
  // high — the EV_HIT* the client latches in g_wound.
  g_wound[ent] = (typeof(g_wound[0])){
      v3_add(p->pos, (v3){{0.0f, 1.10f, 0.0f}}),
      v3_add(p->pos, (v3){{0.0f, 1.50f, 4.0f}}), g_tick, 46, 0};
  p->alive = 0;              // <- the snapshot flag going 0, nothing else
  client_anim_all();         // the tick that has to notice it
  int born = 0; v3 p0 = {{0, 0, 0}};
  for (int c = 0; c < MAX_CORPSES; c++)
    if (g_corpses[c].used) { born++; p0 = g_corpses[c].pos; }
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
  anim_reset(&bt->anim, bt->pos, bt->yaw);
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
  printf("netdeath bot dead_tilt=%.2f dead_topple=%.2f "
         "up_tilt=%.2f up_topple=%.2f\n",
         (double)dead_tilt, (double)dead_top,
         (double)bt->rag.tilt, (double)bt->topple);
  printf("netdeath corpses=%d live=%d moved=%.3f tilt=%.2f grounded=%d queue=%d\n",
         born, live, (double)moved, (double)tilt, gnd, g_player_corpse_q[h]);
  // THE OTHER HALF OF "IT LOOKS BROKEN" IS THE RENDER LERP, and no state dump can see
  // it. scene_draw_bot lerps prev_topple->topple and prev_yaw->yaw by the SUB-TICK
  // alpha, and alpha is re-drawn every rendered frame.
  memset(bt, 0, sizeof *bt);
  bt->active = 1; bt->alive = 1; bt->hp = 100;
  bt->pos = bt->prev_pos = (v3){{2.0f, 0.0f, 6.0f}};
  anim_reset(&bt->anim, bt->pos, bt->yaw);
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
  printf("netdeath join dist=%.3f owned=%d\n",
         (double)sqrtf(v3_dot(jd, jd)), g_cl_body[1]);
  printf("netdeath lerp slackgap=%.3f yawgap=%.3f posgap=%.3f drift=%.3f\n",
         (double)fabsf(sl1 - sl0), (double)yawgap,
         (double)sqrtf(v3_dot(d, d)), (double)sqrtf(v3_dot(dw, dw)));
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

