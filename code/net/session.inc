// =============================================================================
// net/session.inc  —  server connections, the client state machine,
// prediction/rollback, net_cl_snapshot.
// Proof: net*-proofs. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: server-authoritative with client prediction and rollback
// reconciliation. Predicted local fire applies its VM impulse and immediate carry
// latch in gun_shoot while ev_push is suppressed; the authoritative event echo owns
// VFX and the 3P body impulse and never adds a second VM impulse. Foreign presentation
// is RE-SOLVED here, never received: net_cl_extrapolate projects the body,
// net_cl_present_advance re-runs the weapon's own clocks from snapshot intents, and
// ent_on_event (sim/combat.inc) applies delivered recoil/reload/flinch impulses
// (proof: netcarry, netfigure, netrecoil).
// =============================================================================
// ---------------------------------------------------------------------------
// Live connection layer — SHARED. The server accepts clients,
// applies their inputs and streams snapshots; the client runs the connection automaton,
// predicts its own motion and interpolates the others.
// ---------------------------------------------------------------------------

// --- server side ---
typedef struct {
  int      used;
  uint32_t ip; uint16_t port;
  int      human;                 // human slot, 1..MAX_HUMANS-1 (a conn only
                                  // exists fully joined — net_srv_admit
                                  // assigns the slot before the conn)
  uint32_t token;                 // session token, issued in ACCEPT and echoed
                                  // on every INPUT/LEAVE: an unauthenticated
                                  // LEAVE was a 3-byte spoofable kick
  uint32_t last_recv_ms;          // idle-timeout clock
  uint32_t last_join_ms;          // re-JOIN cooldown; see net_srv_packet's JOIN arm
  uint32_t last_input_tick;       // tag the snapshot with the last input applied
  uint32_t ev_high;               // highest event id this client has acked
  // De-jitter input queue.
  net_input_t inq[NET_IN_MAX];
  uint8_t     inq_set[NET_IN_MAX];
  uint32_t    in_next;            // next input tick to consume
  uint32_t    in_high;            // highest input tick buffered (backlog gauge)
  int         in_started;         // first input adopted the client's tick clock
  uint16_t    btn_carry;          // buttons of backlog-dropped ticks, OR-ed
                                  // into the next played one so a press EDGE
                                  // inside a dropped tick still fires
  int         rtt_ms;             // smoothed g_tick - input.view at consume
                                  // time: the whole loop the player feels, and the ping
                                  // the scoreboard shows. 0 = not measured yet
} net_conn_t;
static net_conn_t g_conns_all[SRV_LOBBIES][MAX_HUMANS];
#define g_conns (g_conns_all[g_lobby])
static int g_srv_fd = -1;
static int g_srv_in_budget_all[SRV_LOBBIES][MAX_HUMANS];  // input ticks/s per slot
#define g_srv_in_budget (g_srv_in_budget_all[g_lobby])
static int g_srv_catchup;                // the pacing loop is BEHIND its clock:
                                         // the queued inputs are about to be
                                         // replayed one per catch-up tick, so
                                         // the backlog drop must not eat them

// Insert one decoded input record into a connection's queue.
static int net_srv_queue_input(net_conn_t *c, int h, const net_input_t *in) {
  if (!c->in_started) {                   // adopt the client's tick clock
    c->in_started = 1;
    c->in_next = in->tick;
    c->in_high = in->tick - 1;
  }
  int32_t d = (int32_t)(in->tick - c->in_next);
  if (d < 0) return 0;                    // already consumed: redundancy dup
  if (d >= NET_IN_MAX) {                  // far ahead (stall/loss burst):
    uint32_t skip = in->tick - (NET_IN_MAX - 1) - c->in_next;
    // The COUNTED skip is clamped: `skip` is attacker-controlled through the record's
    // tick field, and one crafted jump made the digest's dropped_in garbage (with a
    // second jump overflowing the int — UB).
    g_srv_dropped_inputs += skip > NET_IN_MAX ? NET_IN_MAX : (unsigned)skip;
    c->in_next += skip;                   // stale slots fail the tick check
    if (g_net_debug)
      printf("netdbg skip human=%d ticks=%u to=%u\n", h, skip, c->in_next);
  }
  int slot = (int)(in->tick % NET_IN_MAX);
  if (c->inq_set[slot] && c->inq[slot].tick == in->tick) return 0;      // dup
  if (g_srv_in_budget[h] >= TICK_HZ * NET_IN_RATE_NUM / NET_IN_RATE_DEN) {
    g_srv_dropped_inputs++;               // rate budget spent this second
    return 0;
  }
  c->inq[slot] = *in;
  c->inq_set[slot] = 1;
  if ((int32_t)(in->tick - c->in_high) > 0) c->in_high = in->tick;
  g_srv_in_budget[h]++;
  return 1;
}

static int net_conn_find(uint32_t ip, uint16_t port) {
  for (int i = 0; i < MAX_HUMANS; i++)
    if (g_conns[i].used && g_conns[i].ip == ip && g_conns[i].port == port) return i;
  return -1;
}

// Buffered-not-yet-played input ticks for one conn — the snapshot's
// qdepth byte and the digest's qd column are the SAME number through this one
// helper. 0 until the client's clock is adopted: before in_started both
// counters are zero and the raw expression read a phantom depth of 1.
static int32_t net_conn_qdepth(const net_conn_t *c) {
  if (!c->in_started) return 0;
  int32_t qd = (int32_t)(c->in_high - c->in_next) + 1;
  return qd < 0 ? 0 : qd;
}

// Not crypto — off-path spoof protection. An on-path attacker can already do
// worse than kick a player. KEYED WITH THE COOKIE SECRET all the same, and through the
// same FNV construction: without it every input is either public (the peer's own
// address) or knowable (a monotonic clock, a counter), so a client can invert its own
// ACCEPT and then predict the tokens issued around it. The secret enters BEFORE a
// multiply, so differencing two observed tokens cannot cancel it.
static uint32_t net_srv_new_token(uint32_t ip, uint16_t port) {
  static uint32_t ctr;
  uint64_t h = g_net_secret ^ 0xcbf29ce484222325ull;
  h = (h ^ ((uint64_t)ip << 16 | port)) * 0x100000001b3ull;
  h = (h ^ ((uint64_t)net_now_ms() << 32 | ++ctr)) * 0x100000001b3ull;
  uint32_t t = (uint32_t)(h ^ (h >> 32));
  return t ? t : 1;
}

// The bot target is a pure function of the humans present: bots top a populated arena
// up to bot_fill, at most cap_bots of them, none when it is empty.
static void net_srv_rebalance(void) {
  int humans = 0;
  for (int h = 0; h < MAX_HUMANS; h++) if (g_humans_on[h]) humans++;
  g_cfg.sp_bots = net_bot_target(humans, g_srv.bot_fill, g_srv.cap_bots);
}

// Everything that makes a human slot THIS player's: score row, display name, player
// struct, input residue, gun-RNG stream, spawn.
static void net_srv_slot_reset(int h, const char *name) {
  int e = ent_of_human(h);
  g_match.kills[e] = g_match.deaths[e] = g_match.shots[e] = g_match.hits[e] = 0;
  int n = 0;
  for (const char *q = name; *q && n < (int)sizeof g_human_name[h] - 1; q++) {
    char g = name_glyph(*q);
    if (g) g_human_name[h][n++] = g;
  }
  g_human_name[h][n] = '\0';
  if (!n) snprintf(g_human_name[h], sizeof g_human_name[h], "PLAYER%d", h);
  memset(&g_players[h], 0, sizeof g_players[h]);
  // CLEAR THE SLOT'S INPUT RESIDUE. g_human_held/wish/move belong to the
  // PREVIOUS occupant of this slot until the new client's first input is
  // consumed — with FIRE still latched, the fresh spawn shot one silent round
  // (its score row opened shots=1) and could walk on stale movement for the
  // first RTT. The move gate closes until the queue actually feeds a tick.
  memset(g_human_held[h], 0, sizeof g_human_held[h]);
  g_human_wish[h][0] = g_human_wish[h][1] = 0.0f;
  g_human_move[h] = 0;
  g_human_view[h] = -1;
  g_rng_gun[h] = rng_gun_seed(g_map_seed, h);   // fresh joiner, fresh stream —
                                                // mirrored client-side at sync
  player_spawn(&g_players[h]);
  player_respawn(&g_players[h], e);
}

