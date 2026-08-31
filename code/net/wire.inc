// =============================================================================
// net/wire.inc  —  packet encode/decode, the np_rd bounds-checked cursor,
// quantization, net_world_hash.
// Proof: netpack, netfuzz, netcarry. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: every decoder treats input as hostile — np_rd's `bad` latch
// makes every post-overflow read return 0; hostile src/dst are neutralised
// to -1 before any array use. NET_PROTO is the wire version; a bump needs
// server and client redeployed together, so an ADDITIVE change into space an
// older build reads as zero (the free NEF_ bits) argues its compatibility at
// its own gate instead. A snapshot carries INPUTS, never solved presentation:
// see the NEF_ set and net_ent_t.
// =============================================================================
// Netcode wire layer — SHARED CORE, above the platform split, exactly the idiom audio
// uses: encode/decode, validation, the connection automaton and the FNV world hash all
// live here where the harness can prove them; only socket()/sendto()/recvfrom() go
// below the split, twice.
// ---------------------------------------------------------------------------

// PROTO is byte 0-1 of EVERY packet, hand-bumped on any wire change.
// 9: hip spread joined the wp table ACCEPT carries.
// 10: the three cone parameters became physical hip/move/heat recoil parameters;
//     layout is unchanged, semantics are not, so mixed builds must not play together.
#define NET_PROTO 10
// Floor on an UNVERIFIED request, so the answer to one is always smaller than the
// request was and the responder cannot be pointed at a forged source address:
// NET_CONNECT_MIN 16 against the 7-byte COOKIE reply.
#define NET_CONNECT_MIN 16
// CEILING on the human slots one source address may hold, and the live bound on a
// single-arena instance is cap_public - 1 — computed in net_srv_admit.
#define NET_JOIN_PER_HOST 4

typedef enum {
  NP_CONNECT = 1,   // client->server, unverified: PROTO + name, zero-padded to
                    // NET_CONNECT_MIN so the COOKIE answer is strictly SMALLER
                    // than the request and the port cannot be reflected
  NP_COOKIE,        // server->client: 8-byte stateless cookie
  NP_JOIN,          // client->server: CONNECT again WITH the cookie
  NP_ACCEPT,        // server->client: your id, map seed, solids hash
  NP_REJECT,        // server->client: reason (proto mismatch, full)
  NP_INPUT,         // client->server: input_tick(s) + event ack
  NP_SNAPSHOT,      // server->client: world state
  NP_LEAVE,         // client->server: I am going. Frees the slot NOW instead
                    // of at the idle timeout — without it a quit-and-
                    // rejoin arrives from a fresh ephemeral port, finds its
                    // old conn still holding a human slot, and can be refused
                    // an arena that is half empty.
  NP_SCORES,        // server->client, 4 Hz: over/winner/fraglimit + one score
                    // row per entity (kills/deaths/shots/hits/ping).
  NP_TELE,          // client->server, fire-and-forget: anonymous usage beat
                    // APPENDED, so no existing value shifts and NET_PROTO is
                    // deliberately NOT bumped: the packet is additive
                    // and never answered, and an old server SILENTLY DROPS it
                    // — the else-if chain in net_srv_packet has no fallback branch, and
                    // the 4-byte NR_PROTO reject fires only on a *protocol* mismatch.
} np_type_t;

// Values are pinned: they ride the NP_REJECT payload, and 3/4 (bad cookie /
// bad name) were retired without producers — a bad cookie is a deliberate
// silent drop (replying would be a reflection vector) and names are
// sanitized with a PLAYERn fallback, never rejected.
typedef enum {
  NR_PROTO = 1, NR_FULL = 2,
  NR_WORLD = 5,   // client-side only: the server's arena hash is not ours
  NR_LOST  = 6,   // client-side only: the link died MID-MATCH. It never rides a
                  // packet either — the server that would have sent it is the
                  // thing that stopped answering.
} np_reject_t;

// Client input for one tick: the wish vector is post-deadzone/curve (the server must
// never re-apply ITS pad settings), angles are QUANTIZED so the client predicts with
// the exact values the server uses — float-yaw prediction against an i16 server angle
// is a per-tick systematic bias, the signature of permanent rubber-banding.
typedef struct {
  uint32_t tick;
  uint32_t view;         // SERVER tick whose world this input was aimed at:
                         // last applied snapshot's tick + the dead-reckoning ticks
                         // since.
  uint16_t buttons;      // IN_COUNT fits in 16 bits
  int16_t  yaw, pitch;   // yaw: full circle -> i16; pitch: +/-PITCH_MAX -> i16
  int16_t  wish_x, wish_y;
} net_input_t;
// ...and that comment is a CHECKED claim: a 17th action is a wire change and a
// NET_PROTO bump, not a local one. One gate on the constant covers all three places
// the field is masked.
static_assert(IN_COUNT <= 16,
              "net_input_t.buttons is a uint16: an action is a WIRE change");

