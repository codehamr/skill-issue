// =============================================================================
// core/events.inc  —  ev_push, the sim's one-way channel to presentation.
// Proof: budget (ev_drops). One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: MAX_EVENTS 128 is a fixed ring; a drop is COUNTED, never silent.
// Bot-vs-bot fire, footsteps, landings and bot reloads are silent by design.
// An event is also the one thing a client receives about a FOREIGN entity's
// actions, so it drives that body's presentation and VFX: ent_on_event applies
// recoil/reload/flash/flinch and vfx_on_event draws it. Predicted local fire is the
// deliberate split: gun_shoot starts its VM response and carry latch immediately;
// the later authoritative echo supplies VFX/3P recoil without duplicating the VM.
// =============================================================================
// ---------------------------------------------------------------------------
// Events — the sim's one-way channel to presentation. player_tick/bot_tick
// push; consumers pull: the harness prints a line per event at push time (the scripted-
// test contract), VFX spawn at push time (pure data), the shared `audio_frame_drain`
// empties the buffer once per frame for audio on every platform (the dedicated server
// drains it per TICK instead), then clears it.
// ---------------------------------------------------------------------------

// Deathmatch roster: short, all-caps (font glyph set), <=7 chars for tight
// scoreboard/killfeed columns.
static const char *BOT_NAMES[MAX_BOTS] = {
  "VIPER", "ROOK",  "HAVOC", "NOVA",  "ONYX",  "SABLE", "PIVOT", "FLUX",
  "RAZOR", "GHOST", "BOLT",  "CRUX",  "DRIFT", "EMBER", "FANG",  "IRIS",
  "JOLT",  "KILO",  "LYNX",  "MACE",
};

// Three-range dispatch.
static const char *ent_name(int e) {
  if (e < 0) return "none";
  if (e == 0) return g_cfg.name;
  if (e <= MAX_BOTS) return BOT_NAMES[e - 1];
  int h = e - MAX_BOTS;
  return h < MAX_HUMANS && g_human_name[h][0] ? g_human_name[h] : "REMOTE";
}

typedef enum {
  EV_FIRE, EV_HIT, EV_HIT_HEAD, EV_KILL, EV_IMPACT, EV_DRYFIRE, EV_RELOAD,
  EV_RELOAD_CANCEL,  // a swap abandoned a running reload; wpn = the weapon
  EV_RESPAWN, EV_END, EV_STEP, EV_LAND
} ev_type_t;

// AI hearing is simulation state, deliberately separate from g_events: events
// are drained by presentation and can never be an authoritative AI input.
typedef enum { BOT_NOISE_SHOT, BOT_NOISE_STEP, BOT_NOISE_COUNT } bot_noise_kind_t;
typedef struct {
  v3 pos;
  long tick;
  float radius;
  int src;
} bot_noise_t;

typedef struct bot_t bot_t;
static void bot_noise_emit(int src, bot_noise_kind_t kind, v3 pos, float radius);
static float bot_step_noise_radius(float speed, float crouch);
static void bot_hear_update(bot_t *b, int bi);
static float bot_eye(const bot_t *b);
static int los_clear(v3 o, v3 tp);
// Forward, not moved: bot_focus_invalidate is the chokepoint for "the target
// identity is changing" and has to release a committed flank, but the flank leg
// needs the cover/search helpers that are defined between the two.
static void bot_flank_release(bot_t *b);

// EV_FIRE: a = impact/end point, b = shot dir, f = hit distance, i = shot index, k = 0
// none / 1 solid / 2 body / 3 head, g = ads blend (the tracer's muzzle offset collapses
// onto the view axis while aiming).
typedef struct {
  ev_type_t type; v3 a, b; float f, g, pen; int i, k, wpn, src, dst;
} event_t;

// 128 against a measured ev_peak of ~10 at 21 entities (20 bots hard); the
// MAX_ENTS growth to 28 entities moves that peak proportionally at most —
// re-measured via `budget`, still an order of magnitude of headroom.
#define MAX_EVENTS 128
static event_t g_events_all[SRV_LOBBIES][MAX_EVENTS];
#define g_events (g_events_all[g_lobby])
static int     g_num_events_all[SRV_LOBBIES];
#define g_num_events (g_num_events_all[g_lobby])
static long    g_tick_all[SRV_LOBBIES];  // sim tick, advanced in player_tick
#define g_tick (g_tick_all[g_lobby])
static int     g_score_held;  // scoreboard key level state, sim -> HUD

// THE BODY ANSWERS ITS OWN EVENT — defined with the sim's combat module, which owns
// every action that pushes one. See ent_on_event for why an impulse rides this channel
// rather than the snapshot.
static void ent_on_event(const event_t *e);
static void vfx_on_event(const event_t *e);  // defined with the renderer
static void vfx_tick(void);                  // ditto — called every sim tick
static void vfx_match_reset(void);           // clears killfeed etc. on restart