// Assign a free human slot to a newly joined client, honouring the fill caps
//. Returns the human slot, or -1 if the instance is full.
static int net_srv_admit(uint32_t ip, uint16_t port, const char *name) {
  int humans = 0;
  for (int h = 0; h < MAX_HUMANS; h++) if (g_humans_on[h]) humans++;
  // ONE HOST MAY NOT BE THE WHOLE ARENA. Nothing above bounds joins per source address,
  // so a single machine could take every human slot with one packet each and lock a
  // public instance out — the cookie proves the address is reachable, not that it is a
  // different person. THE BOUND SCALES WITH THE ARENA: a fixed 4 against the DEPLOYED
  // cap_public 4 can never fire, i.e. the test is dead exactly where it is needed.
  //
  // cap_public - 1, not half: the harm is one address holding the WHOLE arena, and
  // leaving one slot it can never take is precisely enough to prevent that. Half would
  // also lock out the third person in a household behind one NAT — and they cannot
  // simply land in another arena, because net_srv_lobby_pick routes them to the
  // fullest one with room BEFORE admit runs. Floored at 1 (a one-slot instance IS one
  // person) and capped at NET_JOIN_PER_HOST, so raising MAX_HUMANS cannot raise this
  // by itself. At the code default cap_public 6 and above this is unchanged.
  int same = 0;
  for (int i = 0; i < MAX_HUMANS; i++)
    if (g_conns[i].used && g_conns[i].ip == ip) same++;
  // ...and ONLY on a single-arena instance, which is where the harm exists. With more
  // than one lobby a full arena is simply excluded by net_srv_lobby_pick and the next
  // stranger is routed to another one, so nobody is locked out — while the scaled bound
  // WOULD refuse the fourth member of a household behind one NAT outright, since the
  // lobby is chosen before admit runs and a refusal is global, not per-arena.
  int per_host = g_srv.lobbies > 1 ? NET_JOIN_PER_HOST : g_srv.cap_public - 1;
  if (per_host < 1) per_host = 1;
  if (per_host > NET_JOIN_PER_HOST) per_host = NET_JOIN_PER_HOST;
  if (same >= per_host) return -1;
  if (!net_join_ok(humans, g_srv.cap_public))
    return -1;
  int h = -1;
  for (int i = 1; i < MAX_HUMANS; i++) if (!g_humans_on[i]) { h = i; break; }
  if (h < 0) return -1;
  g_humans_on[h] = 1;
  net_srv_rebalance();               // fighter count stays constant
  net_srv_slot_reset(h, name);
  int conn = -1;
  for (int i = 0; i < MAX_HUMANS; i++) if (!g_conns[i].used) { conn = i; break; }
  if (conn < 0) { g_humans_on[h] = 0; return -1; }
  g_conns[conn] = (net_conn_t){.used = 1, .ip = ip, .port = port, .human = h,
                               .token = net_srv_new_token(ip, port),
                               .last_recv_ms = net_now_ms(),
                               .last_join_ms = net_now_ms(),
                               // Start the event ack AT the present: events
                               // pushed before this client existed are not its
                               // history — without this a join within the
                               // resend window replayed the last ~300 ms of
                               // someone else's gunfire into the fresh client.
                               .ev_high = g_srv_ev_next - 1};
  return conn;
}

static void net_srv_drop(int conn) {
  if (!g_conns[conn].used) return;
  int h = g_conns[conn].human;
  if (h > 0) {                         // free the slot and clear the score row
    g_humans_on[h] = 0;
    g_human_view[h] = -1;              // no conn, no lag-comp anchor
    int e = ent_of_human(h);
    g_match.kills[e] = g_match.deaths[e] = g_match.shots[e] = g_match.hits[e] = 0;
    net_srv_rebalance();               // a bot fills the freed slot
  }
  g_conns[conn] = (net_conn_t){0};
}

// Handle one received packet on the server socket. `epoch` is unix_time/30.
static void net_srv_packet(const unsigned char *b, int n, uint32_t ip, uint16_t port,
                           uint32_t epoch) {
  if (n < 3) return;
  np_rd r = {b, (size_t)n, 0, 0};
  if (np_r16(&r) != NET_PROTO) {            // wrong protocol: REJECT — but only
    // to a packet at least as big as the reply: a real old client's
    // CONNECT is NET_CONNECT_MIN-padded, while a 3-byte probe with a forged
    // source must not buy a 4-byte reflection.
    if (n < 4) return;
    unsigned char rej[4]; np_wr w = {rej, sizeof rej, 0, 0};
    np_w16(&w, NET_PROTO); np_w8(&w, NP_REJECT); np_w8(&w, NR_PROTO);
    net_udp_send(g_srv_fd, ip, port, rej, (int)w.at);
    return;
  }
  int type = np_r8(&r);
  if (type == NP_CONNECT) {
    // THE REPLY MUST BE SMALLER THAN THE REQUEST, and that only holds if the request
    // has a floor.
    if (n < NET_CONNECT_MIN) return;
    unsigned char out[8]; np_wr w = {out, sizeof out, 0, 0};
    np_w16(&w, NET_PROTO); np_w8(&w, NP_COOKIE);
    np_w32(&w, net_cookie(ip, port, epoch));
    net_udp_send(g_srv_fd, ip, port, out, (int)w.at);
  } else if (type == NP_JOIN) {
    uint32_t cookie = np_r32(&r);
    if (!net_cookie_valid(ip, port, cookie, epoch)) return;   // no valid cookie, no world
    char name[12] = {0};
    for (int i = 0; i < 11; i++) { char c = (char)np_r8(&r); if (!c) break; name[i] = c; }
    int conn = net_conn_find(ip, port);
    if (conn >= 0) {
      // A JOIN on a LIVE conn is EITHER a client announcing a fresh session from the
      // same address (process restart behind a reused NAT mapping) OR that same
      // client's own 250 ms handshake retry after a lost ACCEPT — and nothing on the
      // wire tells them apart. Only the first wants a reset, so RATE-LIMIT THE RESET
      // AND ALWAYS SEND THE ACCEPT: unbounded, one 7-byte packet re-ran spawn_spot and
      // pushed an EV_RESPAWN, 256 times a tick (net_srv_poll's drain), which evicts
      // every real bang/hit/kill from every OTHER client's 20-slot resend window.
      net_conn_t *c = &g_conns[conn];
      uint32_t now = net_now_ms();
      if (now - c->last_join_ms >= 1000) {
        c->last_join_ms = now;
        c->in_started = 0;
        memset(c->inq_set, 0, sizeof c->inq_set);
        c->btn_carry = 0;
        c->rtt_ms = 0;
        c->ev_high = g_srv_ev_next - 1;
        // A fresh session is a fresh IDENTITY too: same reset a new admit gets
        // (score row, name, spawn, gun-RNG) and a fresh token — the old
        // session's kills carried over to whoever re-joined the slot.
        c->token = net_srv_new_token(ip, port);
        net_srv_slot_reset(c->human, name);
      }
    }
    if (conn < 0) conn = net_srv_admit(ip, port, name);
    if (conn < 0) {                         // instance full
      unsigned char rej[4]; np_wr w = {rej, sizeof rej, 0, 0};
      np_w16(&w, NET_PROTO); np_w8(&w, NP_REJECT); np_w8(&w, NR_FULL);
      net_udp_send(g_srv_fd, ip, port, rej, (int)w.at);
      return;
    }
    g_conns[conn].last_recv_ms = net_now_ms();
    // ACCEPT carries the match's SIM TUNING(light): the client must predict with the
    // SERVER's movement and weapon tables, not its own preferences — a differing mv_run
    // is a permanent standing rubber-band.
    unsigned char out[168]; np_wr w = {out, sizeof out, 0, 0};
    np_w16(&w, NET_PROTO); np_w8(&w, NP_ACCEPT);
    np_w8(&w, (uint8_t)ent_of_human(g_conns[conn].human));
    np_w8(&w, (uint8_t)g_lobby);          // WHICH arena, and
    np_w8(&w, (uint8_t)g_srv.lobbies);    // how many this process runs
    np_w32(&w, g_map_seed);
    np_w32(&w, net_world_hash());
    np_w32(&w, g_conns[conn].token);
    for (int i = 0; i < MV_COUNT; i++) np_wf(&w, g_cfg.mv[i]);
    for (int wp = 0; wp < WPN_COUNT; wp++)
      for (int i = 0; i < WP_COUNT; i++) np_wf(&w, g_cfg.wp[wp][i]);
    net_udp_send(g_srv_fd, ip, port, out, (int)w.at);
  } else if (type == NP_LEAVE) {
    int conn = net_conn_find(ip, port);     // token-gated: a bare LEAVE
    if (conn >= 0 && np_r32(&r) == g_conns[conn].token)
      net_srv_drop(conn);                   // same teardown as the idle timeout
  } else if (type == NP_INPUT) {
    int conn = net_conn_find(ip, port);
    if (conn < 0) return;                   // unknown source: ignore
    net_conn_t *c = &g_conns[conn];
    if (np_r32(&r) != c->token) return;     // spoofed source: no input, no
                                            // idle-timer refresh
    c->last_recv_ms = net_now_ms();
    int h = c->human;
    // Event ack: the highest event id the client has applied.
    uint32_t ack = np_r32(&r);
    if ((int32_t)(ack - c->ev_high) > 0) c->ev_high = ack;
    int cnt = np_r8(&r);
    if (cnt > NET_IN_MAX) cnt = NET_IN_MAX;
    // BUFFER, don't apply — net_srv_consume_inputs plays one per world tick.
    for (int i = 0; i < cnt; i++) {
      net_input_t in;
      if (!net_input_decode(&r, &in)) break;
      net_srv_queue_input(c, h, &in);
    }
  } else if (type == NP_TELE) {
    // Stateless, hostile, NEVER answered (a reply = UDP amplifier). No cookie and no
    // conn: telemetry is not a session.
    net_tele_t t;
    if (g_tele_budget < 128 && g_tele_day_budget < TELE_DAY_MAX &&
        net_tele_parse(b, n, &t)) {
      g_tele_budget++;
      g_tele_day_budget++;
      static const char *M[] = {"ui", "sp", "mp"}, *O[] = {"lin", "win"};
      if (g_server) {
        if (t.kind == 0) slog("tele id=%08x ev=boot os=%s v=%s", t.id, O[t.os], t.ver);
        else             slog("tele id=%08x mode=%s dt=%d", t.id, M[t.mode], t.dt);
      }
    }
  }
}