// A cursor-based writer/reader with hard bounds — the ONLY way bytes move, so no decode
// path can run past its buffer. `bad` latches on any overflow and every read after it
// returns 0, so a truncated packet yields zeros, never a wild read.
typedef struct { unsigned char *p; size_t n, at; int bad; } np_wr;
typedef struct { const unsigned char *p; size_t n, at; int bad; } np_rd;

static void np_w8(np_wr *w, uint8_t v) {
  if (w->at + 1 > w->n) { w->bad = 1; return; }
  w->p[w->at++] = v;
}
static void np_w16(np_wr *w, uint16_t v) { np_w8(w, v & 0xff); np_w8(w, v >> 8); }
static void np_w32(np_wr *w, uint32_t v) { np_w16(w, v & 0xffff); np_w16(w, v >> 16); }
static uint8_t np_r8(np_rd *r) {
  if (r->bad || r->at + 1 > r->n) { r->bad = 1; return 0; }
  return r->p[r->at++];
}
static uint16_t np_r16(np_rd *r) { uint16_t a = np_r8(r); return a | (uint16_t)np_r8(r) << 8; }
static uint32_t np_r32(np_rd *r) { uint32_t a = np_r16(r); return a | (uint32_t)np_r16(r) << 16; }
// Floats as raw IEEE-754 bits — ONLY for the ACCEPT tuning tables, where both sims must
// multiply with the exact same value.
static void np_wf(np_wr *w, float v) { uint32_t b; memcpy(&b, &v, 4); np_w32(w, b); }
static float np_rf(np_rd *r) {
  uint32_t b = np_r32(r); float v; memcpy(&v, &b, 4); return v;
}

// Angle quantization: yaw wraps a full turn to the i16 range, pitch maps +/-PITCH_MAX.
// The client dequantizes and predicts with THESE, so both sides see one value.
static int16_t net_q_yaw(float y)   { return (int16_t)lrintf(wrap_pi(y) * (32767.0f / F_PI)); }
static float   net_dq_yaw(int16_t q){ return (float)q * (F_PI / 32767.0f); }
static int16_t net_q_pitch(float p) { return (int16_t)lrintf(f_clamp(p, -PITCH_MAX, PITCH_MAX) * (32767.0f / PITCH_MAX)); }
static float   net_dq_pitch(int16_t q){ return f_clamp((float)q * (PITCH_MAX / 32767.0f), -PITCH_MAX, PITCH_MAX); }
static int16_t net_q_wish(float w)  { return (int16_t)lrintf(f_clamp(w, -1.0f, 1.0f) * 32767.0f); }
static float   net_dq_wish(int16_t q){ return f_clamp((float)q * (1.0f / 32767.0f), -1.0f, 1.0f); }

// The record carries NO type byte — NP_INPUT is the PACKET header's job
// (net_cl_send_inputs writes it once).
static void net_input_encode(np_wr *w, const net_input_t *in) {
  np_w32(w, in->tick);
  np_w32(w, in->view);
  np_w16(w, in->buttons);
  np_w16(w, (uint16_t)in->yaw);
  np_w16(w, (uint16_t)in->pitch);
  np_w16(w, (uint16_t)in->wish_x);
  np_w16(w, (uint16_t)in->wish_y);
}
// Returns 1 on a fully-consumed, valid record.
static int net_input_decode(np_rd *r, net_input_t *in) {
  in->tick   = np_r32(r);
  in->view   = np_r32(r);
  in->buttons = np_r16(r) & ((1u << IN_COUNT) - 1u);
  in->yaw    = (int16_t)np_r16(r);
  in->pitch  = (int16_t)np_r16(r);
  in->wish_x = (int16_t)np_r16(r);
  in->wish_y = (int16_t)np_r16(r);
  return !r->bad;
}

// Telemetry: 23 fixed bytes, fire-and-forget, NEVER answered — a reply to an unverified
// source is a UDP amplifier.
enum { NET_TELE_LEN = 23 };
// THE 90 s BOUNDARY, named once. A beat says "this id spent dt seconds in mode X"; a
// gap above this is sleep/freeze/packet loss and credits nothing.
enum { NET_TELE_DT_MAX = 90 };
typedef struct { uint32_t id; int kind, mode, os, dt; char ver[13]; } net_tele_t;

