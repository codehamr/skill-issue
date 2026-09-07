// =============================================================================
// core/config.inc — actions, parameter tables and entity vocabulary.
// Proof: cfg, defaultsproof. Included once by game.c in definition order.
// Contract: ACTION_DEF supplies binding names and layout. Generated tuning
// tables come from the host schema/canonical snapshot, not runtime promotion.
// Any array indexed by entity id is sized MAX_ENTS, never MAX_BOTS.
// =============================================================================
// ---------------------------------------------------------------------------
// Config — plain "key value" lines in config.cfg next to the executable.
// Created with defaults on first start; unknown keys are ignored with a note.
// ---------------------------------------------------------------------------

#include <tuning.h>

// The KEYS tab lists these in enum order, so the enum IS the menu layout.
typedef enum {
  IN_FWD, IN_BACK, IN_LEFT, IN_RIGHT, IN_JUMP, IN_CROUCH, IN_LEAN_L, IN_LEAN_R,
  IN_FIRE, IN_ADS, IN_RELOAD, IN_SWAP, IN_SLOT1, IN_SLOT2, IN_SCORE, IN_VIEW,
  IN_COUNT
} action_t;

// ONE row per action. Designated initialisers make the enum the single ordering
// authority for config keys, menu labels and the script vocabulary at once.
typedef struct { const char *key, *dflt, *label, *script, *pdflt; } action_def_t;
// pdflt is the GAMEPAD default (second bind table, config key "pad<key>").
static const action_def_t ACTION_DEF[IN_COUNT] = {
  [IN_FWD]     = {"bind_forward",    "w",      "FORWARD",          "forward",    "none"},
  [IN_BACK]    = {"bind_back",       "s",      "BACK",             "back",       "none"},
  [IN_LEFT]    = {"bind_left",       "a",      "LEFT",             "left",       "none"},
  [IN_RIGHT]   = {"bind_right",      "d",      "RIGHT",            "right",      "none"},
  [IN_JUMP]    = {"bind_jump",       "space",  "JUMP",             "jump",       "pad_a"},
  [IN_CROUCH]  = {"bind_crouch",     "lshift", "CROUCH",           "crouch",     "pad_b"},
  [IN_LEAN_L]  = {"bind_lean_left",  "q",      "LEAN LEFT",        "lean_left",  "pad_lb"},
  [IN_LEAN_R]  = {"bind_lean_right", "e",      "LEAN RIGHT",       "lean_right", "pad_rb"},
  [IN_FIRE]    = {"bind_fire",       "mouse1", "FIRE",             "fire",       "pad_rt"},
  [IN_ADS]     = {"bind_ads",        "mouse2", "AIM",              "ads",        "pad_lt"},
  [IN_RELOAD]  = {"bind_reload",     "r",      "RELOAD",           "reload",     "pad_x"},
  [IN_SWAP]    = {"bind_swap",       "x",      "SWAP WPN",         "swap",       "pad_y"},
  [IN_SLOT1]   = {"bind_slot1",      "1",      "WEAPON 1 AR",      "slot1",      "pad_left"},
  [IN_SLOT2]   = {"bind_slot2",      "2",      "WEAPON 2 SNIPER",  "slot2",      "pad_right"},
  [IN_SCORE]   = {"bind_score",      "tab",    "SCOREBOARD",       "score",      "pad_select"},
  [IN_VIEW]    = {"bind_view",       "v",      "VIEW 1ST/3RD",     "view",       "pad_down"},
};

// Movement tunables, table-driven so config I/O and the UI sliders share one source.
typedef struct { const char *key, *label; float def, lo, hi, step; } param_def_t;

typedef enum {
  MV_RUN, MV_CROUCH_SP, MV_RESPONSE_MS, MV_JUMP_H,
  MV_AIR_CTL, MV_SLIDE_BOOST, MV_SLIDE_LEN, MV_COUNT
} mv_param_t;

static const param_def_t MV_DEF[MV_COUNT] = {
  TUNING_MV_ROWS
};
static_assert(MV_COUNT == TUNING_MV_COUNT);

// Weapon FEEL — one algorithm, per-weapon parameter sets, persisted as
// "<prefix>_<key>" (ar_rpm, sr_rpm, ...). Structural identity is WPN_DEF below.
typedef enum { WPN_AR, WPN_SR, WPN_COUNT } wpn_id_t;

// Third-person weapon cross-section scale. The pose solver needs this before
// render/gun_build.inc is included, and both the solver and renderer must read
// one value rather than silently maintaining parallel grip scales.
#define GUN_GIRTH_3P 1.05f