// Consume at most ONE buffered input tick per world tick for every connection.
static void net_srv_consume_inputs(void) {
  for (int i = 0; i < MAX_HUMANS; i++) {
    net_conn_t *c = &g_conns[i];
    if (!c->used || !c->in_started) continue;
    int h = c->human;
    // Backlog deeper than NET_IN_BACKLOG is a CLIENT running away (clock skew the nudge
    // cannot hold, a runaway sender): trim oldest so input latency stays bounded — but
    // never during the server's own post-stall catch-up, where the same depth is inputs
    // waiting for the ticks about to be replayed.
    int trimmed = 0;
    while (!g_srv_catchup && (int32_t)(c->in_high - c->in_next) > NET_IN_BACKLOG) {
      int slot = (int)(c->in_next % NET_IN_MAX);
      if (c->inq_set[slot] && c->inq[slot].tick == c->in_next) {
        c->btn_carry |= c->inq[slot].buttons;
        c->inq_set[slot] = 0;
      }
      c->in_next++;
      trimmed++;
    }
    g_srv_trim_drops += trimmed;
    if (trimmed && g_net_debug)
      printf("netdbg trim conn=%d ticks=%d high=%u next=%u\n",
             i, trimmed, c->in_high, c->in_next);
    int slot = (int)(c->in_next % NET_IN_MAX);
    if (!c->inq_set[slot] || c->inq[slot].tick != c->in_next) {
      g_human_move[h] = 0;                  // gap: no input, no move
      continue;
    }
    net_input_t in = c->inq[slot];
    c->inq_set[slot] = 0;
    in.buttons |= c->btn_carry;
    c->btn_carry = 0;
    c->last_input_tick = c->in_next;
    c->in_next++;
    // The input's view tick: lag-comp anchor for this move's shots, and g_tick - view
    // IS the round trip the player feels — smoothed into the conn's ping.
    long view = (long)in.view;
    long lag = g_tick - view;
    g_human_view[h] = (in.view && lag >= 0 && lag <= NET_LAGCOMP_MAX) ? view : -1;
    if (in.view && lag >= 0 && lag < 4 * TICK_HZ) {
      int ms = (int)(lag * 1000 / TICK_HZ);
      c->rtt_ms = c->rtt_ms ? (c->rtt_ms * 7 + ms) / 8 : ms;
    }
    for (int b = 0; b < IN_COUNT; b++)
      g_human_held[h][b] = (in.buttons >> b) & 1;
    g_human_wish[h][0] = net_dq_wish(in.wish_x);
    g_human_wish[h][1] = net_dq_wish(in.wish_y);
    float yaw = net_dq_yaw(in.yaw), pitch = net_dq_pitch(in.pitch);
    net_validate_angles(&yaw, &pitch, g_players[h].yaw);   // the yaw chain
    g_players[h].yaw = yaw;
    g_players[h].pitch = pitch;
    g_human_move[h] = 1;
  }
}

// Drain the socket and service timeouts — called once per server tick.
static int net_srv_lobby_of(uint32_t ip, uint16_t port) {
  int save = g_lobby, found = -1;
  for (int l = 0; l < g_srv.lobbies && found < 0; l++) {
    g_lobby = l;
    for (int i = 0; i < MAX_HUMANS; i++)
      if (g_conns[i].used && g_conns[i].ip == ip && g_conns[i].port == port) {
        found = l; break;
      }
  }
  g_lobby = save;
  return found;
}

// THE PACKING RULE, moved server-side.
static int net_srv_lobby_pick(void) {
  int save = g_lobby, best = -1, best_h = -1;
  for (int l = 0; l < g_srv.lobbies; l++) {
    g_lobby = l;
    int humans = 0;
    for (int h = 0; h < MAX_HUMANS; h++) if (g_humans_on[h]) humans++;
    if (humans < g_srv.cap_public && humans > best_h) { best_h = humans; best = l; }
  }
  g_lobby = save;
  return best;
}

[[maybe_unused]] static void net_srv_poll(void) {
  if (g_srv_fd < 0) return;
  uint32_t epoch = (uint32_t)(time(NULL) / 30);
  unsigned char b[2048];
  uint32_t ip; uint16_t port;
  int n, guard = 0;
  while ((n = net_udp_recv(g_srv_fd, &ip, &port, b, sizeof b)) > 0 && guard++ < 256) {
    // ROUTE FIRST, DECODE SECOND. A known address goes to its own arena; a stranger
    // goes to the one the packing rule chose.
    int l = net_srv_lobby_of(ip, port);
    if (l < 0) l = net_srv_lobby_pick();
    g_lobby = l < 0 ? 0 : l;
    net_srv_packet(b, n, ip, port, epoch);
  }
  uint32_t now = net_now_ms();
  for (int l = 0; l < g_srv.lobbies; l++) {
    g_lobby = l;
    for (int i = 0; i < MAX_HUMANS; i++)       // idle timeout: --idle-timeout s
      if (g_conns[i].used &&
          now - g_conns[i].last_recv_ms > (uint32_t)(g_srv.idle_s * 1000))
        net_srv_drop(i);
  }
  g_lobby = 0;
}

// Send a snapshot to every connected client — called after the tick, rate-
// limited to ~60 Hz by the caller.
[[maybe_unused]] static void net_srv_broadcast(void) {
  if (g_srv_fd < 0) return;
  for (int i = 0; i < MAX_HUMANS; i++) {
    if (!g_conns[i].used) continue;
    static unsigned char snap[2048];
    // Queue depth AFTER this tick's consume: buffered-not-yet-played ticks.
    int32_t qd = net_conn_qdepth(&g_conns[i]);
    uint8_t q8 = qd > 255 ? 255 : (uint8_t)qd;
    size_t len = net_snapshot_build(snap, sizeof snap, g_conns[i].last_input_tick,
                                    g_conns[i].ev_high, q8);
    if (len) net_udp_send(g_srv_fd, g_conns[i].ip, g_conns[i].port, snap, (int)len);
  }
}

// Broadcast the score rows (NP_SCORES, 4 Hz from server_main). One build —
// the payload is identical per client except nothing: ping is per-ENTITY.
[[maybe_unused]] static void net_srv_scores_broadcast(void) {
  if (g_srv_fd < 0) return;
  uint8_t ping[MAX_ENTS] = {0};
  for (int i = 0; i < MAX_HUMANS; i++) {
    if (!g_conns[i].used) continue;
    int e = ent_of_human(g_conns[i].human);
    int ms = g_conns[i].rtt_ms;
    ping[e] = ms < 0 ? 0 : ms > 255 ? 255 : (uint8_t)ms;
  }
  static unsigned char buf[512];
  size_t len = net_scores_build(buf, sizeof buf, ping);
  if (!len) return;
  for (int i = 0; i < MAX_HUMANS; i++)
    if (g_conns[i].used)
      net_udp_send(g_srv_fd, g_conns[i].ip, g_conns[i].port, buf, (int)len);
}

static int net_srv_client_count(void) {
  int c = 0;
  for (int i = 0; i < MAX_HUMANS; i++) if (g_conns[i].used) c++;
  return c;
}

// Worst smoothed RTT and worst input-queue depth across the live conns — the
// digest's connection-feel line. Zeros with no clients.
static void net_srv_lat(int *rtt_max, int *qd_max) {
  *rtt_max = 0; *qd_max = 0;
  for (int i = 0; i < MAX_HUMANS; i++) {
    if (!g_conns[i].used) continue;
    if (g_conns[i].rtt_ms > *rtt_max) *rtt_max = g_conns[i].rtt_ms;
    int32_t qd = net_conn_qdepth(&g_conns[i]);
    if (qd > *qd_max) *qd_max = qd;
  }
}