// Both platforms call this now — tele_tick's per-minute beat is shared core.
static void net_tele_encode(unsigned char *b, uint32_t id, int kind, int mode, int dt) {
  np_wr w = {b, NET_TELE_LEN, 0, 0};
  np_w16(&w, NET_PROTO);
  np_w8(&w, NP_TELE);
  np_w32(&w, id);
  np_w8(&w, (uint8_t)kind);
  np_w8(&w, (uint8_t)mode);
#ifdef _WIN32
  np_w8(&w, 1);
#else
  np_w8(&w, 0);
#endif
  np_w8(&w, (uint8_t)dt);
  char ver[13] = {0};                      // 12 wire bytes + NUL for snprintf
  if (kind == 0) snprintf(ver, sizeof ver, "%s", BUILD_VERSION);
  for (int i = 0; i < 12; i++) np_w8(&w, (uint8_t)ver[i]);
}

static int net_tele_parse(const unsigned char *b, int n, net_tele_t *t) {
  if (n != NET_TELE_LEN) return 0;
  np_rd r = {b, (size_t)n, 0, 0};          // full packet, header re-checked here
  if (np_r16(&r) != NET_PROTO || np_r8(&r) != NP_TELE) return 0;
  t->id = np_r32(&r);
  t->kind = np_r8(&r); t->mode = np_r8(&r); t->os = np_r8(&r); t->dt = np_r8(&r);
  if (t->kind > 1 || t->mode > 2) return 0;
  if (t->os > 1) t->os = 0;
  if (t->dt > NET_TELE_DT_MAX) t->dt = NET_TELE_DT_MAX;
  for (int i = 0; i < 12; i++) {
    int c = np_r8(&r);
    // SPACE and '=' are excluded on purpose, not by accident: this string is printed
    // into server.log, whose grammar is space-separated key=value and is a CONTRACT with
    // docker/report.awk.
    t->ver[i] = (char)((c > 0x20 && c <= 0x7e && c != '=') ? c : (c ? '?' : 0));
  }
  t->ver[12] = 0;
  return !r.bad;
}

// --- Telemetry: the client heartbeat --------------------------------------
// WHAT LEAVES THE MACHINE, exhaustively: a random install id (tele_id — u32, rolled
// once from the clock, derived from NOTHING), the mode (ui/sp/mp), a seconds counter,
// the OS family, and BUILD_VERSION on the boot ping.
static int              g_tele_fd = -1;
// 0 = still resolving, 0xFFFFFFFF = failed.
static _Atomic uint32_t g_tele_ip;
static long             g_tele_last;    // wall clock of the last accepted beat
static int              g_tele_dead;    // one failure ends telemetry for the session
// "A session has begun" — the whole difference between ui and sp time, set ONCE
// by ui_close_menu (far below, but this is the file's only writer) and never
// cleared. Leaving an online match DOES return to the entry screen (leave_match),
// and this deliberately does not go back with it: the flag says the session has
// played, not where the player is standing, so the `ui` row keeps meaning "never
// got started" and does not silently absorb post-match idle time. See tele_mode.
static int              g_tele_played;

// Runs OFF the frame thread.
static void tele_resolve_body(void) {
  uint32_t ip = net_resolve(g_cfg.mp_host);
  atomic_store(&g_tele_ip, ip ? ip : 0xFFFFFFFFu);
}

// Did the id actually reach the disk? config_write returns void and reports failures to
// stderr only, and a read-only install directory is a REAL deployment.
static int tele_id_persisted(uint32_t id) {
  FILE *f = fopen(g_cfg_path, "r");
  if (!f) return 0;
  char line[128];                        // the loader's own line width
  unsigned v = 0;
  int found = 0;
  while (!found && fgets(line, sizeof line, f))
    found = sscanf(line, "tele_id %u", &v) == 1 && v == id;
  fclose(f);
  return found;
}

// Platform half, defined once in each backend below the split — exactly like
// upd_check_start: spawn the detached thread, and on a spawn failure store the
// sentinel so tele_tick gives up instead of waiting forever.
static void tele_resolve_start(void);

// The mode a beat is credited to, as ONE predicate, because the three rows of `make
// server-logs` are exactly this function's three returns: mp — connected online.
static int tele_mode(void) { return g_online ? 2 : g_tele_played ? 1 : 0; }