// Two rules are deliberately global: jumping/sliding costs the same accuracy with
// every gun, and every recoil offset returns at the same visible angular speed.
typedef enum { GP_ACTION_SPREAD, GP_RECENTER, GP_COUNT } gun_param_t;

static const param_def_t GP_DEF[GP_COUNT] = {
  TUNING_GP_ROWS
};
static_assert(GP_COUNT == TUNING_GP_COUNT);

typedef enum {
  WP_RPM, WP_RECOIL_UP, WP_RECOIL_SIDE, WP_HIP_SPREAD,
  WP_ADS_MS, WP_ADS_FOV, WP_COUNT
} wp_param_t;

// `def` is deliberately 0: defaults are PER-WEAPON, in WP_DEFAULTS, the one
// authority config_defaults and the DEV reset both read.
static const param_def_t WP_DEF[WP_COUNT] = {
  TUNING_WP_ROWS
};
static_assert(WP_COUNT == TUNING_WP_COUNT);

// Per-shot degrees, not hidden pattern amplitudes. Round zero flies on the current
// barrel axis; only after it leaves are these two values added to the next axis.
static const float WP_DEFAULTS[WPN_COUNT][WP_COUNT] = {
  TUNING_WP_DEFAULT_ROWS
};

// Structural weapon identity — not sliders: these change WHAT the weapon is.
typedef struct {
  const char *name, *prefix;
  int   mag, dmg, head_x, reload_ticks;
  // Tactical (round chambered, no bolt lock, no charging handle) vs empty. A
  // bolt gun works its action either way, so the sniper's two are identical.
  int   reload_tac_ticks;
  int   full_auto;      // 0 = one shot per trigger press (bolt/semi)
  int   sfx_fire;       // which synth buffer a shot triggers
  int   sfx_reload;     // ...and which reload; a bolt gun does not change a mag
  float sfx_hit_t;      // s to delay the hit/kill confirmation by: it must clear
                        // the shot, and the reports differ threefold
  float v_round;        // m/s. One consumer: the fly-by crack's lead over the
                        // report. The rounds themselves are hitscan. NO `pen` COLUMN —
                        // penetration is one rule for both guns (PEN_PATH).
} wpn_def_t;

// Sound ids live here so WPN_DEF can reference them; buffers are built in the
// audio section.
enum {
  SFX_FIRE_AR, SFX_FIRE_SR,          // the shot as the shooter hears it
  SFX_FAR_AR, SFX_FAR_SR,            // ...and as someone across the arena does
  SFX_HIT, SFX_HIT_HEAD,             // your hit confirmation
  SFX_KILL, SFX_KILL_HEAD,           // your kill confirmation
  SFX_HURT,                          // taking a round yourself
  SFX_IMPACT,                        // a bullet arriving in the world
  SFX_WHIZZ,                         // one going past your head
  SFX_STEP,                          // a boot landing
  SFX_DRY, SFX_RELOAD_AR, SFX_RELOAD_SR,
  // A NEW ID GOES AT THE END, ALWAYS: sfx_alloc reseeds the noise stream FROM
  // THE ID, so inserting one re-rolls every sound after it.
  SFX_FLESH,                         // a round arriving in a MAN
  SFX_RELOAD_AR_TAC,                 // ...and the AR's tactical reload
  SFX_THUNDER, SFX_RAIN, SFX_WIND,
  SFX_COUNT
};

static const wpn_def_t WPN_DEF[WPN_COUNT] = {
  [WPN_AR] = {"AR",     TUNING_WPN_AR_PREFIX, 30,  26, 2, 152, 124, 1, SFX_FIRE_AR,
              SFX_RELOAD_AR, 0.045f, 900.0f},
  [WPN_SR] = {"SNIPER", TUNING_WPN_SR_PREFIX,  5, 150, 2, 214, 214, 0, SFX_FIRE_SR,
              SFX_RELOAD_SR, 0.130f, 840.0f},
};

// Shared reload duration for simulation, animation and startup sound buffers.
static int wpn_reload_ticks(int cur, int tac) {
  return tac ? WPN_DEF[cur].reload_tac_ticks : WPN_DEF[cur].reload_ticks;
}

// The reload's two beats as fractions of reload_ticks: ONE timeline for both the audio
// events and the viewmodel's punch impulses.
static const float RELOAD_BEAT[WPN_COUNT][2] = {
  {0.560f, 0.960f},   // AR: the magazine SEATS, then the bolt goes home
  {0.560f, 0.955f},   // SR: the box seats, then the bolt is worked and locked
};