// Harness mode.
#ifndef _WIN32
static int g_headless;
#endif

// A DROPPED EVENT IS A MISSING SOUND: VFX (hitmarkers included) spawn at push time
// outside the ring, so what a drop loses is the frame drain's AUDIO for that event.
static long g_ev_drops_all[SRV_LOBBIES];
#define g_ev_drops (g_ev_drops_all[g_lobby])
static int  g_ev_peak_all[SRV_LOBBIES];
#define g_ev_peak (g_ev_peak_all[g_lobby])

// While the client PREDICTS/REPLAYS its own motion, events, VFX, audio and damage must
// not fire — they belong to the authoritative server pass. Rollback is distinguished
// from the first forward prediction because only the latter may create a new local
// presentation edge; replay recomputes derived weapon/VM state from recorded inputs.
static int g_predicting;
static int g_prediction_replay;

static void ev_push(event_t e) {
  if (g_predicting) return;   // a replayed shot must not re-spawn its VFX/audio
  if (g_num_events < MAX_EVENTS) {
    g_events[g_num_events++] = e;
    if (g_num_events > g_ev_peak) g_ev_peak = g_num_events;
  } else {
    g_ev_drops++;
  }
  // THE BODY FIRST, THEN WHAT IS DRAWN OF IT: the shot's kick, the reload's clock and
  // the hit's flinch are state the VFX layer reads back off the entity two lines later.
  ent_on_event(&e);
  vfx_on_event(&e);
#ifndef _WIN32
  if (!g_headless) return;
  static const char *KIND[4] = {"none", "solid", "bot", "head"};
  switch (e.type) {
  case EV_FIRE:
    if (e.src == g_local_ent) {  // player shots only: bot fire would flood every trace
      // `pen` is the PATH LENGTH the round spent inside material, and it is the only
      // way a script can tell a wallbang from an ordinary hit — the two are identical
      // in every other field.
      printf("bang t=%ld n=%d dir=(%.3f %.3f %.3f) hit=%s at=(%.2f %.2f %.2f) d=%.2f",
             g_tick, e.i, (double)e.b.x, (double)e.b.y, (double)e.b.z,
             // k is wire-fed on a netclient (net_ev_to_live copies it raw) —
             // clamp here at the one consumer, not in the decoder where k's
             // meaning is per-type (EV_HIT hp, EV_END winner).
             e.k >= 0 && e.k < 4 ? KIND[e.k] : "?",
             (double)e.a.x, (double)e.a.y, (double)e.a.z,
             (double)(e.k ? e.f : -1.0f));
      if (e.pen > 0.0f) printf(" pen=%.3f", (double)e.pen);
      putchar('\n');
    }
    break;
  case EV_HIT:
  case EV_HIT_HEAD:
    if (e.src == g_local_ent)       // the player's own hits: unchanged contract
      printf("hit t=%ld part=%s d=%d hp=%d\n", g_tick,
             e.type == EV_HIT_HEAD ? "head" : "body", e.i, e.k);
    else if (e.dst == g_local_ent)  // the player taking damage
      printf("phit t=%ld part=%s d=%d hp=%d by=%s\n", g_tick,
             e.type == EV_HIT_HEAD ? "head" : "body", e.i, e.k, ent_name(e.src));
    break;                // bot-vs-bot hits stay silent
  case EV_KILL:
    printf("kill t=%ld by=%s victim=%s hs=%d\n", g_tick,
           ent_name(e.src), ent_name(e.dst), e.k);
    break;
  // Gated on the pusher's identity, not on push-side discipline: the printer
  // contract is "the PLAYER's dryfire/reload_cancel", and once bots or remote
  // humans push these with .src set, an ungated line lands mid-trace in every
  // gunplay artefact of the repo.
  case EV_DRYFIRE: if (e.src == g_local_ent) printf("dryfire t=%ld\n", g_tick); break;
  // PLAYER RELOADS ONLY, like `bang`.
  case EV_RELOAD:  if (e.src == g_local_ent) printf("reload t=%ld\n", g_tick); break;
  case EV_RELOAD_CANCEL:
    if (e.src == g_local_ent) printf("reload_cancel t=%ld\n", g_tick);
    break;
  case EV_RESPAWN: printf("respawn t=%ld ent=%s\n", g_tick, ent_name(e.dst)); break;
  case EV_END:     printf("matchover t=%ld winner=%s\n", g_tick, ent_name(e.k)); break;
  case EV_IMPACT:  break;  // visual only
  // Footsteps and landings are SILENT in the trace by design: 21 entities at
  // ~4 steps/s would bury every gunplay line the contract is written against.
  case EV_STEP:
  case EV_LAND:    break;
  }
#endif
}