// --- client side ---
typedef enum { NC_OFF, NC_CONNECTING, NC_JOINING, NC_PLAYING, NC_FAILED } nc_state_t;
typedef struct {
  int        fd;
  uint32_t   ip; uint16_t port;
  nc_state_t state;
  uint32_t   cookie;
  int        my_ent;                 // our entity id, from ACCEPT
  uint32_t   connect_start_ms, last_send_ms, last_recv_ms;
  uint32_t   server_tick;            // newest snapshot's server tick
  uint8_t    have_snap;              // server_tick is seeded (first snapshot applied)
  uint32_t   world;                  // server's world hash (arena check)
  // Event high-water mark: the highest event id applied.
  uint32_t   ev_high;
  uint32_t   token;                  // session token from ACCEPT, echoed on
                                     // every INPUT/LEAVE (the spoof gate)
  uint8_t    spawn_seq;              // last seen for our own entity (respawn edge)
  int        have_spawn_seq;
  uint8_t    ent_ss[MAX_ENTS];       // last spawn_seq per FOREIGN entity: a
  uint8_t    ent_ss_seen[MAX_ENTS];  // changed one is a respawn teleport —
                                     // reset the anim solver, don't lerp
                                     // a corpse across the map to its spawn
  char       name[12];
  int        lobby, lobbies;         // which arena of how many (ACCEPT)
  int        reject;                 // last REJECT reason, 0 = none
  int        synced;                 // first authoritative fix received?
  float      srv_mv[MV_COUNT];       // the SERVER's sim tuning, from ACCEPT —
  float      srv_wp[WPN_COUNT][WP_COUNT];  // g_sim_mv/g_sim_wp point here online
  uint32_t   seq;                    // our own input counter (prediction anchor)
  uint32_t   sync_seq;               // seq at the first authoritative fix —
                                     // predpos entries before it are the PRE-
                                     // JOIN local match, not predictions
  uint32_t   snap_seq;               // seq when the last snapshot applied: the
                                     // dead-reckoning tick count since, and
                                     // with server_tick the input's VIEW tick
  float      qdepth_ema;             // smoothed server-side input-queue depth
                                     // (snapshot header) — the clock nudge
                                     // holds it at NET_QDEPTH_TARGET
  float      prev_yaw;               // last PREDICTED (validated) yaw: the
                                     // prev the server's validation will
                                     // use for the next record — p->yaw is
                                     // already the new aim by the time the
                                     // tick runs, so it cannot be the anchor
  int        have_prev_yaw;
  v3         predpos[NET_HIST];      // predicted pos AFTER each seq's move
  net_input_t inlog[NET_HIST];       // sent inputs, the redundancy source
} net_client_t;
static net_client_t g_cl;
// Score-row furniture replicated by NP_SCORES (online only): per-entity ping
// for the scoreboard, and the SERVER's frag limit — the local g_cfg.sp_frag is
// this player's own preference and lies about the match he actually joined.
static uint8_t  g_net_ping_all[SRV_LOBBIES][MAX_ENTS];
#define g_net_ping (g_net_ping_all[g_lobby])
static uint16_t g_net_fraglimit;
static int g_cl_corrections;         // reconciliation count, for the proof
static float g_cl_worst_err_mm;
static int g_cl_ev_applied, g_cl_ev_dupes;   // event channel, for the proof
static void net_cl_send_inputs(void);        // defined below

// Derived predicted state per input tick, the predpos ring's sibling. vm is local-only
// presentation, but it is still derived by player_move_tick: restoring it is what makes
// a shot inside a rollback window produce exactly one viewmodel impulse.
typedef struct {
  weapon_t wp;
  vm_t vm;
  uint64_t rng;
  float yaw;
} cl_pred_t;
static cl_pred_t g_cl_pred[NET_HIST];

// True when input tick `t` is inside the recorded prediction window — the
// same three conditions the reconcile anchor checks.
static int net_cl_hist_ok(uint32_t t) {
  return g_cl.synced && (int32_t)(t - g_cl.sync_seq) >= 0 &&
         (int32_t)(g_cl.seq - t) >= 0 && g_cl.seq - t < NET_HIST;
}

// Input edges are derived state too. Replaying (li, seq) must begin with the buttons
// the server had just consumed at li, not with the live endpoint's later buttons.
static void net_cl_restore_buttons(player_t *p, uint32_t li) {
  const net_input_t *in = &g_cl.inlog[li % NET_HIST];
  if (in->tick != li) return;
  for (int b = 0; b < IN_COUNT; b++) p->prev_held[b] = (in->buttons >> b) & 1;
}

// Restore the recorded derived state at input `li` before a rollback replay.
static void net_cl_restore_pred(player_t *p, int h, uint32_t li) {
  if (!net_cl_hist_ok(li)) return;
  const cl_pred_t *ps = &g_cl_pred[li % NET_HIST];
  p->wp = ps->wp;
  p->vm = ps->vm;
  g_rng_gun[h] = ps->rng;
  p->yaw = ps->yaw;        // the chain value the replay validates against
  net_cl_restore_buttons(p, li);
}

// A spawn-sequence edge is the same fresh life player_respawn creates on the server.
// Preserve only lifetime/accounting state and the acknowledged input edge; every
// movement, collision and presentation follower is reset by the one player_spawn
// implementation before authoritative snapshot fields are applied below.
static void net_cl_reset_life(player_t *p, uint32_t li) {
  int shots = p->shots;
  int prev_held[IN_COUNT];
  memcpy(prev_held, p->prev_held, sizeof prev_held);
  player_spawn(p);
  p->shots = shots;
  memcpy(p->prev_held, prev_held, sizeof prev_held);
  net_cl_restore_buttons(p, li);
}

// An adopted position rides the wire's 1 cm grid, and against a wall the server slides
// along at exactly face+radius the grid lands up to 5 mm INSIDE the solid. move_axis
// resolves ANY overlap by snapping to a face of the first overlapping axis, so a
// millimetre of wire rounding became a 2-22 m sideways teleport, re-created by every
// subsequent adopt — the sustained correction sawtooth.
static void net_cl_depenetrate(player_t *p) {
  float height = player_col_height(p);
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (!(p->pos.x + PLAYER_RADIUS > s->min.x && p->pos.x - PLAYER_RADIUS < s->max.x &&
          p->pos.y + height        > s->min.y && p->pos.y                 < s->max.y &&
          p->pos.z + PLAYER_RADIUS > s->min.z && p->pos.z - PLAYER_RADIUS < s->max.z))
      continue;
    float push[5] = {
      s->max.x - (p->pos.x - PLAYER_RADIUS),     // out through +x
      (p->pos.x + PLAYER_RADIUS) - s->min.x,     // out through -x
      s->max.z - (p->pos.z - PLAYER_RADIUS),     // out through +z
      (p->pos.z + PLAYER_RADIUS) - s->min.z,     // out through -z
      s->max.y - p->pos.y,                       // feet up onto the top
    };
    int k = 0;
    for (int j = 1; j < 5; j++) if (push[j] < push[k]) k = j;
    if (push[k] > 0.011f) continue;              // > one grid step: not ours
    switch (k) {
      case 0: p->pos.x = s->max.x + PLAYER_RADIUS; break;
      case 1: p->pos.x = s->min.x - PLAYER_RADIUS; break;
      case 2: p->pos.z = s->max.z + PLAYER_RADIUS; break;
      case 3: p->pos.z = s->min.z - PLAYER_RADIUS; break;
      case 4: p->pos.y = s->max.y; break;
    }
  }
}

static void net_cl_send_connect(void) {
  unsigned char b[NET_CONNECT_MIN] = {0}; np_wr w = {b, sizeof b, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_CONNECT);
  for (int i = 0; i < 11 && g_cl.name[i]; i++) np_w8(&w, (uint8_t)g_cl.name[i]);
  // Sent at full length whatever the name weighs: the padding IS the
  // anti-amplification property, so it may not shrink with a short name.
  net_udp_send(g_cl.fd, g_cl.ip, g_cl.port, b, NET_CONNECT_MIN);
  g_cl.last_send_ms = net_now_ms();
}
static void net_cl_send_join(void) {
  unsigned char b[24]; np_wr w = {b, sizeof b, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_JOIN); np_w32(&w, g_cl.cookie);
  for (int i = 0; i < 11 && g_cl.name[i]; i++) np_w8(&w, (uint8_t)g_cl.name[i]);
  net_udp_send(g_cl.fd, g_cl.ip, g_cl.port, b, (int)w.at);
  g_cl.last_send_ms = net_now_ms();
}

// Begin a connection(the caller keeps playing locally meanwhile).
static int net_cl_connect(uint32_t ip, uint16_t port, const char *name) {
  if (g_cl.fd >= 0) net_udp_close(g_cl.fd);   // a FAILED handshake parks its
                                              // fd here; retrying must not
                                              // leak one descriptor per press
  memset(&g_cl, 0, sizeof g_cl);
  g_cl.fd = net_udp_open(0);           // ephemeral local port
  if (g_cl.fd < 0) { g_cl.state = NC_FAILED; return 0; }
  g_cl.ip = ip; g_cl.port = port;
  g_cl.my_ent = -1;
  snprintf(g_cl.name, sizeof g_cl.name, "%s", name && name[0] ? name : "PLAYER");
  g_cl.state = NC_CONNECTING;
  g_cl.connect_start_ms = net_now_ms();
  net_cl_send_connect();
  return 1;
}
static void net_cl_disconnect(void) {
  // The sim tuning goes back to the player's own config BEFORE g_cl is wiped
  // — the pointers aim into g_cl.srv_mv/srv_wp while online(light).
  g_sim_mv = g_cfg.mv;
  g_sim_wp = g_cfg.wp;
  // SAY GOODBYE. Best effort and unacknowledged — UDP, and a client that has already
  // lost the link cannot be helped — but in the common case (LEAVE MATCH, or quitting)
  // it hands the slot back at once instead of parking it for the idle timeout. Sent
  // x3 against single-datagram loss: 24 bytes total, and the server side is
  // idempotent (token-validated, `used`-guarded drop).
  if (g_cl.fd >= 0 && (g_cl.state == NC_PLAYING || g_cl.state == NC_JOINING)) {
    unsigned char b[8]; np_wr w = {b, sizeof b, 0, 0};
    np_w16(&w, NET_PROTO); np_w8(&w, NP_LEAVE);
    np_w32(&w, g_cl.token);                   // session token
    for (int i = 0; i < 3; i++)
      net_udp_send(g_cl.fd, g_cl.ip, g_cl.port, b, (int)w.at);
  }
  if (g_cl.fd >= 0) net_udp_close(g_cl.fd);
  memset(&g_cl, 0, sizeof g_cl);
  g_cl.fd = -1;
  memset(g_net_ping, 0, sizeof g_net_ping);  // scoreboard furniture goes local
  g_net_fraglimit = 0;
}

