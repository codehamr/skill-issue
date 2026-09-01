// =============================================================================
// core/config.inc  —  config.cfg vocabulary: action_t/ACTION_DEF, MV_DEF/
// WP_DEF/WP_DEFAULTS parameter tables, ent_next/ENT_FOREACH, SP_BOTS_DEF/
// SP_FRAG_DEF.
// Proof: cfg. One TU: included once by game.c, in definition order — moving a
// definition out is a semantic change.
// Contract: the enum IS the menu layout (ACTION_DEF is one designated-
// initialiser row per action: key, menu label, script token). Any array
// indexed by an entity id must be sized MAX_ENTS, never MAX_BOTS.
// =============================================================================
// ---------------------------------------------------------------------------
// Config — plain "key value" lines in config.cfg next to the executable.
// Created with defaults on first start; unknown keys are ignored with a note.
// ---------------------------------------------------------------------------

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
  MV_RUN, MV_CROUCH_SP, MV_ACCEL_MS, MV_STOP_MS, MV_JUMP_H,
  MV_AIR_CTL, MV_SLIDE_BOOST, MV_SLIDE_LEN, MV_FOV_KICK, MV_COUNT
} mv_param_t;

static const param_def_t MV_DEF[MV_COUNT] = {
  [MV_RUN]         = {"mv_run_speed",     "RUN SPEED",      6.5f,  2.0f,   14.0f,  0.1f},
  [MV_CROUCH_SP]   = {"mv_crouch_speed",  "CROUCH SPEED",   2.5f,  0.5f,    6.0f,  0.1f},
  [MV_ACCEL_MS]    = {"mv_accel_ms",      "ACCEL TIME",   200.0f, 30.0f, 1000.0f, 10.0f},
  [MV_STOP_MS]     = {"mv_stop_ms",       "STOP TIME",    200.0f, 30.0f, 1000.0f, 10.0f},
  [MV_JUMP_H]      = {"mv_jump_height",   "JUMP HEIGHT",    1.2f,  0.3f,    3.0f,  0.1f},
  [MV_AIR_CTL]     = {"mv_air_ctl_pct",   "AIR CONTROL",   30.0f,  0.0f,  100.0f,  5.0f},
  [MV_SLIDE_BOOST] = {"mv_slide_boost_x", "SLIDE BOOST",    2.5f,  1.0f,    5.0f,  0.1f},
  [MV_SLIDE_LEN]   = {"mv_slide_len",     "SLIDE LENGTH",  10.0f,  2.0f,   20.0f,  0.5f},
  // 0.0 BY CHOICE, not by an accident of multiplying by zero. Turn it up to 9.0.
  [MV_FOV_KICK]    = {"mv_fov_kick",      "FOV KICK",       0.0f,  0.0f,   20.0f,  0.5f},
};

// Weapon FEEL — one algorithm, per-weapon parameter sets, persisted as
// "<prefix>_<key>" (ar_rpm, sr_rpm, ...). Structural identity is WPN_DEF below.
typedef enum { WPN_AR, WPN_SR, WPN_COUNT } wpn_id_t;

// Third-person weapon cross-section scale. The pose solver needs this before
// render/gun_build.inc is included, and both the solver and renderer must read
// one value rather than silently maintaining parallel grip scales.
#define GUN_GIRTH_3P 1.05f

typedef enum {
  WP_RPM, WP_KICK_UP, WP_KICK_SIDE, WP_RECOVER_MS, WP_HIP_KICK, WP_MOVE_KICK,
  WP_HEAT, WP_ADS_MS, WP_ADS_ZOOM, WP_PUNCH, WP_SWAY, WP_COUNT
} wp_param_t;