// Called once per app_frame — the shared frame step, so both native backends reach it
// and nothing else does.
static void tele_tick(void) {
  int headless = 0;                          // g_headless is Linux-only (declared
#ifndef _WIN32                               // inside #ifndef _WIN32) — the file's
  headless = g_headless;                     // own idiom for reading it from
#endif                                       // shared core, see app_start's gate
  if (g_tele_dead || g_server || headless || !g_cfg.telemetry) return;
  long now = (long)time(NULL);
  if (g_tele_fd < 0) {                       // bring-up: id, resolve, socket, boot ping
    if (!g_cfg.tele_id) {
      // Shared-core entropy: seed_from_clock() lives in the Linux backend below the
      // split and MUST NOT be called or forward-declared here. | 1 keeps 0 meaning "not
      // yet generated".
      g_cfg.tele_id = (((uint32_t)time(NULL) * 2654435761u) ^ net_now_ms()) | 1u;
      config_write(g_cfg_path, &g_cfg);      // the path the session was started with
      // A persist that did not stick would churn the id every launch and
      // invent a player per start — see tele_id_persisted. g_cfg keeps the id
      // in memory on purpose: a later menu-close write may still land it.
      if (!tele_id_persisted(g_cfg.tele_id)) { g_tele_dead = 1; return; }
    }
    static int kicked;
    if (!kicked) { kicked = 1; tele_resolve_start(); return; }   // never blocks
    uint32_t ip = atomic_load(&g_tele_ip);
    if (!ip) return;                                   // still resolving; retry next frame
    if (ip == 0xFFFFFFFFu || (g_tele_fd = net_udp_open(0)) < 0) { g_tele_dead = 1; return; }
    g_tele_last = now;
    unsigned char b[NET_TELE_LEN];
    net_tele_encode(b, g_cfg.tele_id, 0, 0, 0);
    net_udp_send(g_tele_fd, ip, (uint16_t)g_cfg.mp_port, b, NET_TELE_LEN);
    return;
  }
  long gap = now - g_tele_last;
  // A BACKWARDS clock step (NTP correction, manual set, DST on a machine whose RTC is
  // local time) makes gap negative, which falls into `gap < 60` WITHOUT resetting the
  // anchor — beats then stop until the clock catches up, which can be hours.
  if (gap < 0) { g_tele_last = now; return; }
  if (gap < 60) return;
  g_tele_last = now;
  if (gap > NET_TELE_DT_MAX) return;   // slept/frozen/missed: restart the clock,
                                       // credit nothing — THE boundary, shared
                                       // with the wire field and the server
                                       // clamp (decision 7, named at the enum)
  unsigned char b[NET_TELE_LEN];
  net_tele_encode(b, g_cfg.tele_id, 1, tele_mode(), (int)gap);
  net_udp_send(g_tele_fd, atomic_load(&g_tele_ip), (uint16_t)g_cfg.mp_port, b, NET_TELE_LEN);
}

// Per-entity snapshot record: the anim_tick INPUT list (pure physics state —
// anim_t/pose_t/vm_t are 100% presentation, re-solved by each client and NEVER sent)
// plus the state the HUD and the weapon model need. spawn_seq tells the client to
// anim_reset instead of interpolating across a respawn teleport.
// THE FLAG BYTE IS THE CHEAPEST FIELD ON THE WIRE, and the carriage lives in it for
// exactly that reason: a client re-solves the POSE, but only from inputs it was given,
// and three of anim_tick's inputs are booleans the client can never derive on its own
// (it never sees a foreign player's buttons). One bit each, no field added, no record
// grown. Named because there are seven of them now and `>> 5 & 1` next to `>> 3 & 1`
// is the mistake this byte would otherwise be waiting for.
enum {
  NEF_ALIVE    = 1 << 0,
  NEF_GROUNDED = 1 << 1,
  NEF_CROUCH   = 1 << 2,
  NEF_SLIDE    = 1 << 3,
  // The carriage triple — see net_ent_from_live for where each is published and
  // net_cl_present_advance for how the client turns the two intents back into blends.
  NEF_ADS      = 1 << 4,   // aim INTENT, not the blend: the client runs its own ramp
  NEF_READY    = 1 << 5,   // fighting — stock up, rather than the low/patrol carry
  NEF_TUCK     = 1 << 6,   // mid-air crouch: the legs are folded into pos.y, and the
                           // figure must not follow that step (the amount is the one
                           // stand-to-crouch constant, so the bit IS the value)
  // bit 7 free.
};
// ADDITIVE, AND DELIBERATELY NOT A NET_PROTO BUMP — the same argument NP_TELE makes.
// No field moved and no byte was added, and bits 4-6 read zero out of every older
// build: an old client against a new server renders the carriage it always did, a new
// client against an old server likewise. A bump here would lock out every un-updated
// client to fix a pose.
typedef struct {
  uint8_t  id, flags;      // the NEF_* set above
  uint8_t  hp, weapon;     // weapon: 2 bits used
  uint8_t  spawn_seq;
  uint8_t  mag;            // rounds in the CURRENT weapon's magazine. Without
                           // it a lost reload press was invisible forever: the
                           // predicted HUD showed a full mag while the server
                           // dry-fired an empty one — "my gun shoots no
                           // bullets" with no way to reconcile out of it.
  int16_t  x, y, z;        // position, cm precision over +/-320 m
  int16_t  vx, vy, vz;     // velocity, cm/s
  int16_t  yaw, pitch;
  int16_t  lean;           // -1..1
} net_ent_t;