// ...and WHOSE BODY THAT IS. A dead bot's position has one owner: from the death edge
// it saw itself, this client's own ragdoll integrates it and the snapshot stops writing
// it (see net_cl_apply_ent).
static unsigned char g_cl_body[MAX_ENTS];

// Write one entity record from a snapshot into the local world mirror.
static void net_cl_apply_ent(const net_ent_t *e) {
  int h = human_of_ent(e->id);
  v3 pos = {{net_dq_pos(e->x), net_dq_pos(e->y), net_dq_pos(e->z)}};
  v3 vel = {{net_dq_vel(e->vx), net_dq_vel(e->vy), net_dq_vel(e->vz)}};
  // A spawn_seq edge is a TELEPORT: kill the render lerp (prev_pos = pos) and
  // re-solve the pose, exactly what the sim does on its own respawns.
  int tele = g_cl.ent_ss_seen[e->id] && g_cl.ent_ss[e->id] != e->spawn_seq;
  g_cl.ent_ss[e->id] = e->spawn_seq;
  g_cl.ent_ss_seen[e->id] = 1;
  if (h >= 0) {
    player_t *p = &g_players[h];
    p->prev_pos = tele ? pos : p->pos; p->pos = pos; p->vel = vel;
    p->yaw = net_dq_yaw(e->yaw); p->pitch = net_dq_pitch(e->pitch);
    p->lean = net_dq_wish(e->lean);
    p->alive = !!(e->flags & NEF_ALIVE); p->grounded = !!(e->flags & NEF_GROUNDED);
    p->crouched = !!(e->flags & NEF_CROUCH); p->sliding = !!(e->flags & NEF_SLIDE);
    p->hp = e->hp; p->wp.cur = e->weapon;
    // THE CARRIAGE ARRIVES AS INTENT. ads_want feeds the client's own gun_ads
    // (net_cl_present_advance) rather than being a blend to copy; ready_want is the
    // carry predicate the client cannot run itself; the tuck is one constant or
    // nothing, which is why one bit carries the whole of it.
    p->wp.ads_want = !!(e->flags & NEF_ADS);
    p->wp.ready_want = !!(e->flags & NEF_READY);
    p->tuck_y = (e->flags & NEF_TUCK) ? HEIGHT_STAND - HEIGHT_CROUCH : 0.0f;
    if (!g_humans_on[h]) {
      g_humans_on[h] = 1;
      anim_reset(&g_player_anim[h], pos, p->yaw, p->wp.cur);
    } else if (tele) {
      anim_reset(&g_player_anim[h], pos, p->yaw, p->wp.cur);
    }
  } else {
    bot_t *b = &g_bots[e->id - 1];
    float byaw = net_dq_yaw(e->yaw);
    // A CORPSE IS PRESENTATION AND IT HAS EXACTLY ONE OWNER. The server ragdolls its
    // dead bots too (bot_tick's dead branch) and ENT_FOREACH sends them, so once the
    // client launched its own ragdoll BOTH were writing this one position: the snapshot
    // yanked the body onto the server's solution, the next tick's rag_step pushed it
    // back onto the client's, and prev_pos was left holding whichever the other one
    // wrote.
    int keep = g_cl_body[e->id] && !b->alive && !(e->flags & NEF_ALIVE);
    if (!keep) { b->prev_pos = tele ? pos : b->pos; b->pos = pos; b->vel = vel; }
    // THE HEADING IS A PAIR TOO. scene_draw_bot lerps prev_yaw->yaw by the sub-tick
    // alpha exactly as it lerps the position pair, and offline bot_tick advances it
    // every tick.
    b->prev_yaw = tele ? byaw : b->yaw; b->yaw = byaw;
    b->pitch = net_dq_pitch(e->pitch);
    b->alive = !!(e->flags & NEF_ALIVE); b->grounded = !!(e->flags & NEF_GROUNDED);
    b->crouch = !!(e->flags & NEF_CROUCH); b->sliding = !!(e->flags & NEF_SLIDE);
    b->hp = e->hp; b->wp.cur = e->weapon; b->active = 1;
    b->wp.ads_want = !!(e->flags & NEF_ADS);      // a bot's carriage is the same
    b->wp.ready_want = !!(e->flags & NEF_READY);  // two bits (it never tucks)
    if (tele) anim_reset(&b->anim, pos, b->yaw, b->wp.cur);
    if (e->id > g_match.nbots) g_match.nbots = e->id;   // grow the roster to fit
  }
}

// One tick of a foreign entity's WEAPON PRESENTATION — the client half of the
// "presentation is absent, the client re-solves it" contract at net_ent_from_live.
// Nothing here is on the wire, and nothing here needs to be: each clock is re-solved
// from something the client was given, through the same one copy the server ran.
//
//   the AIM BLEND, from the NEF_ADS intent, through the one gun_ads — so a remote
//     figure shoulders its weapon on exactly the weapon's own ADS TIME rather than
//     on the snapshot cadence;
//   the RELOAD CLOCK, started by EV_RELOAD in ent_on_event and counted down here on
//     the same gun_reload_tick the shooter ran — without the countdown reload_t would
//     be stamped once and never move, i.e. a magazine change that begins and never
//     ends;
//   the BARREL, following the recoil target each shot echo set (gun_recoil_step, whose
//     rec_hold releases the crown a round interval after the last echo — a viewer has
//     no trigger to watch). Heat is NOT advanced here and must not be: its readers are
//     the grip correction and the round's cone, both of which belong to the process
//     that simulates the shooter, and the visual kick asks the round's index instead.
//
// FOREIGN ONLY, both of them: our own body is predicted, and re-running either clock
// on top of prediction would double it.
//
// This sits BEFORE net_cl_extrapolate's staleness cap on purpose. Projecting a
// POSITION on stale velocity walks a body through a wall, so that stops; these
// converge on the last known intent, so continuing them costs at worst a few ms of
// blend and freezing them leaves a half-raised rifle hanging.
static void net_cl_present_advance(void) {
  int me = human_of_ent(g_cl.my_ent);
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h] || h == me) continue;
    player_t *p = &g_players[h];
    if (!p->alive) continue;
    gun_ads(&p->wp, p->wp.ads_want);   // idempotent on the intent it re-publishes
    gun_reload_tick(&p->wp);
    gun_recoil_step(&p->wp);
    gun_sway_tick(&p->wp, p->vel, p->grounded, p->yaw, p->pitch);
  }
  for (int i = 0; i < g_match.nbots; i++) {
    bot_t *b = &g_bots[i];
    if (!b->active || !b->alive) continue;
    gun_ads(&b->wp, b->wp.ads_want);
    gun_reload_tick(&b->wp);
    gun_recoil_step(&b->wp);
    gun_sway_tick(&b->wp, b->vel, b->grounded, b->yaw, b->pitch);
  }
}

// One tick of dead reckoning for every FOREIGN entity.
static void net_cl_extrapolate(void) {
  net_cl_present_advance();

  // BOUNDED dead reckoning: snapshots correct the projection every two
  // ticks; when they STOP (loss burst, server stall) unbounded projection
  // glides every foreign entity through walls on stale velocity until the 5 s
  // timeout. 24 ticks (200 ms) is the Source-class cap: beyond it entities
  // hold position and the next snapshot teleports them honestly.
  if (g_cl.seq - g_cl.snap_seq > 24) return;
  int me = human_of_ent(g_cl.my_ent);
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h] || h == me) continue;
    player_t *p = &g_players[h];
    if (!p->alive) continue;
    p->prev_pos = p->pos;
    p->pos = v3_add(p->pos, v3_scale(p->vel, TICK_DT));
  }
  for (int i = 0; i < g_match.nbots; i++) {
    bot_t *b = &g_bots[i];
    if (!b->active || !b->alive) continue;
    b->prev_pos = b->pos;
    b->prev_yaw = b->yaw;      // no angular velocity on the wire: the heading
                               // holds, but the PAIR still has to step or the
                               // render lerp keeps reaching back a whole tick
    b->pos = v3_add(b->pos, v3_scale(b->vel, TICK_DT));
  }
}

