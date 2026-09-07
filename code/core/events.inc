// =============================================================================
// core/events.inc  —  ev_push, the sim's one-way channel to presentation.
// Proof: budget (ev_drops). One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: MAX_EVENTS bounds the frame event buffer; a drop is COUNTED, never silent.
// The terse harness printer omits bot fire/hits/reloads, footsteps and landings.
// This text policy does not suppress their event routing, audio or presentation.
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

// Handles share the human name length limit; presentation adds the bot marker.
static const char BOT_NAMES[][12] = {
  "V1per", "Rook", "Havoc", "Nova", "Onyx", "Sable", "Pivot", "Flux",
  "Razor", "Gh0st", "Bolt", "Crux", "Dr1ft", "Ember", "Fang", "Iris",
  "Jolt", "Kilo", "Lynx", "Mace", "ToastPilot", "MoonTaxi", "PixelOtter", "SirLagALot",
  "MossyBoots", "NeonNoodle", "PocketAce", "CozyChaos", "MangoByte", "LateToAim", "TinyStorm", "CloudSnack",
  "NightOwl", "TurboToast", "LowBattery", "SleepyFox", "SneakyPea", "ZeroChill", "CoffeeMain", "DustBunny",
  "NoScopeNed", "CrispyPing", "MoonMoth", "WaffleWolf", "OddSocks", "PanicRoll", "VelvetEcho", "Fr0stByte",
  "NoodleDuck", "RamenRush", "soft.reset", "AlmostPro", "LuckyMoth", "QuietRiot", "RustyHalo", "LemonLoot",
  "P1xelPunk", "DizzyKiwi", "MidnightFM", "CosmicPea", "RainCheck", "BlueStatic", "MildPanic", "FuzzyLogic",
  "SnackBreak", "EchoBloom", "AimlessJay", "OrbitKid", "MintyFresh", "SlowMoJo", "PeachFuzz", "CometClub",
  "NoriNinja", "CrateMate", "PaperTiger", "NeonFern", "FeralToast", "CocoaRush", "Daydream", "SideQuest",
  "MangoMoth", "PogoPanda", "KarmaDuck", "DustyPixel", "ColdPizza", "NoisyMoss", "ByteBard", "LoFi_Lynx",
  "TiltedTea", "AfkAstral", "JellyScope", "NimbleNewt", "SilverPea", "TurboTurnip", "OopsReload", "Tiny-Comet",
  "MarshMellow", "NightBus", "BananaPeel", "HushPuppy", "AimAtClouds", "RogueRamen", "KiwiKick", "PuddleJump",
  "LuckySock", "WobblyAim", "TeaAndTaps", "SaltyWaffle", "CedarGhost", "BlinkTwice", "SoupWizard", "MossMode",
  "PinkNoise", "SodaSprite", "PandaPause", "EmberWisp", "RooftopRay", "YawnPatrol", "SillyGoose", "MellowMint",
  "NightShift", "CherryRush", "SocksOnIce", "MapleMood", "DuskRunner", "HappyCamper", "LocalCrypt", "SkrrtSkrrt",
  "NekoNaps", "KeksKurier", "echo.exe", "low_key", "n0va", "WolkenKeks", "KaffeeKoma", "xXJinxXx",
  "Luna92", "MikaFPS", "itsMilo", "heyNico", "JustJules", "CasualKai", "xNova17", "Lenny404",
  "RinRin", "Tomo7", "LivLoops", "OlliePop", "benji.gg", "NiaPlays", "Samwise88", "ElioEcho",
  "Rooki3", "KuroKai", "zZenny", "V3xxed", "MochiMain", "JunoJumps", "TessTaps", "Wr3n",
};
static_assert(sizeof BOT_NAMES / sizeof BOT_NAMES[0] >= MAX_BOTS,
              "bot name pool must cover every slot without duplicates");