// --- The event channel ---------------------------------------------
// Every event carries its sim tick and a monotone id; the client acks the highest id it
// has seen on every input packet; the server resends everything above that ack for ~200
// ms; the client drops any id <= its high-water mark.
#define NET_EV_SNAP_MAX   20     // records per snapshot; the rest ride the
                                 // resend. 20 covers the measured 8-10 events
                                 // per tick at two ticks per snapshot; the
                                 // full datagram stays under ~1400 B (MTU)
#define NET_EV_RESEND_TCK 36     // ~300 ms at 120 Hz, then it ages out
typedef struct {
  uint32_t id;
  uint8_t  type, src, dst, wpn, k;
  int16_t  i, ax, ay, az, bx, by, bz, f, g, pen;
} net_ev_t;

static int16_t net_q_pos(float m)  { return (int16_t)lrintf(f_clamp(m, -327.0f, 327.0f) * 100.0f); }
static float   net_dq_pos(int16_t q){ return (float)q * 0.01f; }
static int16_t net_q_vel(float v)  { return (int16_t)lrintf(f_clamp(v, -327.0f, 327.0f) * 100.0f); }
static float   net_dq_vel(int16_t q){ return (float)q * 0.01f; }

static void net_ent_encode(np_wr *w, const net_ent_t *e) {
  np_w8(w, e->id); np_w8(w, e->flags);
  np_w8(w, e->hp); np_w8(w, e->weapon);
  np_w8(w, e->spawn_seq);
  np_w8(w, e->mag);
  np_w16(w, (uint16_t)e->x); np_w16(w, (uint16_t)e->y); np_w16(w, (uint16_t)e->z);
  np_w16(w, (uint16_t)e->vx); np_w16(w, (uint16_t)e->vy); np_w16(w, (uint16_t)e->vz);
  np_w16(w, (uint16_t)e->yaw); np_w16(w, (uint16_t)e->pitch);
  np_w16(w, (uint16_t)e->lean);
}
static void net_ev_encode(np_wr *w, const net_ev_t *e) {
  np_w32(w, e->id);
  np_w8(w, e->type); np_w8(w, e->src); np_w8(w, e->dst);
  np_w8(w, e->wpn);  np_w8(w, e->k);
  np_w16(w, (uint16_t)e->i);
  np_w16(w, (uint16_t)e->ax); np_w16(w, (uint16_t)e->ay); np_w16(w, (uint16_t)e->az);
  np_w16(w, (uint16_t)e->bx); np_w16(w, (uint16_t)e->by); np_w16(w, (uint16_t)e->bz);
  np_w16(w, (uint16_t)e->f);  np_w16(w, (uint16_t)e->g);  np_w16(w, (uint16_t)e->pen);
}
// Every field is read through the bounds-checked cursor; `type` is clamped to
// the defined range on USE (net_ev_to_live), never trusted as an array index
// here — a hostile server is still a decoder input like any other.
static int net_ev_decode(np_rd *r, net_ev_t *e) {
  e->id = np_r32(r);
  e->type = np_r8(r); e->src = np_r8(r); e->dst = np_r8(r);
  e->wpn = np_r8(r);  e->k = np_r8(r);
  e->i = (int16_t)np_r16(r);
  e->ax = (int16_t)np_r16(r); e->ay = (int16_t)np_r16(r); e->az = (int16_t)np_r16(r);
  e->bx = (int16_t)np_r16(r); e->by = (int16_t)np_r16(r); e->bz = (int16_t)np_r16(r);
  e->f = (int16_t)np_r16(r);  e->g = (int16_t)np_r16(r);  e->pen = (int16_t)np_r16(r);
  return !r->bad;
}
static void net_ev_from_live(net_ev_t *n, const event_t *e, uint32_t id) {
  n->id = id;
  n->type = (uint8_t)e->type;
  n->src = (uint8_t)(e->src < 0 ? 0xFF : e->src);
  n->dst = (uint8_t)(e->dst < 0 ? 0xFF : e->dst);
  n->wpn = (uint8_t)e->wpn; n->k = (uint8_t)e->k;
  n->i = (int16_t)(e->i < -32768 ? -32768 : e->i > 32767 ? 32767 : e->i);
  n->ax = net_q_pos(e->a.x); n->ay = net_q_pos(e->a.y); n->az = net_q_pos(e->a.z);
  n->bx = net_q_pos(e->b.x); n->by = net_q_pos(e->b.y); n->bz = net_q_pos(e->b.z);
  n->f = net_q_pos(e->f); n->g = net_q_wish(e->g); n->pen = net_q_pos(e->pen);
}
static void net_ev_to_live(event_t *e, const net_ev_t *n) {
  *e = (event_t){0};
  e->type = (ev_type_t)(n->type > EV_LAND ? EV_IMPACT : n->type);
  // src/dst/wpn are ARRAY INDICES in every consumer (g_player_anim via human_of_ent,
  // WPN_DEF, g_bots) — a hostile server's 254 walked straight into g_player_anim[234].
  e->src = n->src >= MAX_ENTS ? -1 : n->src;
  e->dst = n->dst >= MAX_ENTS ? -1 : n->dst;
  e->wpn = n->wpn >= WPN_COUNT ? 0 : n->wpn;
  e->k = n->k; e->i = n->i;
  e->a = (v3){{net_dq_pos(n->ax), net_dq_pos(n->ay), net_dq_pos(n->az)}};
  e->b = (v3){{net_dq_pos(n->bx), net_dq_pos(n->by), net_dq_pos(n->bz)}};
  e->f = net_dq_pos(n->f); e->g = net_dq_wish(n->g); e->pen = net_dq_pos(n->pen);
}
static int net_ent_decode(np_rd *r, net_ent_t *e) {
  e->id = np_r8(r); e->flags = np_r8(r);
  e->hp = np_r8(r);
  e->weapon = np_r8(r);
  e->spawn_seq = np_r8(r);
  e->mag = np_r8(r);
  e->x = (int16_t)np_r16(r); e->y = (int16_t)np_r16(r); e->z = (int16_t)np_r16(r);
  e->vx = (int16_t)np_r16(r); e->vy = (int16_t)np_r16(r); e->vz = (int16_t)np_r16(r);
  e->yaw = (int16_t)np_r16(r); e->pitch = (int16_t)np_r16(r);
  e->lean = (int16_t)np_r16(r);
  // Both fields are used as ARRAY INDICES by the caller (g_players/g_bots and
  // WPN_DEF/g_cfg.wp), so both are rejection criteria rather than masks: the
  // old `& 3` let a hostile server hand out weapon 2 and 3 against a
  // WPN_COUNT of 2, i.e. an out-of-bounds read per snapshot record.
  return !r->bad && e->id < MAX_ENTS && e->weapon < WPN_COUNT;
}