// REPLAY THE PENDING INPUTS: the caller has just adopted the server's authoritative
// state AFTER input `li`; every input in (li, seq) is already on the wire and the
// server WILL play it — after a respawn the server is already alive and walking one RTT
// before this client knows.
static void net_cl_replay(player_t *p, int h, uint32_t li) {
  net_cl_depenetrate(p);     // the adopted fix may sit a wire-rounding inside
                             // a wall; replaying from there teleports
  int predicting_save = g_predicting;
  int replay_save = g_prediction_replay;
  g_prediction_replay = 1;
  for (uint32_t rt = li + 1; (int32_t)(g_cl.seq - rt) > 0; rt++) {
    const net_input_t *rin = &g_cl.inlog[rt % NET_HIST];
    if (rin->tick != rt) break;              // ring recycled: stop cleanly
    int rheld[IN_COUNT];
    for (int b = 0; b < IN_COUNT; b++) rheld[b] = (rin->buttons >> b) & 1;
    float ry = net_dq_yaw(rin->yaw), rp = net_dq_pitch(rin->pitch);
    net_validate_angles(&ry, &rp, p->yaw);   // the chain, as sent
    p->yaw = ry; p->pitch = rp;
    g_predicting = 1;
    player_move_tick(p, g_cl.my_ent, rheld,
                     net_dq_wish(rin->wish_x), net_dq_wish(rin->wish_y));
    g_predicting = predicting_save;
    g_cl.predpos[rt % NET_HIST] = p->pos;
    // Replay replaces history, not merely the live endpoint: a later ack may anchor on
    // any one of these ticks, so every derived field must describe the recomputed run.
    g_cl_pred[rt % NET_HIST] =
        (cl_pred_t){.wp = p->wp, .vm = p->vm, .rng = g_rng_gun[h],
                    .yaw = p->yaw};
  }
  g_predicting = predicting_save;
  g_prediction_replay = replay_save;
}

static void net_cl_snapshot(const unsigned char *b, int n) {
  uint32_t st, li, wh, sd;
  uint8_t qd = 0;
  net_ent_t es[MAX_ENTS];
  net_ev_t  evs[NET_EV_SNAP_MAX];
  int full = 0, nev = 0;
  int cnt = net_snapshot_read(b, (size_t)n, &st, &li, &wh, &sd, &qd, es, MAX_ENTS,
                              &full, evs, NET_EV_SNAP_MAX, &nev);
  if (cnt < 0) return;
  // Signed-difference like every other monotone-counter compare in this layer (ev ack,
  // resend window): a plain unsigned '<' inverts at the 32-bit tick wrap (~414 days of
  // server uptime) and then drops every later snapshot.
  if (g_cl.have_snap && (int32_t)(st - g_cl.server_tick) < 0)
    return;   // stale/out-of-order: drop
  g_cl.have_snap = 1;
  g_cl.server_tick = st;
  g_cl.snap_seq = g_cl.seq;                  // dead-reckoning ticks count from here
  // Server-side input-queue depth, smoothed: the clock nudge's input.
  g_cl.qdepth_ema += ((float)qd - g_cl.qdepth_ema) * 0.1f;
  g_cl.world = wh;
  // FOLLOW THE SERVER'S ARENA. A rematch regenerates it, so the seed can move
  // mid-session; adopt it and re-upload the world VBO. Then check the hash the
  // same way the join does — if the two disagree after regenerating from the
  // server's own seed, the two builds do not agree on the generator, and the join
  // says refuse rather than play a divergent map.
  if (sd != g_map_seed) {
    map_generate(sd); g_map_seed = sd;
    g_world_dirty = 1;
  }
  if (wh != net_world_hash()) {
    g_cl.reject = NR_WORLD;
    g_cl.state = NC_FAILED;
    return;
  }
  // Foreign entities are applied straight from the newest snapshot.
  for (int i = 0; i < cnt; i++)
    if ((int)es[i].id != g_cl.my_ent) net_cl_apply_ent(&es[i]);
  // A ROSTER SHRINKS TOO. Every snapshot carries the FULL roster (the server walks
  // ENT_FOREACH with no culling), so absence is information: a bot that yielded its
  // slot to a joining human, or a human who left, is simply not in the record list.
  if (full) {
    unsigned char seen[MAX_ENTS] = {0};
    for (int i = 0; i < cnt; i++) seen[es[i].id] = 1;
    int me = human_of_ent(g_cl.my_ent);
    // From slot 0, not 1.
    for (int h = 0; h < MAX_HUMANS; h++)
      if (h != me && !seen[ent_of_human(h)]) g_humans_on[h] = 0;
    int top = 0;
    for (int e = 1; e <= MAX_BOTS; e++) {
      if (seen[e]) top = e;
      else g_bots[e - 1].active = 0;
    }
    g_match.nbots = top;
    // Departure clears the SCORE ROW too (the server does the same in
    // net_srv_drop; NP_SCORES only carries present entities, so a leaver's
    // kills otherwise stay on the board as a frozen phantom lead).
    for (int e = 0; e < MAX_ENTS; e++)
      if (!seen[e] && e != g_cl.my_ent) {
        g_match.kills[e] = g_match.deaths[e] = 0;
        g_match.shots[e] = g_match.hits[e] = 0;
        g_net_ping[e] = 0;
      }
  }
  // The LOCAL player's authoritative block: reconcile. last_input_
  // tick (`li`) is the highest input seq the server has applied — the anchor.
  for (int i = 0; i < cnt; i++) {
    if ((int)es[i].id != g_cl.my_ent) continue;
    int h = human_of_ent(g_cl.my_ent);
    if (h < 0) break;
    player_t *p = &g_players[h];
    v3 auth = {{net_dq_pos(es[i].x), net_dq_pos(es[i].y), net_dq_pos(es[i].z)}};
    v3 avel = {{net_dq_vel(es[i].vx), net_dq_vel(es[i].vy), net_dq_vel(es[i].vz)}};
    uint8_t alive = !!(es[i].flags & NEF_ALIVE);
    if (!g_humans_on[h]) g_humans_on[h] = 1;
    // The PREDICTION ERROR, and the CORRECTION, both anchor on predpos[li] — our own
    // recorded prediction for the tick the server just acked.
    int synced = g_cl.synced;
    int anchor = synced && alive && p->alive &&
                 (int32_t)(li - g_cl.sync_seq) >= 0 &&
                 (int32_t)(g_cl.seq - li) >= 0 && g_cl.seq - li < NET_HIST;
    // Computed BEFORE the error metric: a spawn_seq edge without a death is a
    // match-restart teleport — a legitimate server-side move, not a
    // misprediction, and counting it made every rematch read as one
    // correction in a metric whose healthy value is 0.
    int respawned = g_cl.have_spawn_seq && es[i].spawn_seq != g_cl.spawn_seq;
    v3 err = {{0, 0, 0}};
    if (anchor) {
      v3 pp = g_cl.predpos[li % NET_HIST];
      err = v3_sub(auth, pp);
      float e_mm = sqrtf(err.x * err.x + err.z * err.z) * 1000.0f;
      if (respawned) e_mm = 0.0f;              // teleport, not error
      if (e_mm > g_cl_worst_err_mm) g_cl_worst_err_mm = e_mm;
      if (e_mm > 50.0f) {                      // > 5 cm at the acked tick
        g_cl_corrections++;
        if (g_net_debug)
          printf("netdbg corr li=%u seq=%u err=%.0fmm auth=(%.2f %.2f) pred=(%.2f %.2f) vel=(%.2f %.2f) grounded=%d\n",
                 li, g_cl.seq, (double)e_mm, (double)auth.x, (double)auth.z,
                 (double)pp.x, (double)pp.z, (double)avel.x, (double)avel.z,
                 !!(es[i].flags & NEF_GROUNDED));
      }
    }
    // A big gap or a first fix is a SPAWN/desync, not drift: snap hard and re-solve the
    // pose.
    float cdx = auth.x - p->pos.x, cdz = auth.z - p->pos.z;
    float cur_err = anchor ? sqrtf(err.x * err.x + err.z * err.z)
                           : sqrtf(cdx * cdx + cdz * cdz);
    // A RESPAWN is the only thing that may move the player's AIM. Everything else here
    // corrects POSITION. Taking the server's yaw/pitch on every correction meant the
    // view snapped back to where it was pointing one RTT ago on every hitch or dropped
    // packet — invisible on localhost, a violent mouse yank over a real link.
    g_cl.spawn_seq = es[i].spawn_seq;          // respawned: computed above,
    g_cl.have_spawn_seq = 1;                   // before the error metric
    if (respawned || !synced || cur_err > 2.0f || !alive || !p->alive) {
      if (!synced) {
        // SPAWN THE LOCAL SLOT BEFORE ADOPTING THE SERVER'S STATE. Until this instant
        // the player was entity 0; g_players[h] for the server's assigned slot is still
        // the zeroed struct, magazines included — so everything the client predicted
        // out of it started EMPTY (HUD 0/30, dry-fire on every trigger pull, viewmodel
        // never cycling) while the server fired a full magazine. player_spawn is pure,
        // so calling it here costs nothing and desyncs nothing.
        float carry_yaw = g_players[0].yaw, carry_pitch = g_players[0].pitch;
        player_spawn(p);
        p->yaw = carry_yaw; p->pitch = carry_pitch;
        g_rng_gun[h] = rng_gun_seed(g_map_seed, h);   // mirror net_srv_admit:
                                                      // both sides draw the
                                                      // same recoil stream
        // THE LOCAL MATCH'S SCORE ROWS ARE NOT THIS MATCH'S — the pre-join
        // arena ran through the whole handshake scoring into g_match.
        // NP_SCORES refills the truth within 250 ms.
        match_scores_clear();
        // Our own name renders from g_human_name like every human's:
        // the server admits us under it, but nothing mirrored it locally —
        // scoreboard and killfeed called the local player REMOTE.
        snprintf(g_human_name[h], sizeof g_human_name[h], "%s", g_cl.name);
        g_local_ent = g_cl.my_ent;
        g_online = 1;
        g_cl.sync_seq = g_cl.seq;    // predictions are real from HERE on
      }
      if (respawned) net_cl_reset_life(p, li);
      p->pos = p->prev_pos = auth; p->vel = avel;
      if (respawned) { p->yaw = net_dq_yaw(es[i].yaw); p->pitch = net_dq_pitch(es[i].pitch); }
      g_cl.prev_yaw = p->yaw;              // the prev chain re-anchors here,
      g_cl.have_prev_yaw = 1;              // exactly as the server's does
      p->wp.cur = es[i].weapon;
      anim_reset(&g_player_anim[h], auth, p->yaw, p->wp.cur);
      // FLOOD THE PREDICTION HISTORY WITH THE ADOPTED FIX. The ring still holds pre-
      // teleport positions for the ~RTT of ticks the server has yet to ack; without
      // this every snapshot of that window re-measured the corpse->spawn distance as
      // "error" (worst_err 28 m on LOOPBACK), took the >2 m hard branch again and
      // anim_reset the fresh spawn once per snapshot — a pose pop per respawn and a
      // metric that buried real regressions.
      for (int k = 0; k < NET_HIST; k++) g_cl.predpos[k] = auth;
      g_cl.synced = 1;
      // The hard branch adopts the server's own derived state wholesale — a
      // spawn or desync means the prediction stream is broken anyway.
      p->grounded = !!(es[i].flags & NEF_GROUNDED);
      p->crouched = !!(es[i].flags & NEF_CROUCH); p->sliding = !!(es[i].flags & NEF_SLIDE);
      // A respawn starts from player_spawn above, then adopts the server's post-input
      // state. The snapshot carries the effective Lean and tuck intent; the local
      // command/clearance split is re-anchored to that one observable value.
      if (respawned) {
        p->prev_grounded = p->grounded;
        p->tuck_y = (es[i].flags & NEF_TUCK) ? HEIGHT_STAND - HEIGHT_CROUCH : 0.0f;
        p->eye = p->prev_eye = EYE_STAND - p->tuck_y;
        float lean = net_dq_wish(es[i].lean);
        p->lean_in = p->lean = p->prev_lean = lean;
        p->lean_clr = 1.0f;
        p->wp.ads_want = !!(es[i].flags & NEF_ADS);
        p->wp.ready_want = !!(es[i].flags & NEF_READY);
        p->vm.last_eye = p->eye;
      } else if (alive) {
        net_cl_restore_pred(p, h, li);
      }
      p->wp.cur = es[i].weapon;
      p->wp.ammo[p->wp.cur] = es[i].mag;
      // REPLAY THE PENDING INPUTS. The adopted fix is the state after input `li`; every
      // input in (li, seq) is already on the wire and the server WILL play it — after a
      // respawn it is already alive and walking one RTT before this client knows.
      if (p->alive && !alive)                // death edge: presentation-only
        p->respawn_t = PLAYER_RESPAWN_TICKS; // countdown for the REDEPLOY bar
      else if (alive)                        // (the sim's own respawn_t is
        p->respawn_t = 0;                    // server state and never arrives)
      p->alive = alive;                      // BEFORE the replay: the move
      p->hp = es[i].hp;                      // tick's dead branch is a no-op
      if (alive) {
        net_cl_replay(p, h, li);
        g_cl.prev_yaw = p->yaw;                // the prev chain continues from here
      }
    } else if (anchor && sqrtf(err.x * err.x + err.z * err.z) > 0.05f) {
      // ROLLBACK HEAL: a real divergence (> 5 cm at the acked tick).
      p->prev_pos = p->pos;                  // one honest render lerp
      p->pos = auth; p->vel = avel;
      p->grounded = !!(es[i].flags & NEF_GROUNDED);
      p->crouched = !!(es[i].flags & NEF_CROUCH); p->sliding = !!(es[i].flags & NEF_SLIDE);
      net_cl_restore_pred(p, h, li);         // recorded derived state at li…
      p->wp.cur = es[i].weapon;              // …then the server's weapon truth
      p->wp.ammo[p->wp.cur] = es[i].mag;     // replay re-predicts pending shots
      g_cl.predpos[li % NET_HIST] = auth;
      net_cl_replay(p, h, li);
      g_cl.prev_yaw = p->yaw;
    } else if (anchor) {
      // Move the RESIDUAL only, a quarter per snapshot, and shift the recorded
      // prediction history by the same amount — otherwise the next snapshot measures
      // the same error again and the correction compounds.
      v3 c = v3_scale(err, 0.25f);
      p->pos = v3_add(p->pos, c);
      net_cl_depenetrate(p);    // the bleed target is a quantized position and
                                // may sit a wire-rounding inside a wall
      for (int k = 0; k < NET_HIST; k++)
        g_cl.predpos[k] = v3_add(g_cl.predpos[k], c);
      // Magazine: measure the desync AT THE ACKED TICK — the recorded prediction for li
      // against the server's mag at li — and shift the CURRENT predicted magazine by
      // that error, keeping in-flight shots.
      if (net_cl_hist_ok(li) && g_cl_pred[li % NET_HIST].wp.cur == es[i].weapon) {
        int am = g_cl_pred[li % NET_HIST].wp.ammo[es[i].weapon] - (int)es[i].mag;
        if (am) {
          // Clamped BOTH ways: around a respawn the ring can still hold the
          // dying magazine against the fresh one and the shift overshoots
          // (read 31/30 live before the cap).
          int mag = p->wp.ammo[p->wp.cur] - am;
          int cap = WPN_DEF[p->wp.cur].mag;
          p->wp.ammo[p->wp.cur] = mag < 0 ? 0 : mag > cap ? cap : mag;
        }
      } else {
        int am = p->wp.ammo[p->wp.cur] - (int)es[i].mag;   // no record: old rule
        if (am < -1 || am > 1) p->wp.ammo[p->wp.cur] = es[i].mag;
      }
    } else {
      p->pos.x += cdx * 0.25f; p->pos.z += cdz * 0.25f;   // no anchor: old bleed
      net_cl_depenetrate(p);
    }
    p->hp = es[i].hp; p->alive = alive;
  }
  // EVENTS LAST, and only once the entities are current: vfx_on_event anchors tracers
  // and flashes on the shooter, and the killfeed names entities that must already
  // exist.
  for (int k = 0; k < nev; k++) {
    if ((int32_t)(evs[k].id - g_cl.ev_high) <= 0) { g_cl_ev_dupes++; continue; }
    event_t e;
    net_ev_to_live(&e, &evs[k]);
    // The death caption's killer is per-viewer state match_on_kill derives
    // from g_local_ent — which is the SERVER'S -1 when the kill happens, so
    // the client derives it here with the identical rule ("BY <own name>"
    // was ent_name(0) falling back to the player's own config name).
    if (e.type == EV_KILL && e.dst == g_local_ent && e.src >= 0)
      g_match.last_killer = e.src;
    ev_push(e);
    g_cl.ev_high = evs[k].id;
    g_cl_ev_applied++;
  }
}