// `def` is deliberately 0: defaults are PER-WEAPON, in WP_DEFAULTS, the one
// authority config_defaults and the DEV reset both read.
static const param_def_t WP_DEF[WP_COUNT] = {
  [WP_RPM]        = {"rpm",        "FIRE RATE",  0,  40.0f, 1000.0f, 10.0f},
  [WP_KICK_UP]    = {"kick_up",    "KICK UP",    0,   0.0f,    3.0f,  0.05f},
  [WP_KICK_SIDE]  = {"kick_side",  "KICK SIDE",  0,   0.0f,    1.0f,  0.05f},
  [WP_RECOVER_MS] = {"recover_ms", "RECOVERY",   0,  50.0f,  800.0f, 10.0f},
  // Accuracy is the visible barrel now: these tune the physical impulse, never a
  // second random cone. HIP RECOIL is a multiplier (1 = fully braced), MOVE RECOIL
  // adds at run speed, and HEAT/SHOT accumulates into later impulses of a burst.
  [WP_HIP_KICK]   = {"hip_recoil",  "HIP RECOIL", 0,   1.0f,    2.0f,  0.05f},
  [WP_MOVE_KICK]  = {"move_recoil", "MOVE RECOIL",0,   0.0f,    1.0f,  0.05f},
  [WP_HEAT]       = {"heat",        "HEAT/SHOT",  0,   0.0f,    0.4f,  0.01f},
  [WP_ADS_MS]     = {"ads_ms",     "ADS TIME",   0,  80.0f,  500.0f, 10.0f},
  [WP_ADS_ZOOM]   = {"ads_zoom",   "ADS ZOOM",   0,  40.0f,  100.0f,  1.0f},
  [WP_PUNCH]      = {"punch",      "VIEW PUNCH", 0,   0.0f,    3.0f,  0.1f},
  [WP_SWAY]       = {"sway",       "SWAY/BOB",   0,   0.0f,    3.0f,  0.1f},
};

// The AR remains controllable, but a sustained burst has real angular momentum:
// recovery cannot erase one impulse before the next arrives, movement and a loose hip
// shoulder amplify it, and heat makes the later rounds progressively harder to hold.
// The sniper is the violent single shot.
static const float WP_DEFAULTS[WPN_COUNT][WP_COUNT] = {
  [WPN_AR] = {750, 0.95f, 0.42f, 460, 1.45f, 0.48f, 0.15f, 180, 80, 1.00f, 0.7f},
  // ads_zoom 89 not 100: the scope's PiP pass owns the magnification; the base
  // FOV drop only sells the transition. rpm caps the semi-auto trigger interval
  // at 60 ms; it is not a fire rate (fire mode is structural, in WPN_DEF).
  [WPN_SR] = {1000, 1.80f, 0.20f, 400, 1.35f, 0.50f, 0.10f, 80, 89, 2.20f, 1.0f},
};

// Exact emitted signature of the pre-physical-bore AR defaults. Config loading uses
// the whole row to distinguish an untouched install from a user-authored tune: one
// changed field preserves the entire row.
static const float WP_AR_DEFAULTS_PRE_PHYSICAL[WP_COUNT] = {
  750, 0.30f, 0.15f, 200, 1.10f, 0.30f, 0.06f, 180, 80, 1.00f, 0.6f,
};

// The first physical-bore tune proved axis identity but not a useful full-auto wall
// trace: its spring reached a sub-degree equilibrium by round four. Recognize that
// exact emitted row as another untouched default so existing installs receive the
// visible climb without overwriting any hand-tuned profile.
static const float WP_AR_DEFAULTS_PRE_WALL_TRACE[WP_COUNT] = {
  750, 0.52f, 0.28f, 260, 1.30f, 0.48f, 0.10f, 180, 80, 1.00f, 0.7f,
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
  SFX_COUNT
};

static const wpn_def_t WPN_DEF[WPN_COUNT] = {
  [WPN_AR] = {"AR",     "ar", 30,  26, 2, 152, 124, 1, SFX_FIRE_AR,
              SFX_RELOAD_AR, 0.045f, 900.0f},
  [WPN_SR] = {"SNIPER", "sr",  5, 150, 2, 214, 214, 0, SFX_FIRE_SR,
              SFX_RELOAD_SR, 0.130f, 840.0f},
};

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
// Every array indexed by an ENTITY ID must be MAX_ENTS. g_bots[], BOT_NAMES[],
// SP_BOTS_DEF's range and tacstat's was_sl/was_air are BOT-indexed and must STAY
// MAX_BOTS — a blanket sweep is wrong.
#define MAX_BOTS   20
#define MAX_HUMANS 8
#define MAX_ENTS   (MAX_BOTS + MAX_HUMANS)
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
// 1 <= cap_public <= MAX_HUMANS, 0 <= bot_fill <= cap_public and
// 1 <= idle_s <= 120 are CHECKED at startup.
static struct {   // consumed by the Linux server half only
  int port, cap_bots, cap_public, skill, fraglimit, lobbies;
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
static const param_def_t SP_FRAG_DEF = {"sp_fraglimit", "FRAG LIMIT", 10, 1, 1000,     1};