// The magazine's insert parameter, 0 at the start of the travel window and 1 at the
// seat. THE WINDOW END IS THE SEAT BEAT: 0.40 + 0.16 == RELOAD_BEAT[*][0].
#define GUN_MAG_U(prog) f_clamp(((prog) - 0.40f) / 0.16f, 0.0f, 1.0f)
// The tactical AR's buffer authors the seat as its LAST event, at 0.880 of
// reload_tac_ticks; the pose must read the same fraction.
#define RELOAD_SEAT_TAC 0.880f
#define GUN_MAG_U_TAC(prog) \
  f_clamp(((prog) - 0.40f) / (RELOAD_SEAT_TAC - 0.40f), 0.0f, 1.0f)

static const param_def_t VOL_DEF = {"volume", "VOLUME", 0.7f, 0.0f, 1.0f, 0.05f};
// The level the LIMITER sees, fixed for every volume setting, so the
// confirmation ladder and the duck step are measured against one bus.
#define VOL_REF 0.70f

// Singleplayer deathmatch settings (MATCH tab). Entity ids everywhere — the
// THREE-RANGE rule:
//   e == 0                      -> human 0 (the local player in SP)
//   1 <= e <= MAX_BOTS          -> bot e - 1
//   e >  MAX_BOTS               -> human e - MAX_BOTS
// Every array indexed by an ENTITY ID must be MAX_ENTS. g_bots[],
// SP_BOTS_DEF's range and tacstat's was_sl/was_air are BOT-indexed and must STAY
// MAX_BOTS — a blanket sweep is wrong.
#define MAX_BOTS   50
#define NET_BOTS_MAX 20
#define MAX_HUMANS 8
// Slot zero belongs to local play and is never assigned by server admission.
#define NET_PUBLIC_MAX (MAX_HUMANS - 1)
#define MAX_ENTS   (MAX_BOTS + MAX_HUMANS)
static_assert(MAX_ENTS < 255, "entity ids reserve byte 255 for no actor");
// g_rng_gun is declared above the roster constants and indexed by human slot, so
// this makes raising MAX_HUMANS a compile error instead of an overrun.
static_assert(sizeof g_rng_gun / sizeof g_rng_gun[0] == MAX_HUMANS,
              "g_rng_gun is PER HUMAN: size it MAX_HUMANS");

// Dedicated-server mode. DECLARED ABOVE THE PLATFORM SPLIT: g_headless sits inside
// #ifndef _WIN32 and copying that idiom here breaks MinGW in app_start's gate.
static int g_server;
// THE port, for every role: server listen and the client's mp_port.
#define NET_PORT_DEF 31415
// Server parameters come from ARGV, not the config: argv is the interface the
// deploy needs anyway, and it deletes the config-trap quartet (parser switch,
// emit list, unknown-key deletion, live slider for a bind-time value).
// 1 <= cap_public <= NET_PUBLIC_MAX, 0 <= bot_fill <= cap_public and
// 1 <= idle_s <= 120 are CHECKED at startup.
static struct {   // consumed by the Linux server half only
  int port, cap_bots, cap_public, skill, fraglimit, lobbies;
  int biome, time, weather; // environment choices, zero is random
  int bot_fill;        // bots top a populated arena up to this (net_bot_target)
  int idle_s;          // drop a silent conn after this many seconds (net_srv_poll)
  long ticks;          // 0 = run forever; N = exit after N ticks (test/RSS runs)
// DESIGNATED, not positional: a positional list once fell out of step with the field
// list and the server ran one tick and exited, forever, under the respawn wrapper.
} g_srv = {.port = NET_PORT_DEF, .cap_bots = 2, .cap_public = 6,
           .skill = 1, .fraglimit = 12, .lobbies = 1,
           .bot_fill = 2, .idle_s = 4, .ticks = 0};

// WHICH entity is "me", the one identity allowed to differ per process: 0 in
// singleplayer, the own id on a client, -1 on a dedicated server (where no branch gated
// on it may fire).
static int g_local_ent = 0;
// Networked-session flag (tentative definition; merges with the UI section's).
static int g_online;

// The human roster. Human 0 always exists; slots 1.. join and leave in networked
// sessions. OUTSIDE match_t on purpose: match_restart memsets match state.
static unsigned char g_humans_on_all[SRV_LOBBIES][MAX_HUMANS] =
    {[0 ... SRV_LOBBIES - 1] = {1}};