// FNV-1a over the generated solids: map_generate has NO transcendental
// calls, so box count/kind/order are bit-identical across platforms and only
// coordinates could differ by ULPs — the hash detects a divergent arena so the
// client REFUSES rather than playing a different map (EV_IMPACT.i indexes this
// exact list). 4-byte seed + 4-byte hash, fits any packet, no fragmentation.
static uint32_t net_world_hash(void) {
  // Cached per arena: the solids only change on map_generate (seed change),
  // but this used to rescan ~solids x 32 B per CLIENT per 60 Hz snapshot.
  static uint32_t cache, cache_seed = 0xffffffffu;
  static int cache_n = -1;
  if (cache_seed == g_map_seed && cache_n == g_num_solids) return cache;
  uint32_t h = 2166136261u;
  const unsigned char *p = (const unsigned char *)g_solids;
  size_t n = (size_t)g_num_solids * sizeof g_solids[0];
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  h = (h ^ (uint32_t)g_num_solids) * 16777619u;
  cache = h; cache_seed = g_map_seed; cache_n = g_num_solids;
  return h;
}

// Angle validation: absolute angles per tick without checks hand the client two things
// server authority is supposed to hold. gun_shoot builds the direction as yaw + rec_y
// and clamp(pitch + rec_p) — recoil is ADDED to the client angle, so a modified client
// that subtracts the expected recoil has a laser.
#define NET_MAX_TURN_PER_TICK 0.6f   // rad/tick (~4100 deg/s): far above human,
                                     // below a spinbot's instant flip