// One predicted client tick: store the input, run our OWN move locally (events
// and damage suppressed — those are the server's), and send it stamped with a
// monotone seq the snapshot echoes back for reconciliation.
static void net_cl_tick(const int held[IN_COUNT], float wx, float wy,
                        float yaw, float pitch) {
  if (g_cl.state != NC_PLAYING || g_cl.my_ent < 0) return;
  int h = human_of_ent(g_cl.my_ent);
  if (h < 0) return;
  player_t *p = &g_players[h];
  uint16_t btn = 0;
  for (int k = 0; k < IN_COUNT; k++) if (held[k]) btn |= (uint16_t)(1u << k);
  // PREDICT ON THE VALUES THE SERVER WILL ACTUALLY SIMULATE. The wire quantizes angles
  // and the wish vector; predicting on the raw floats and sending the rounded ones
  // makes client and server run different inputs every single tick, so reconciliation
  // spends its budget correcting a difference the client created.
  yaw   = net_dq_yaw(net_q_yaw(yaw));
  pitch = net_dq_pitch(net_q_pitch(pitch));
  wx    = net_dq_wish(net_q_wish(wx));
  wy    = net_dq_wish(net_q_wish(wy));
  // MIRROR THE SERVER'S ANGLE VALIDATION before predicting: the record
  // carries the raw quantized angles, the server dequantizes and validates
  // against ITS previous yaw — an identical input sequence makes both prev
  // chains identical, so validating here keeps the two sims bit-exact even
  // through a flick past NET_MAX_TURN_PER_TICK. Predicting the unvalidated
  // yaw rotated the move basis against the server's for exactly that tick.
  {
    float vy = yaw, vp = pitch;
    net_validate_angles(&vy, &vp, g_cl.have_prev_yaw ? g_cl.prev_yaw : yaw);
    p->yaw = vy; p->pitch = vp;
    g_cl.prev_yaw = vy;
    g_cl.have_prev_yaw = 1;
  }
  // The VIEW tick: the server tick whose world this input was aimed at — the last
  // applied snapshot's tick plus one dead-reckoning tick per client tick since
  // (net_cl_extrapolate advances the targets exactly so).
  uint32_t view = g_cl.synced ? g_cl.server_tick + (g_cl.seq - g_cl.snap_seq) : 0;
  // Log BEFORE predicting: the record is what the packet carries, and the
  // redundancy tail re-sends it for the next NET_IN_REDUN packets.
  g_cl.inlog[g_cl.seq % NET_HIST] =
      (net_input_t){.tick = g_cl.seq, .view = view, .buttons = btn,
                    .yaw = net_q_yaw(yaw), .pitch = net_q_pitch(pitch),
                    .wish_x = net_q_wish(wx), .wish_y = net_q_wish(wy)};
  // Predict only while ALIVE: the move tick's dead branch counts down respawn_t and
  // self-respawns — but respawn_t is set in ent_damage, which never runs on a client,
  // so the stale counter respawned the predicted player instantly and every snapshot
  // snapped it back to the corpse, an oscillation at snapshot rate for the whole death
  // screen.
  if (g_cl.synced && p->alive) {
    g_predicting = 1;
    player_move_tick(p, g_cl.my_ent, held, wx, wy);
    g_predicting = 0;
  }
  g_cl.predpos[g_cl.seq % NET_HIST] = p->pos;   // record for the error metric
  g_cl_pred[g_cl.seq % NET_HIST] =              // …and the derived state a
      (cl_pred_t){.wp = p->wp, .vm = p->vm, .rng = g_rng_gun[h],
                  .yaw = p->yaw};
                                                 // rollback replay restores
  net_cl_send_inputs();
  g_cl.seq++;
}