// Shuffle once per arena seed, independently of gameplay RNG. The server and
// clients (including late joiners) derive the same roster from the shared seed.
// Filling every slot keeps names stable across respawns and bot fill changes.
static const char *bot_name(int bi) {
  static uint32_t seeds[SRV_LOBBIES];
  static unsigned char ready[SRV_LOBBIES];
  static char names[SRV_LOBBIES][MAX_BOTS][sizeof BOT_NAMES[0] + sizeof " [bot]" - 1];
  if (!ready[g_lobby] || seeds[g_lobby] != g_map_seed) {
    int picks[sizeof BOT_NAMES / sizeof BOT_NAMES[0]];
    int count = (int)(sizeof picks / sizeof picks[0]);
    for (int i = 0; i < count; i++) picks[i] = i;
    uint64_t rng = ((uint64_t)g_map_seed << 32) | 0x626f7473u;
    (void)rng_next(&rng);
    for (int i = 0; i < MAX_BOTS; i++) {
      int j = i + (int)(rng_next(&rng) % (uint32_t)(count - i));
      int pick = picks[j];
      picks[j] = picks[i];
      snprintf(names[g_lobby][i], sizeof names[g_lobby][i], "%s [bot]", BOT_NAMES[pick]);
    }
    seeds[g_lobby] = g_map_seed;
    ready[g_lobby] = 1;
  }
  return names[g_lobby][bi];
}

// Three-range dispatch.
static const char *ent_name(int e) {
  if (e < 0) return "none";
  if (e == 0) return g_cfg.name;
  if (e <= MAX_BOTS) return bot_name(e - 1);
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

// EV_FIRE: a = impact/end point, b = shot dir, f = the round's travel FROM THE EYE
// (the shooter's, not the muzzle's — a − b·f reconstructs the eye, and the rifle's
// muzzle is a presentation offset off it), i = shot index, k = 0 none / 1 solid /
// 2 body / 3 head, g = ads blend (the tracer's muzzle offset collapses onto the view
// axis while aiming).
typedef struct {
  ev_type_t type; v3 a, b; float f, g, pen; int i, k, wpn, src, dst;
} event_t;

// Feedback uses event positions, never a later lookup of a hidden attacker.
// Double intermediates keep any finite binary32 positions safe to subtract/normalise.
static int event_direction(v3 from, v3 to, v3 *out) {
  for (int axis = 0; axis < 3; axis++)
    if (f_is_nonfinite(from.e[axis]) || f_is_nonfinite(to.e[axis])) return 0;
  double x = (double)from.x - to.x, y = (double)from.y - to.y, z = (double)from.z - to.z;
  double length = sqrt(x*x + y*y + z*z);
  if (length <= 0.0001) return 0;
  *out = (v3){{(float)(x/length), (float)(y/length), (float)(z/length)}};
  return 1;
}
static int event_actor_pair(const event_t *e) {
  return (unsigned)e->src < MAX_ENTS && (unsigned)e->dst < MAX_ENTS && e->src != e->dst;
}
static int event_local_outcome(const event_t *e) {
  int outcome = e->type == EV_KILL || ((e->type == EV_HIT || e->type == EV_HIT_HEAD) && e->i > 0);
  return outcome && (unsigned)g_local_ent < MAX_ENTS && e->src == g_local_ent && event_actor_pair(e);
}

// Per-frame admission is measured by the 20/50-bot budget gate, including
// twelve simulation ticks before the presentation drain. Overflow is counted.
#define MAX_EVENTS 128
static event_t g_events_all[SRV_LOBBIES][MAX_EVENTS];
#define g_events (g_events_all[g_lobby])
// Local admission time only; authoritative network events start their local feedback
// lifetime when accepted. This sidecar is not simulated or encoded on the wire.
static long g_event_tick_all[SRV_LOBBIES][MAX_EVENTS];
#define g_event_tick (g_event_tick_all[g_lobby])
// Personal audio eligibility is invalidated only by a known local life boundary.
static unsigned char g_event_feedback_all[SRV_LOBBIES][MAX_EVENTS];
#define g_event_feedback (g_event_feedback_all[g_lobby])
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
static void vfx_local_reset(void);           // local life feedback, not the match feed
static void vfx_match_reset(void);           // clears killfeed etc. on restart

// Harness mode.
#ifndef _WIN32
static int g_headless;
#endif

// A dropped event is absent from audio draining and server event delivery.
// Local VFX, including hitmarkers, still spawn at push time outside the buffer.
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
    g_event_tick[g_num_events] = g_tick;
    g_event_feedback[g_num_events] = 1;
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