static void net_validate_angles(float *yaw, float *pitch, float prev_yaw) {
  // Bit tests, not isfinite(): under -ffast-math the library call folds away,
  // so the defence this line exists for was not actually compiled in.
  if (f_is_nonfinite(*yaw) || f_is_nonfinite(*pitch)) {
    *yaw = prev_yaw; *pitch = 0.0f; return;
  }
  *pitch = f_clamp(*pitch, -PITCH_MAX, PITCH_MAX);
  float dy = wrap_pi(*yaw - prev_yaw);
  if      (dy >  NET_MAX_TURN_PER_TICK) *yaw = wrap_pi(prev_yaw + NET_MAX_TURN_PER_TICK);
  else if (dy < -NET_MAX_TURN_PER_TICK) *yaw = wrap_pi(prev_yaw - NET_MAX_TURN_PER_TICK);
  else                                  *yaw = wrap_pi(*yaw);
}

// Stateless cookie: hash(secret, ip, port, time/30). No per-attempt state; the reply is
// SMALLER than the request, so the server is not an amplifier.
static uint64_t g_net_secret;
static uint32_t net_cookie(uint32_t ip, uint16_t port, uint32_t epoch) {
  uint64_t h = g_net_secret ^ 0xcbf29ce484222325ull;
  uint64_t v = (uint64_t)ip << 16 | port;
  h = (h ^ v) * 0x100000001b3ull;
  h = (h ^ epoch) * 0x100000001b3ull;
  return (uint32_t)(h ^ (h >> 32));
}
static int net_cookie_valid(uint32_t ip, uint16_t port,
                            uint32_t got, uint32_t now_epoch) {
  return got == net_cookie(ip, port, now_epoch) ||
         got == net_cookie(ip, port, now_epoch - 1);   // one epoch of slack
}

// Input bounds: the lag-switch aimbot is a client that stalls and then dumps 60
// backdated inputs in one frame, each shot rewound to a different time against victims
// who saw nothing move.
#define NET_IN_MAX      64           // hard cap on buffered input ticks
#define NET_IN_RATE_NUM 5            // <= 1.25x real-time ticks per second...
#define NET_IN_RATE_DEN 4            // ...i.e. 5/4
// Backlog above which consume trims oldest ticks (buttons carried). 12 ticks = 100 ms
// of jitter/frame-burst absorption.
#define NET_IN_BACKLOG  12
#define NET_QDEPTH_TARGET 2          // ~1 consumable + 1 tick of jitter margin
// Ceiling on the lag-comp rewind(rule 2): 30 ticks = 250 ms covers any
// playable ping; beyond it favour-the-shooter starts shooting the past.
#define NET_LAGCOMP_MAX 30
// Each input packet carries the last N ticks, oldest first.
#define NET_IN_REDUN    4
// (Live snapshot build/read moved below the sim structs — they read g_players/g_bots.)


#define PLAYER_HP            100
#define PLAYER_RESPAWN_TICKS 120  // 1 s death screen

// The player's eye WITHOUT the lean — the sim's own reference point.
static v3 player_eye_base(const player_t *p) {
  return (v3){{p->pos.x, p->pos.y + p->eye, p->pos.z}};
}

// Lateral world offset the lean has moved the upper body by, at lean amount l.
// Right-hand side of the aim yaw, so +1 is a right lean.
static v3 lean_offset(float yaw, float l) {
  float k = lean_eye_off(l);
  return (v3){{cosf(yaw) * k, 0, sinf(yaw) * k}};
}

// Where the player actually looks and shoots FROM. Peeking has to move the shot origin
// or the whole mechanic is a camera trick: the point of leaning out is that the round
// leaves from where the eye is, past the corner the body is still behind.
static v3 player_eye_pos(const player_t *p) {
  v3 e = player_eye_base(p);
  e = v3_add(e, lean_offset(p->yaw, p->lean));
  e.y -= lean_eye_drop(p->lean);
  return e;
}