// One client input packet: the event ack + the last NET_IN_REDUN input ticks (oldest
// first, the order the server's queue insert expects).
static void net_cl_send_inputs(void) {
  if (g_cl.state != NC_PLAYING) return;
  // 8 B header + NET_IN_REDUN records; sized with headroom so a future input
  // field cannot silently latch w.bad and truncate the newest record.
  unsigned char b[128]; np_wr w = {b, sizeof b, 0, 0};
  np_w16(&w, NET_PROTO); np_w8(&w, NP_INPUT);
  np_w32(&w, g_cl.token);                     // session token
  np_w32(&w, g_cl.ev_high);                   // event ack
  uint32_t lo = g_cl.seq >= NET_IN_REDUN - 1 ? g_cl.seq - (NET_IN_REDUN - 1) : 0;
  np_w8(&w, (uint8_t)(g_cl.seq - lo + 1));
  for (uint32_t t = lo; t != g_cl.seq + 1; t++)
    net_input_encode(&w, &g_cl.inlog[t % NET_HIST]);
  net_udp_send(g_cl.fd, g_cl.ip, g_cl.port, b, (int)w.at);
  g_cl.last_send_ms = net_now_ms();
}


// Drain the socket, drive the automaton, honour timeouts. Returns 1 while a
// connection is live or forming, 0 once it has failed (caller falls back local).
static int net_cl_poll(void) {
  if (g_cl.state == NC_OFF || g_cl.fd < 0) return 0;
  uint32_t now = net_now_ms();
  unsigned char b[2048];
  uint32_t ip; uint16_t port;
  int n, guard = 0;
  while ((n = net_udp_recv(g_cl.fd, &ip, &port, b, sizeof b)) > 0 && guard++ < 256) {
    // Source ADDRESS and PORT: the port is half the identity of the socket we
    // are talking to, and without it any other service on the same host (or
    // anything that can guess it) can inject into the session.
    if (ip != g_cl.ip || port != g_cl.port) continue;
    if (n < 3) continue;
    np_rd r = {b, (size_t)n, 0, 0};
    if (np_r16(&r) != NET_PROTO) continue;
    int type = np_r8(&r);
    g_cl.last_recv_ms = now;
    if (type == NP_COOKIE && g_cl.state == NC_CONNECTING) {
      g_cl.cookie = np_r32(&r);
      g_cl.state = NC_JOINING;
      net_cl_send_join();
    } else if (type == NP_ACCEPT && g_cl.state == NC_JOINING) {
      int me = np_r8(&r);
      int lob = np_r8(&r), lobn = np_r8(&r);
      uint32_t seed = np_r32(&r);
      uint32_t world = np_r32(&r);
      uint32_t token = np_r32(&r);
      // The id the server hands out is an INDEX into g_players for the rest of the
      // session (human_of_ent), so it is validated once, here, at the only place it
      // enters the process.
      if (me >= MAX_ENTS || human_of_ent(me) < 0) {
        g_cl.reject = NR_PROTO; g_cl.state = NC_FAILED; return 0;
      }
      map_generate(seed); g_map_seed = seed;  // adopt the server's arena
      // A DIVERGENT ARENA IS REFUSED, NOT PLAYED. Both sides generate
      // the map from the seed, so the hashes agree unless the generators do
      // not — and then every prediction, every trace and every spawn is wrong
      // in a way that reads as cheating rather than as a version mismatch.
      if (world != net_world_hash()) {
        g_cl.reject = NR_WORLD; g_cl.state = NC_FAILED; return 0;
      }
      // Cosmetic only — the scoreboard names the arena so a player can tell two friends
      // whether they landed together.
      g_cl.lobby   = lob  < SRV_LOBBIES ? lob : 0;
      g_cl.lobbies = lobn >= 1 && lobn <= SRV_LOBBIES ? lobn : 1;
      g_cl.my_ent = me;
      g_cl.world = world;
      g_cl.token = token;
      // Adopt the SERVER's sim tuning(light) and predict with it from the first tick.
      for (int i = 0; i < MV_COUNT; i++) {
        float v = np_rf(&r);
        g_cl.srv_mv[i] = v >= MV_DEF[i].lo ? (v <= MV_DEF[i].hi ? v : MV_DEF[i].hi)
                                           : MV_DEF[i].lo;
      }
      for (int wp = 0; wp < WPN_COUNT; wp++)
        for (int i = 0; i < WP_COUNT; i++) {
          float v = np_rf(&r);
          g_cl.srv_wp[wp][i] = v >= WP_DEF[i].lo ? (v <= WP_DEF[i].hi ? v : WP_DEF[i].hi)
                                                 : WP_DEF[i].lo;
        }
      g_sim_mv = g_cl.srv_mv;
      g_sim_wp = g_cl.srv_wp;
      g_world_dirty = 1;                       // renderer re-uploads the VBO
      // g_local_ent / g_online flip only on the FIRST snapshot: keep
      // playing the local match until the server's world actually arrives.
      g_cl.state = NC_PLAYING;
    } else if (type == NP_REJECT && g_cl.state != NC_PLAYING) {
      g_cl.reject = np_r8(&r);
      g_cl.state = NC_FAILED;
      return 0;
    } else if (type == NP_SNAPSHOT && g_cl.state == NC_PLAYING) {
      net_cl_snapshot(b, n);
    } else if (type == NP_SCORES && g_cl.state == NC_PLAYING && g_cl.synced) {
      // synced-gated: rows landing before the first snapshot would only be wiped again
      // by the sync memset — and a stale over=1 pinned the VICTORY banner once already
      // (see the !synced branch).
      net_score_t rows[MAX_ENTS];
      net_name_t names[MAX_HUMANS];
      uint8_t over = 0, winner = 0;
      uint16_t frag = 0;
      int nn = 0;
      int nr = net_scores_read(b, (size_t)n, &over, &winner, &frag, rows,
                               MAX_ENTS, names, MAX_HUMANS, &nn);
      if (nr >= 0) {
        // WHOLESALE MEANS WHOLESALE.
        match_counters_clear();
        memset(g_net_ping, 0, sizeof g_net_ping);
        for (int h = 1; h < MAX_HUMANS; h++) g_human_name[h][0] = '\0';
        for (int i = 0; i < nr; i++) {
          int e = rows[i].id;
          g_match.kills[e] = rows[i].kills;
          g_match.deaths[e] = rows[i].deaths;
          g_match.shots[e] = rows[i].shots;
          g_match.hits[e] = rows[i].hits;
          g_net_ping[e] = rows[i].ping;
        }
        for (int i = 0; i < nn; i++) {
          int h = human_of_ent(names[i].id);
          if (h >= 0)
            snprintf(g_human_name[h], sizeof g_human_name[h], "%s", names[i].name);
        }
        g_match.over = over ? 1 : 0;
        if (over) g_match.winner = winner == 0xFF ? 0 : winner;
        g_net_fraglimit = frag;
      }
    }
  }
  // timeouts / resends
  if (g_cl.state == NC_CONNECTING || g_cl.state == NC_JOINING) {
    if (now - g_cl.connect_start_ms > 4000) { g_cl.state = NC_FAILED; return 0; }  // connect timeout
    if (now - g_cl.last_send_ms > 250) {      // resend the last handshake step
      if (g_cl.state == NC_CONNECTING) net_cl_send_connect(); else net_cl_send_join();
    }
  } else if (g_cl.state == NC_PLAYING) {
    if (now - g_cl.last_recv_ms > 5000) { g_cl.state = NC_FAILED; return 0; }  // server gone
  }
  return g_cl.state != NC_FAILED;
}