#define g_humans_on (g_humans_on_all[g_lobby])
static char g_human_name_all[SRV_LOBBIES][MAX_HUMANS][12];  // [0] mirrors g_cfg.name
#define g_human_name (g_human_name_all[g_lobby])
static int human_of_ent(int e) { return e == 0 ? 0 : e > MAX_BOTS ? e - MAX_BOTS : -1; }
static int ent_of_human(int h) { return h == 0 ? 0 : MAX_BOTS + h; }

// THE roster iterator: 0, 1..nbots, then the active high human slots. A loop
// using `e <= nbots` instead is a human who cannot be seen, hit or scored.
static int g_match_nbots_fwd(void);   // match_t is defined later
static int ent_next(int e) {
  if (e < 0) {
    if (g_humans_on[0]) return 0;   // off on a dedicated server until a join
    e = 0;                          // fall through to the bot range
  }
  if (e < g_match_nbots_fwd()) return e + 1;
  for (int h = e > MAX_BOTS ? e - MAX_BOTS + 1 : 1; h < MAX_HUMANS; h++)
    if (g_humans_on[h]) return MAX_BOTS + h;
  return -1;
}
// Seeded from ent_next(-1), not a literal 0: on a dedicated server slot 0 is
// empty, and starting there put a phantom ALIVE entity into every snapshot.
#define ENT_FOREACH(e) for (int e = ent_next(-1); e >= 0; e = ent_next(e))

// ONE definition each: these ranges used to be restated in the config parser,
// the MATCH tab and the harness's bots/fraglimit commands. 1 bot / 10 frags
// makes the opening match a duel; the difficulty default carries the challenge.
static const param_def_t SP_BOTS_DEF = {"sp_bots",      "BOTS",        1, 1, MAX_BOTS, 1};
// The final slider position means OFF; saved and active rules encode it as zero.
#define SP_FRAG_MAX 100
static const param_def_t SP_FRAG_DEF = {"sp_fraglimit", "FRAG LIMIT", 10, 5, SP_FRAG_MAX + 1, 1};
static int sp_frag_normalize(long n) {
  if (n == 0 || n > SP_FRAG_MAX) return 0;
  return n < (long)SP_FRAG_DEF.lo ? (int)SP_FRAG_DEF.lo : (int)n;
}

// Environment option indices are shared by config, match UI and the wire.
// Zero means random; explicit choices use their resolved enum plus one.
typedef enum { BIOME_QUARRY, BIOME_DUNES, BIOME_MARSH, BIOME_FOREST,
               BIOME_FROST, BIOME_COUNT } biome_kind_t;
typedef enum { TIME_DAY, TIME_SUNSET, TIME_NIGHT, TIME_COUNT } time_kind_t;
typedef enum { WEATHER_CLEAR, WEATHER_HAZE, WEATHER_CLOUDY, WEATHER_MIST,
               WEATHER_RAIN, WEATHER_STORM, WEATHER_SUNSHOWER, WEATHER_KIND_COUNT } weather_kind_t;
typedef struct { int biome, time, weather; } environment_t;
static const char *const ENV_BIOME_OPTS[] = {"RANDOM", "QUARRY", "DESERT", "MARSH", "FOREST", "SNOW"};
static const char *const ENV_TIME_OPTS[] = {"RANDOM", "DAY", "SUNSET", "NIGHT"};
static const char *const ENV_WEATHER_OPTS[] = {"RANDOM", "CLEAR", "LIGHT HAZE", "CLOUDY", "FOG", "RAIN", "STORM", "SUNSHOWER"};
static const char *const ENV_BIOME_KEYS[] = {"random", "quarry", "dunes", "marsh", "forest", "frost"};
static const char *const ENV_TIME_KEYS[] = {"random", "day", "sunset", "night"};
static const char *const ENV_WEATHER_KEYS[] = {"random", "clear", "haze", "cloudy", "fog", "rain", "storm", "sunshower"};
static int environment_option(const char *s, const char *const *keys, int count) {
  for (int i = 0; i <= count; i++) if (!strcmp(s, keys[i])) return i;
  return -1;
}
static uint16_t environment_pack(environment_t e) {
  return (uint16_t)(e.biome | e.time << 4 | e.weather << 8);
}
static int environment_unpack(uint16_t bits, environment_t *e) {
  environment_t v = {bits & 15, (bits >> 4) & 15, bits >> 8};
  if (v.biome > BIOME_COUNT || v.time > TIME_COUNT || v.weather > WEATHER_KIND_COUNT) return 0;
  *e = v;
  return 1;
}