static void player_spawn(player_t *p) {
  memset(p, 0, sizeof *p);
  p->pos = SPAWN_POS;
  p->prev_pos = p->pos;
  p->lean_clr = 1.0f;
  p->eye = p->prev_eye = EYE_STAND;
  p->grounded = p->prev_grounded = 1;  // spawn point is on the floor
  p->alive = 1;
  p->hp = PLAYER_HP;
  wpn_fill_mags(&p->wp);
}

// MOUSE LOOK, and the ADS multiplier lives HERE rather than at the three call sites —
// the online fork, the singleplayer path and the harness `look` command all have to
// feel the same, and the harness one is what a proof measures.
static float mouse_ads_look_scale(const player_t *p) {
  float ads = p->wp.ads_s;
  if (ads <= 0) return 1.0f;
  return 1.0f + (g_cfg.ads_sense - 1.0f) * ads;
}

static void player_look(player_t *p, float dx, float dy, float sense) {
  float k = sense * mouse_ads_look_scale(p) * SENSE_RAD_PER_COUNT;
  p->yaw += dx * k;
  p->pitch = f_clamp(p->pitch - dy * k, -PITCH_MAX, PITCH_MAX);
}

// Generic AABB body helpers — shared by the player and the bot so both
// collide with the world through the exact same arithmetic.
static int aabb_fits(v3 pos, float radius, float height) {
  v3 mn = {{pos.x - radius, pos.y, pos.z - radius}};
  v3 mx = {{pos.x + radius, pos.y + height, pos.z + radius}};
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    if (mx.x > s->min.x && mn.x < s->max.x &&
        mx.y > s->min.y && mn.y < s->max.y &&
        mx.z > s->min.z && mn.z < s->max.z) return 0;
  }
  return 1;
}

static int player_fits(const player_t *p, float height) {
  return aabb_fits(p->pos, PLAYER_RADIUS, height);
}

// The collision height of a live body: standing, or folded from the TOP while crouched
// (the box follows the smoothed eye down, never below the crouch floor).
static float player_col_height(const player_t *p) {
  if (!p->crouched) return HEIGHT_STAND;
  float h = p->eye + (HEIGHT_STAND - EYE_STAND);
  return h < HEIGHT_CROUCH ? HEIGHT_CROUCH : h;
}

static void move_axis(v3 *pos, v3 *vel, int *grounded, int axis,
                      float radius, float height, float dt) {
  pos->e[axis] += vel->e[axis] * dt;
  float ext[3] = {radius, height, radius};
  for (int i = 0; i < g_num_solids; i++) {
    const solid_t *s = &g_solids[i];
    v3 mn = {{pos->x - radius, pos->y, pos->z - radius}};
    v3 mx = {{pos->x + radius, pos->y + height, pos->z + radius}};
    if (!(mx.x > s->min.x && mn.x < s->max.x &&
          mx.y > s->min.y && mn.y < s->max.y &&
          mx.z > s->min.z && mn.z < s->max.z)) continue;
    if (vel->e[axis] > 0) {
      // ext[axis] is the height on y: moving up hits a box bottom with the full body
      // height (contrast the +0 on the downward line — the origin is at the feet).
      pos->e[axis] = s->min.e[axis] - ext[axis];
    } else if (vel->e[axis] < 0) {
      pos->e[axis] = s->max.e[axis] + (axis == 1 ? 0 : ext[axis]);
      if (axis == 1) *grounded = 1;
    }
    vel->e[axis] = 0;
  }
  if (axis == 1 && pos->y <= 0) {  // arena floor
    pos->y = 0;
    if (vel->y < 0) vel->y = 0;
    *grounded = 1;
  }
}

// One tick of the collision sweep for ANY entity.
static void ent_move(v3 *pos, v3 *vel, int *grounded, float height) {
  move_axis(pos, vel, grounded, 0, PLAYER_RADIUS, height, TICK_DT);
  move_axis(pos, vel, grounded, 2, PLAYER_RADIUS, height, TICK_DT);
  move_axis(pos, vel, grounded, 1, PLAYER_RADIUS, height, TICK_DT);
}

// Keep an entity inside the arena.
static void arena_clamp(v3 *pos) {
  pos->x = f_clamp(pos->x, -ARENA_HALF + PLAYER_RADIUS, ARENA_HALF - PLAYER_RADIUS);
  pos->z = f_clamp(pos->z, -ARENA_HALF + PLAYER_RADIUS, ARENA_HALF - PLAYER_RADIUS);
}
