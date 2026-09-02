// =============================================================================
// core/pad.inc  —  pad vocabulary and low-level state (PAD_BTN_DEF, PAD_DEF,
// pad_apply_standard/xinput/hid, pad_deadzone, pad_curve_apply) plus
// config_t: config.cfg load/save/parse (config_load_or_create, config_emit,
// parse_float/long/int, padbind_key, exe_dir).
// Proof: padbackend, padcurve. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Note: pad_look and the aim-assist friction (pad_assist_friction) are NOT
// here — they live in core/frame.inc; see task-4-report.md.
// =============================================================================
// ---------------------------------------------------------------------------
// Gamepad — shared vocabulary and processing. The backends (evdev on Linux,
// XInput + DirectInput8 on Windows) only FILL g_pad; everything that interprets
// it — deadzone, curve, look rates, button edges, menu nav — lives here, for the
// same reason the frame loop does.
// ---------------------------------------------------------------------------

// Developer menu gate, HARD half: 0 removes the DEV tab from the build entirely,
// overridable from the build line (-DDEV_MENU=0).
#ifndef DEV_MENU
#define DEV_MENU 1
#endif

typedef enum {
  PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT,
  PB_SELECT, PB_START, PB_L3, PB_R3, PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT,
  // Back paddles (8BitDo Ultimate family). Appended, never inserted: these extend
  // the KEY_PAD0 key space, so an insertion renumbers every existing pad bind.
  PB_P1, PB_P2, PB_P3, PB_P4,
  PB_COUNT
} pad_btn_t;

// tok = config vocabulary, label = what the menu prints.
typedef struct { const char *tok, *label; } pad_btn_def_t;
static const pad_btn_def_t PAD_BTN_DEF[PB_COUNT] = {
  [PB_A]      = {"pad_a",      "A"},        [PB_B]     = {"pad_b",     "B"},
  [PB_X]      = {"pad_x",      "X"},        [PB_Y]     = {"pad_y",     "Y"},
  [PB_LB]     = {"pad_lb",     "LB"},       [PB_RB]    = {"pad_rb",    "RB"},
  [PB_LT]     = {"pad_lt",     "LT"},       [PB_RT]    = {"pad_rt",    "RT"},
  [PB_SELECT] = {"pad_select", "VIEW BTN"}, [PB_START] = {"pad_start", "MENU BTN"},
  [PB_L3]     = {"pad_l3",     "L3"},       [PB_R3]    = {"pad_r3",    "R3"},
  [PB_UP]     = {"pad_up",     "DPAD UP"},  [PB_DOWN]  = {"pad_down",  "DPAD DN"},
  [PB_LEFT]   = {"pad_left",   "DPAD L"},   [PB_RIGHT] = {"pad_right", "DPAD R"},
  [PB_P1]     = {"pad_p1",     "PADDLE 1"}, [PB_P2]    = {"pad_p2",    "PADDLE 2"},
  [PB_P3]     = {"pad_p3",     "PADDLE 3"}, [PB_P4]    = {"pad_p4",    "PADDLE 4"},
};

// Pad feel, table-driven like MV_DEF so config I/O and the GAMEPAD tab share one
// source. Deadzones are PER STICK: a worn left stick must not deaden a good right.
typedef enum { PAD_DZ_L, PAD_DZ_R, PAD_SENS, PAD_ADS_SENS,
               PAD_AS_FRIC, PAD_AS_BUBBLE, PAD_AS_DIST,
               PAD_COUNT } pad_param_t;
// ONE sensitivity, on the console 1-10 notch scale: a notch is 60 deg/s of yaw and
// 40 of pitch, the genre's 2:3 ratio baked in rather than two sliders to co-tune.
#define PAD_SENS_DEG_X 60.0f
#define PAD_SENS_DEG_Y 40.0f
static const param_def_t PAD_DEF[PAD_COUNT] = {
  // Essentially OFF by default: a 0.12 dead ring is 12% of the travel spent doing
  // nothing.
  [PAD_DZ_L]    = {"pad_dz_l",     "DEADZONE L",   0.00f,  0.00f,   0.45f,  0.01f},
  [PAD_DZ_R]    = {"pad_dz_r",     "DEADZONE R",   0.04f,  0.00f,   0.45f,  0.01f},
  [PAD_SENS]    = {"pad_sens",     "SENSITIVITY",  4.0f,   1.0f,   10.0f,   1.0f},
  // Multiplier on the FOV-derived ADS slowdown: 1.00 = the pure zoom ratio. A
  // factor, so it composes with any weapon's zoom instead of fighting it.
  [PAD_ADS_SENS] = {"pad_ads_sens", "ADS SENS",     0.70f,  0.10f,   1.00f,  0.05f},
  // Friction-only aim assist (DEV > ASSIST tab).
  [PAD_AS_FRIC]   = {"pad_as_friction", "FRICTION", 1.00f, 0.05f, 1.00f, 0.05f},
  [PAD_AS_BUBBLE] = {"pad_as_bubble",   "BUBBLE",   2.5f,  1.0f,  15.0f, 0.5f},
  [PAD_AS_DIST]   = {"pad_as_dist",     "DIST",     80.0f, 5.0f,  80.0f, 1.0f},
};
static const char *const PAD_CURVE_OPTS[3] = {"LINEAR", "STANDARD", "DYNAMIC"};

// Raw controller state, written by the platform pump. Sticks -1..1, +x right and +y UP
// (the evdev pump negates its down-positive axes).
typedef struct {
  int   connected;
  float lx, ly, rx, ry;   // sticks
  float lt, rt;           // triggers
  int   btn[PB_COUNT];
} pad_state_t;
static pad_state_t g_pad;

// Past this, a stick or a trigger counts as HELD rather than as rest drift — the one
// number every hand-over rule asks about, since "which pad is the player holding" has
// no other answer.
#define PAD_WAKE_DEFL 0.5f

// IS ANYONE TOUCHING THIS PAD. Levels, not edges, which is all a hand-over needs: it
// only ever asks "is the pad I am reading idle while another one is not".
// [[maybe_unused]]: the only caller is win_pad_pump, so on Linux this is a
// -Wunused-function, i.e. a broken release build. Same idiom as upd_status_line.
[[maybe_unused]] static int pad_touched(const pad_state_t *p) {
  for (int b = 0; b < PB_COUNT; b++) if (p->btn[b]) return 1;
  return p->lt > PAD_WAKE_DEFL || p->rt > PAD_WAKE_DEFL ||
         fabsf(p->lx) > PAD_WAKE_DEFL || fabsf(p->ly) > PAD_WAKE_DEFL ||
         fabsf(p->rx) > PAD_WAKE_DEFL || fabsf(p->ry) > PAD_WAKE_DEFL;
}

// Portable GameInput-shaped sample. Platform adapters write this one vocabulary;
// the mapper below is the only place native button masks become pad_state_t.
typedef struct {
  int connected;
  float lx, ly, rx, ry;
  float lt, rt;
  uint32_t buttons;
} pad_standard_state_t;

enum {
  PAD_STANDARD_MENU  = 0x0001u, PAD_STANDARD_VIEW  = 0x0002u,
  PAD_STANDARD_A     = 0x0004u, PAD_STANDARD_B     = 0x0008u,
  PAD_STANDARD_X     = 0x0010u, PAD_STANDARD_Y     = 0x0020u,
  PAD_STANDARD_UP    = 0x0040u, PAD_STANDARD_DOWN  = 0x0080u,
  PAD_STANDARD_LEFT  = 0x0100u, PAD_STANDARD_RIGHT = 0x0200u,
  PAD_STANDARD_LB    = 0x0400u, PAD_STANDARD_RB    = 0x0800u,
  PAD_STANDARD_L3    = 0x1000u, PAD_STANDARD_R3    = 0x2000u,
  PAD_STANDARD_P1    = 0x4000u, PAD_STANDARD_P2    = 0x8000u,
  PAD_STANDARD_P3    = 0x10000u, PAD_STANDARD_P4   = 0x20000u,
};

static void pad_apply_standard(pad_state_t *dst, const pad_standard_state_t *src) {
  static const struct { uint32_t bit; pad_btn_t button; } button_map[] = {
    {PAD_STANDARD_MENU, PB_START}, {PAD_STANDARD_VIEW, PB_SELECT},
    {PAD_STANDARD_A, PB_A},        {PAD_STANDARD_B, PB_B},
    {PAD_STANDARD_X, PB_X},        {PAD_STANDARD_Y, PB_Y},
    {PAD_STANDARD_UP, PB_UP},      {PAD_STANDARD_DOWN, PB_DOWN},
    {PAD_STANDARD_LEFT, PB_LEFT},  {PAD_STANDARD_RIGHT, PB_RIGHT},
    {PAD_STANDARD_LB, PB_LB},      {PAD_STANDARD_RB, PB_RB},
    {PAD_STANDARD_L3, PB_L3},      {PAD_STANDARD_R3, PB_R3},
    {PAD_STANDARD_P1, PB_P1},      {PAD_STANDARD_P2, PB_P2},
    {PAD_STANDARD_P3, PB_P3},      {PAD_STANDARD_P4, PB_P4},
  };
  memset(dst, 0, sizeof *dst);
  if (!src->connected) return;
  dst->connected = 1;
  dst->lx = f_clamp(src->lx, -1.0f, 1.0f);
  dst->ly = f_clamp(src->ly, -1.0f, 1.0f);
  dst->rx = f_clamp(src->rx, -1.0f, 1.0f);
  dst->ry = f_clamp(src->ry, -1.0f, 1.0f);
  dst->lt = f_clamp(src->lt, 0.0f, 1.0f);
  dst->rt = f_clamp(src->rt, 0.0f, 1.0f);
  for (size_t i = 0; i < sizeof button_map / sizeof button_map[0]; i++)
    dst->btn[button_map[i].button] = (src->buttons & button_map[i].bit) != 0;
}

// The XInput ABI packet is separate so its mask/range conversion can be tested by
// the Linux harness instead of living untested in a Windows-only poll loop.
typedef struct {
  uint16_t buttons;
  uint8_t lt, rt;
  int16_t lx, ly, rx, ry;
} pad_xinput_raw_t;

static void pad_apply_xinput(pad_state_t *dst, const pad_xinput_raw_t *raw) {
  uint32_t buttons = 0;
  const uint16_t b = raw->buttons;
  if (b & 0x0001u) buttons |= PAD_STANDARD_UP;
  if (b & 0x0002u) buttons |= PAD_STANDARD_DOWN;
  if (b & 0x0004u) buttons |= PAD_STANDARD_LEFT;
  if (b & 0x0008u) buttons |= PAD_STANDARD_RIGHT;
  if (b & 0x0010u) buttons |= PAD_STANDARD_MENU;
  if (b & 0x0020u) buttons |= PAD_STANDARD_VIEW;
  if (b & 0x0040u) buttons |= PAD_STANDARD_L3;
  if (b & 0x0080u) buttons |= PAD_STANDARD_R3;
  if (b & 0x0100u) buttons |= PAD_STANDARD_LB;
  if (b & 0x0200u) buttons |= PAD_STANDARD_RB;
  if (b & 0x1000u) buttons |= PAD_STANDARD_A;
  if (b & 0x2000u) buttons |= PAD_STANDARD_B;
  if (b & 0x4000u) buttons |= PAD_STANDARD_X;
  if (b & 0x8000u) buttons |= PAD_STANDARD_Y;
  pad_apply_standard(dst, &(pad_standard_state_t){
    .connected = 1,
    .lx = (float)raw->lx / 32767.0f, .ly = (float)raw->ly / 32767.0f,
    .rx = (float)raw->rx / 32767.0f, .ry = (float)raw->ry / 32767.0f,
    .lt = (float)raw->lt / 255.0f, .rt = (float)raw->rt / 255.0f,
    .buttons = buttons,
  });
}

// HID/DirectInput-shaped sample.
enum {  // Generic Desktop usage order == the DIJOYSTATE member order
  PH_X, PH_Y, PH_Z, PH_RX, PH_RY, PH_RZ, PH_S0, PH_S1, PH_AXES
};
typedef struct {
  int      connected;
  uint32_t axes;          // presence bitmask, 1 << PH_*
  float    ax[PH_AXES];   // -1..1 as reported (+y DOWN, HID's convention)
  int      pov;           // hundredths of a degree CW from north, -1 = centered
  uint32_t buttons;       // bit i = raw HID button index i
  uint16_t vid;           // USB vendor id, 0 = unknown
} pad_hid_raw_t;

typedef struct {
  // rgbButtons index of A B X Y LB RB SELECT START L3 R3 P1 P2 P3 P4 (-1 =
  // absent), in PAD_HID_BTN_BITS order. Positional semantics (A = south).
  int8_t  btn[14];
  int8_t  lt, rt;         // digital trigger bits, drive lt/rt when pressed
  uint8_t right_rxry;     // both axis pairs present: right stick is Rx/Ry
  uint8_t trig_digital;   // never read analog trigger axes on this vendor
} pad_hid_map_t;
static const uint32_t PAD_HID_BTN_BITS[14] = {
  PAD_STANDARD_A,  PAD_STANDARD_B,  PAD_STANDARD_X,    PAD_STANDARD_Y,
  PAD_STANDARD_LB, PAD_STANDARD_RB, PAD_STANDARD_VIEW, PAD_STANDARD_MENU,
  PAD_STANDARD_L3, PAD_STANDARD_R3, PAD_STANDARD_P1,   PAD_STANDARD_P2,
  PAD_STANDARD_P3, PAD_STANDARD_P4,
};

static uint32_t pad_hid_pov_bits(int pov) {
  if (pov < 0) return 0;   // centered
  static const uint32_t dir[8] = {
    PAD_STANDARD_UP,   PAD_STANDARD_UP | PAD_STANDARD_RIGHT,
    PAD_STANDARD_RIGHT, PAD_STANDARD_DOWN | PAD_STANDARD_RIGHT,
    PAD_STANDARD_DOWN, PAD_STANDARD_DOWN | PAD_STANDARD_LEFT,
    PAD_STANDARD_LEFT, PAD_STANDARD_UP | PAD_STANDARD_LEFT,
  };
  return dir[((pov + 2250) % 36000) / 4500];
}

static void pad_apply_hid(pad_state_t *dst, const pad_hid_raw_t *src) {
  // The three vendor rows are the measured exceptions; anything unknown gets the
  // sequential Xbox order, SDL's own guess for a pad with no db entry.
  static const pad_hid_map_t MAP_XBOX =   // generic fallback
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, -1, -1, -1, -1},           -1, -1, 0, 0};
  // 8BitDo D-input mode: Android numbering with the paddles at 17/16/2/5 (the 2C
  // variants carry only the 2/5 pair, which then reads as PADDLE 3/4).
  static const pad_hid_map_t MAP_8BITDO =
    {{0, 1, 3, 4, 6, 7, 10, 11, 13, 14, 17, 16, 2, 5},          8,  9, 1, 1};
  static const pad_hid_map_t MAP_SONY =   // DualShock 4 / DualSense
    {{1, 2, 0, 3, 4, 5, 8, 9, 10, 11, -1, -1, -1, -1},          6,  7, 0, 0};
  static const pad_hid_map_t MAP_SWITCH = // Pro Controller (south IS its B)
    {{0, 1, 2, 3, 4, 5, 8, 9, 10, 11, -1, -1, -1, -1},          6,  7, 1, 1};
  if (!src->connected) { memset(dst, 0, sizeof *dst); return; }
  const pad_hid_map_t *m = src->vid == 0x2DC8 ? &MAP_8BITDO
                         : src->vid == 0x054C ? &MAP_SONY
                         : src->vid == 0x057E ? &MAP_SWITCH : &MAP_XBOX;
  const uint32_t ax = src->axes;
  const int zrz  = (ax & 1u << PH_Z)  && (ax & 1u << PH_RZ);
  const int rxry = (ax & 1u << PH_RX) && (ax & 1u << PH_RY);
  float rx = 0, ry = 0;
  float lt = -1, rt = -1;                    // < 0 = no analog source found
  if (rxry && (m->right_rxry || !zrz)) {
    rx = src->ax[PH_RX];
    ry = src->ax[PH_RY];
      // Xbox-through-DInput folds both triggers onto one Z axis: LT +, RT -,
      // both pressed = centre. Splitting on sign is the standard reading.
    if (!m->trig_digital && (ax & 1u << PH_Z) && !(ax & 1u << PH_RZ)) {
      lt = f_clamp(src->ax[PH_Z], 0.0f, 1.0f);
      rt = f_clamp(-src->ax[PH_Z], 0.0f, 1.0f);
    }
  } else if (zrz) {
    rx = src->ax[PH_Z];
    ry = src->ax[PH_RZ];
    if (!m->trig_digital && rxry) {          // DualShock shape: analog rest -1
      lt = (src->ax[PH_RX] + 1.0f) * 0.5f;
      rt = (src->ax[PH_RY] + 1.0f) * 0.5f;
    }
  }
  if (lt < 0 && !m->trig_digital && (ax & 1u << PH_S0) && (ax & 1u << PH_S1)) {
    lt = (src->ax[PH_S0] + 1.0f) * 0.5f;     // slider triggers, rest -1
    rt = (src->ax[PH_S1] + 1.0f) * 0.5f;
  }
  if (lt < 0) lt = rt = 0;
  // Digital trigger bits ride on top of (or stand in for) the analog value;
  // the max keeps a vendor reporting both from flickering at the threshold.
  if (m->lt >= 0 && src->buttons >> m->lt & 1u) lt = 1.0f;
  if (m->rt >= 0 && src->buttons >> m->rt & 1u) rt = 1.0f;
  uint32_t buttons = pad_hid_pov_bits(src->pov);
  for (int i = 0; i < 14; i++)
    if (m->btn[i] >= 0 && src->buttons >> m->btn[i] & 1u)
      buttons |= PAD_HID_BTN_BITS[i];
  pad_apply_standard(dst, &(pad_standard_state_t){
    .connected = 1,
    .lx = src->ax[PH_X], .ly = -src->ax[PH_Y],   // HID +y is DOWN, ours is UP
    .rx = rx, .ry = -ry,
    .lt = lt, .rt = rt, .buttons = buttons,
  });
}

// A gamepad NODE exists but the game may not open it: the Linux scan sets this when a
// node whose capabilities say "gamepad" refuses open()/grab, which is what Steam
// Input's device hiding looks like.
static int g_pad_denied;

// The UI owns this flag; it is DEFINED up here because the pad move gate in
// player_tick needs it — analog move bypasses the held[] array that frame_input_keys
// already gates on the menu. A repeat in the UI section is legal C (a second tentative
// definition) and therefore SILENT, so the one-declaration discipline is manual.
static int g_menu_open;
// The boot screen ("HOME"): a live showcase behind a tile rail.
static int g_home;
// The boot screen's own bot count — three is a firefight, six a crowd, and
// sp_bots is the player's MATCH setting. Read it through sp_bot_count() only.
#define HOME_BOTS 3
static int sp_bot_count(void);

// Radial SCALED deadzone: direction preserved, magnitude remapped to start at zero AT
// the edge and reach 1 at full deflection.
#define PAD_OUTER 1.0f
static float pad_deadzone(float x, float y, float dz, float *ox, float *oy) {
  float m = sqrtf(x * x + y * y);
  if (m <= dz) { *ox = *oy = 0; return 0; }
  float n = (m - dz) / (PAD_OUTER - dz);
  if (n > 1) n = 1;
  *ox = x / m * n;
  *oy = y / m * n;
  return n;
}

// Aim response curve on the deflection MAGNITUDE, never per axis (that bends the aim
// toward the diagonals).
static float pad_curve_apply(float m, int curve) {
  m = f_clamp(m, 0.0f, 1.0f);
  switch (curve) {
    case 1:  return m * m;
    case 2:  return 0.5f - sinf(asinf(1.0f - 2.0f * m) * (1.0f / 3.0f));
    default: return m;
  }
}

typedef struct {
  float sense;               // mouse sensitivity, degrees per count at 0.022 base
  // ADS SENS for the MOUSE, deliberately not the pad's construction: a thumb
  // pushes a RATE (so the pad anchors on the zoom ratio), a mouse pushes an ANGLE.
  // Plain multiplier on the same ADS ramp; 1.00 is the identity everywhere.
  float ads_sense;           // mouse ADS sensitivity multiplier, 1 = unchanged
  float fov;                 // horizontal FOV in degrees
  // How much of the recoil offset the CAMERA follows, 0..1. At 1 the reticle is
  // always the next round's line and the climb is fought with the mouse; below it
  // the crosshair lies about the barrel by the remainder. No menu row on purpose:
  // it is the aim contract, not a taste setting, and the sway follow is separate.
  float recoil_view;
  int   vsync;
  int   fps_cap;             // frame limiter, 0 = off
  int   show_fps;
  int   msaa;                // MSAA samples: 0 (off), 2, 4 or 8
  int   ssao;                // screen-space ambient occlusion, 1 = on
  int   ssr;                 // screen-space reflections in standing water, 1 = on
  int   fullscreen;          // borderless fullscreen, 1 = on
  // THREE STATES, NOT A FLAG: the shadow map's resolution IS the setting, so
  // "low" is a real quality step and not a half-disabled pass. 0 off, 1 low,
  // 2 high — shadow_tex_size() is the one reader of the distinction.
  int   shadows;
  int   bloom;               // glow: the bloom chain AND the sun shafts, one switch
  int   dust;                // airborne motes
  int   grain;               // film grain AND the vignette it belongs to
  // Exposure multiplier applied BEFORE the tonemap curve (uLook.w), so it is a
  // real exposure and not a lift on the displayed picture: 1 is the shipped look.
  float brightness;
  // Written by the GAME tab's slider on the main thread, read by audio_mix on the
  // audio thread. Atomic member, not a mirrored global with three publish sites.
  _Atomic float vol;         // master volume 0..1
  int   sp_bots;             // deathmatch opponents, range in SP_BOTS_DEF
  int   sp_diff;             // bot difficulty: 0 easy, 1 normal, 2 hard
  int   sp_frag;             // frag limit, range in SP_FRAG_DEF
  char  name[12];            // player name; glyph policy lives in name_glyph
  int   third_person;        // camera: 0 = first person, 1 = shoulder cam
  int   devmode;             // show the DEV tab; hard-gated by DEV_MENU, def 0
  float mv[MV_COUNT];        // movement tuning, see MV_DEF
  float wp[WPN_COUNT][WP_COUNT];  // per-weapon tuning, see WP_DEF/WPN_DEF
  float pad[PAD_COUNT];      // gamepad feel, see PAD_DEF
  int   pad_curve;           // aim response curve, index into PAD_CURVE_OPTS
  int   pad_inv_y;           // inverted look on the right stick
  int   pad_assist;          // aim assist (FRICTION ONLY), pad only; tuning = PAD_DEF PAD_AS_*
  char  bind[IN_COUNT][12];  // key tokens, resolved per-platform
  char  padbind[IN_COUNT][12];  // gamepad tokens, "none" = unbound
  // Master directory address, TWO keys: host:port in one field would be a parser
  // this file has nowhere else. 64 == the parser's val width, so it cannot truncate.
  char  mp_host[64];
  int   mp_port;
  // The release URL is a config key, not a compiled-in address: a repo move
  // would otherwise kill every installed client's updates for good.
  char  update_url[64];
  int   update_check;        // 0 disables the startup check entirely
  // Anonymous telemetry. The flag is the OPT-OUT; tele_id is a random u32 rolled
  // once and persisted, derived from NOTHING — not hardware, hostname or MAC.
  int      telemetry;        // 1 = send (default), 0 = off entirely
  unsigned tele_id;          // 0 = not yet generated
} config_t;

static config_t g_cfg;
static char     g_cfg_path[512];

// The tuning tables the SIM reads.
static const float *g_sim_mv = g_cfg.mv;
static const float (*g_sim_wp)[WP_COUNT] = g_cfg.wp;

// Q/E are the lean keys, so the weapon TOGGLE moved to x; an existing `bind_swap q` is
// moved by the migration pass below.
static void wp_key(char *out, size_t n, int w, const char *name) {
  snprintf(out, n, "%s_%s", WPN_DEF[w].prefix, name);
}

// THE TWO SPELLINGS ONE WEAPON PARAMETER ANSWERS TO, in one predicate: the
// "<prefix>_<name>" form config_write emits, and the pre-sniper single-weapon
// "wp_<name>", which maps onto the AR so a config tuned before there was a second
// weapon keeps its feel. Both live here so no caller can teach the parser a spelling
// the emitter does not know.
static int wp_key_match(const char *key, int w, const char *name) {
  char full[32];
  wp_key(full, sizeof full, w, name);
  return !strcmp(key, full) ||
         (w == WPN_AR && !strncmp(key, "wp_", 3) && !strcmp(key + 3, name));
}

// Valid MSAA sample counts are 0/2/4/8; anything else snaps to the next lower.
static int msaa_snap(long v) { return v >= 8 ? 8 : v >= 4 ? 4 : v >= 2 ? 2 : 0; }

// The player-name glyph policy in ONE place — A-Z (lowercase upcased) and 0-9. Three
// callers: the config parser, the server's name sanitizer and the name editor. It is
// also why ui_keyboard's grid can be a CONSTANT rather than a widget tree: this fixes
// the glyph set, so the layout follows from it.
static char name_glyph(char c) {
  if (c >= 'a' && c <= 'z') c -= 32;
  return ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? c : 0;
}

static config_t config_defaults(void) {
  // fov 100 is the genre's PC default; vsync 0 + fps_cap 120 matches the handheld
  // panel without vsync's latency; msaa 8 because flat facets and long straight
  // silhouettes are what MSAA is cheap on and what crawls without it. recoil_view 1
  // is the open value: the camera carries the whole climb, so the reticle never lies.
  config_t c = {.sense = 1.0f, .ads_sense = 1.0f, .fov = 100.0f, .recoil_view = 1.0f,
                .vsync = 0, .fps_cap = 120, .show_fps = 1, .msaa = 8,
                .ssao = 1, .ssr = 1,
                // The graphics defaults ARE the top quality preset, deliberately:
                // an untouched install renders exactly the picture it rendered
                // before these keys existed, byte for byte.
                .fullscreen = 1, .shadows = 2, .bloom = 1, .dust = 1, .grain = 1,
                .brightness = 1.0f,
                // sp_diff 2 = HARD. The bot count came down to 1, so difficulty
                // is what carries the challenge now — see SP_BOTS_DEF.
                .vol = VOL_DEF.def, .sp_bots = (int)SP_BOTS_DEF.def, .sp_diff = 2,
                .sp_frag = (int)SP_FRAG_DEF.def, .name = "PLAYER",
                // LINEAR (0): the curve and the friction assist are two non-linearities
                // on one input, and stacked they made small corrections unpredictable.
                .pad_curve = 0, .pad_assist = 1,
                // The master server, compiled in so a fresh install is playable and
                // overridable by config.cfg.
                .mp_host = "178.104.216.82", .mp_port = NET_PORT_DEF,
                .update_url = "https://github.com/codehamr/skill-issue",
                // .tele_id = 0 is the "not yet generated" sentinel, spelled out
                // rather than left to the implicit zero: tele_tick branches on it.
                .update_check = 1, .telemetry = 1, .tele_id = 0};
  for (int i = 0; i < MV_COUNT; i++) c.mv[i] = MV_DEF[i].def;
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) c.wp[w][i] = WP_DEFAULTS[w][i];
  for (int i = 0; i < PAD_COUNT; i++) c.pad[i] = PAD_DEF[i].def;
  for (int i = 0; i < IN_COUNT; i++) {
    snprintf(c.bind[i], sizeof c.bind[i], "%s", ACTION_DEF[i].dflt);
    snprintf(c.padbind[i], sizeof c.padbind[i], "%s", ACTION_DEF[i].pdflt);
  }
  return c;
}

// Composed pad-bind config key for action i: "padbind_forward", ...
static void padbind_key(char *out, size_t n, int i) {
  snprintf(out, n, "pad%s", ACTION_DEF[i].key);
}

// ONE emitter for the whole config: config_write and --print-config used to print
// the same keys from two hand-maintained lists, and had already drifted.
static void config_emit(FILE *f, const config_t *c) {
  fprintf(f,
    "# skill-issue config — auto-generated defaults, edit freely.\n"
    "# key tokens: a-z 0-9 space tab lshift lctrl | mouse1=left mouse2=right mouse3=middle\n"
    "#             mouse4/mouse5 = thumb buttons (back/forward)\n"
    "sense %.3f\n"
    "ads_sense %.3f\n"
    "fov %.0f\n"
    "recoil_view %.2f\n"
    "vsync %d\n"
    "fps_cap %d\n"
    "show_fps %d\n"
    "msaa %d\n"
    "ssao %d\n"
    "ssr %d\n"
    "fullscreen %d\n"
    "shadows %d\n"
    "bloom %d\n"
    "dust %d\n"
    "grain %d\n"
    "brightness %.2f\n"
    "third_person %d\n"
    "devmode %d\n",
    (double)c->sense, (double)c->ads_sense, (double)c->fov, (double)c->recoil_view,
    c->vsync, c->fps_cap,
    c->show_fps, c->msaa, c->ssao, c->ssr,
    c->fullscreen, c->shadows, c->bloom, c->dust, c->grain, (double)c->brightness,
    c->third_person, c->devmode);
  fprintf(f, "%s %.2f\n", VOL_DEF.key, (double)c->vol);
  fprintf(f, "sp_bots %d\nsp_difficulty %d\nsp_fraglimit %d\nplayer_name %s\n",
          c->sp_bots, c->sp_diff, c->sp_frag, c->name);
    // COUPLING: tele_id_persisted() re-reads this file for "tele_id %u" to check
    // the id landed. RENAMING THIS KEY DISABLES TELEMETRY. Rename both together.
  fprintf(f, "mp_host %s\nmp_port %d\nupdate_url %s\nupdate_check %d\n"
             "telemetry %d\ntele_id %u\n",
          c->mp_host, c->mp_port, c->update_url, c->update_check,
          c->telemetry, c->tele_id);
  for (int i = 0; i < MV_COUNT; i++)
    fprintf(f, "%s %.2f\n", MV_DEF[i].key, (double)c->mv[i]);
  for (int w = 0; w < WPN_COUNT; w++)
    for (int i = 0; i < WP_COUNT; i++) {
      char key[32];
      wp_key(key, sizeof key, w, WP_DEF[i].key);
      fprintf(f, "%s %.2f\n", key, (double)c->wp[w][i]);
    }
  for (int i = 0; i < PAD_COUNT; i++)
    fprintf(f, "%s %.2f\n", PAD_DEF[i].key, (double)c->pad[i]);
  fprintf(f, "pad_curve %d\npad_inv_y %d\npad_assist %d\n",
          c->pad_curve, c->pad_inv_y, c->pad_assist);
  for (int i = 0; i < IN_COUNT; i++)
    fprintf(f, "%s %s\n", ACTION_DEF[i].key, c->bind[i]);
  for (int i = 0; i < IN_COUNT; i++) {
    char pk[24];
    padbind_key(pk, sizeof pk, i);
    fprintf(f, "%s %s\n", pk, c->padbind[i]);
  }
}

// Write-to-temp-then-rename, because fopen(path, "w") truncates immediately and
// this runs when a handheld gets hard-powered-off. On error the old file stays.
static void config_write(const char *path, const config_t *c) {
  char tmp[512];
  // Checked, not fire-and-forget: a truncated temp path would write some other
  // file and rename it over nothing. Also what keeps -Wformat-truncation quiet.
  int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
  if (n < 0 || (size_t)n >= sizeof tmp) {
    fprintf(stderr, "config: path too long, not writing: %s\n", path);
    return;
  }
  FILE *f = fopen(tmp, "w");
  if (!f) { fprintf(stderr, "config: cannot write %s\n", tmp); return; }
  config_emit(f, c);
  int bad = ferror(f);
  if (fclose(f) != 0) bad = 1;
  if (bad) {
    fprintf(stderr, "config: write failed, keeping old %s\n", path);
    remove(tmp);
    return;
  }
#ifdef _WIN32
  if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
#else
  if (rename(tmp, path) != 0)
#endif
    fprintf(stderr, "config: cannot replace %s\n", path);
}

// Tolerant float parse: accepts a decimal comma (hand-edited configs on German
// Windows), keeps the previous value on garbage rather than clamping nonsense.
static float parse_float(const char *s, float prev, float lo, float hi) {
  // buf is DELIBERATELY not val's width: 11 chars is the historical numeric
  // budget, which pins every float key's parse across the val widening. The
  // precision spells that truncation out rather than letting snprintf perform it
  // silently, and it is derived from sizeof so it cannot drift from the buffer.
  // A val that long is a number no key wants: what survives still goes through
  // strtof and the f_clamp below, so the worst case is a clamped bound.
  char buf[12];
  snprintf(buf, sizeof buf, "%.*s", (int)(sizeof buf - 1), s);
  for (char *q = buf; *q; q++)
    if (*q == ',') *q = '.';
  char *end;
  float v = strtof(buf, &end);
  if (end == buf) { fprintf(stderr, "config: bad number '%s'\n", s); return prev; }
    // Bit-wise, NOT isnan()/v != v: -ffast-math folds those. +/-inf stays, since
    // it clamps to the bound; NaN would reach a (int) cast, which is UB.
  if (f_is_nan(v)) {
    fprintf(stderr, "config: bad number '%s'\n", s);
    return prev;
  }
  return f_clamp(v, lo, hi);
}

// Integer twin of parse_float (an int never has a decimal comma). Keeps the
// previous value on garbage, exactly like the float path.
static long parse_long(const char *s, long prev) {
  char *end;
  long v = strtol(s, &end, 10);
  if (end == s) { fprintf(stderr, "config: bad number '%s'\n", s); return prev; }
  return v;
}
// ...and the clamped form, mirroring parse_float's (prev, lo, hi) signature so
// the open-coded `(int)(v < lo ? lo : v > hi ? hi : v)` ladders go away with it.
static int parse_int(const char *s, int prev, long lo, long hi) {
  long v = parse_long(s, prev);
  return (int)(v < lo ? lo : v > hi ? hi : v);
}
// Same clamp against a slider's own published range. Integer math throughout: a
// long round-tripped through float loses the top of a 1..1000 range.
static int parse_int_def(long v, const param_def_t *d) {
  long lo = (long)d->lo, hi = (long)d->hi;
  return (int)(v < lo ? lo : v > hi ? hi : v);
}

// Bind migration: on a collision the action squatting on another's default moves back
// to its own, else the higher index does. Repeated until stable. ONE function for both
// device tables — the ui_binds_reset idiom — so the two rules cannot drift.
//
// The PAD table alone lets "none" repeat: it is the absence of a bind, not a key two
// actions could fight over, and four pad defaults ARE "none" (the pad moves on a
// stick), so ungated this would report four self-collisions on every load.
static void config_bind_migrate(config_t *c, int pad) {
  char (*tbl)[12] = pad ? c->padbind : c->bind;
  for (int pass = 0; pass < IN_COUNT; pass++) {
    int changed = 0;
    for (int i = 0; i < IN_COUNT && !changed; i++)
      for (int j = i + 1; j < IN_COUNT && !changed; j++) {
        if (pad && !strcmp(tbl[i], "none")) continue;
        if (strcmp(tbl[i], tbl[j]) != 0) continue;
        const char *dj = pad ? ACTION_DEF[j].pdflt : ACTION_DEF[j].dflt;
        int fix = !strcmp(tbl[i], dj) ? i : j;
        const char *df = pad ? ACTION_DEF[fix].pdflt : ACTION_DEF[fix].dflt;
        fprintf(stderr, "config: '%s' bound to both %s%s and %s — %s reset to '%s'\n",
                tbl[i], pad ? "pad " : "", ACTION_DEF[i].key, ACTION_DEF[j].key,
                ACTION_DEF[fix].key, df);
        snprintf(tbl[fix], sizeof tbl[fix], "%s", df);
        changed = 1;
      }
    if (!changed) break;
  }
}

// DID THIS LINE NAME A WEAPON PARAMETER? Two vocabularies, one column and one clamp:
// WP_DEF's live names, and the superseded ones in WP_LEGACY_KEY, which are parsed and
// never emitted because the untouched-install migration below reads a WHOLE profile
// and a key it cannot place is a row it cannot recognise.
static int config_wp_key(config_t *c, const char *key, const char *val) {
  for (int w = 0; w < WPN_COUNT; w++) {
    int i = -1;
    for (int k = 0; k < WP_COUNT && i < 0; k++)
      if (wp_key_match(key, w, WP_DEF[k].key)) i = k;
    for (unsigned li = 0;
         li < sizeof WP_LEGACY_KEY / sizeof *WP_LEGACY_KEY && i < 0; li++)
      if (wp_key_match(key, w, WP_LEGACY_KEY[li].key)) i = WP_LEGACY_KEY[li].idx;
    if (i < 0) continue;
    c->wp[w][i] = parse_float(val, c->wp[w][i], WP_DEF[i].lo, WP_DEF[i].hi);
    return 1;
  }
  return 0;
}

static void config_load_or_create(const char *path, config_t *c) {
  *c = config_defaults();
  FILE *f = fopen(path, "r");
  if (!f) {
    // Only a genuinely missing file means "first start" — a locked or
    // unreadable existing config must never be overwritten with defaults.
    if (errno == ENOENT) config_write(path, c);
    else fprintf(stderr, "config: cannot read %s — using defaults\n", path);
    return;
  }
  char line[128];
  while (fgets(line, sizeof line, f)) {
    int cut_tail = 0;
    if (!strchr(line, '\n') && !feof(f)) {  // overlong line: discard the tail
      int ch;
      while ((ch = fgetc(f)) != EOF && ch != '\n') {}
      cut_tail = 1;  // loud below — a silently shortened host is a wrong host
    }
    char *hash = strchr(line, '#');
    int cut_hash = hash && hash > line && hash[-1] != ' ' && hash[-1] != '\t' &&
                   hash[-1] != '\n' && hash[-1] != '\r';  // '#' glued to a token
    if (hash) *hash = '\0';
      // val is 64 wide for the string keys; %11s once cut "192.168.1.100" to the
      // still-VALID "192.168.1.1". Widen %63s and val[64] together or it smashes.
    char key[24], val[64];
    int used = -1;
    if (sscanf(line, "%23s %63s%n", key, val, &used) != 2) continue;
    // Loud, not silent: a capped value, a dropped tail or a glued '#' would all
    // reach g_cfg mutilated while the file shows the full override.
    if (used > 0) {
      const char *rest = line + used;
      while (*rest == ' ' || *rest == '\t' || *rest == '\n' || *rest == '\r') rest++;
      if (*rest) fprintf(stderr, "config: value of '%s' truncated or has trailing garbage\n", key);
    }
    if (cut_tail) fprintf(stderr, "config: line for '%s' longer than 127 chars — tail dropped\n", key);
    if (cut_hash) fprintf(stderr, "config: '#' cuts the value of '%s' (quote it is not supported)\n", key);
    if      (!strcmp(key, "sense")) c->sense = parse_float(val, c->sense, 0.001f, 10.0f);
    // Ranges are wider than the sliders' on both ends: a hand-edited file may
    // ask for more than the menu offers, and the menu must not rewrite it.
    else if (!strcmp(key, "ads_sense")) c->ads_sense = parse_float(val, c->ads_sense, 0.01f, 10.0f);
    else if (!strcmp(key, "fov"))   c->fov   = parse_float(val, c->fov, 50.0f, 130.0f);
    // 0 = the camera ignores the climb entirely, 1 = it carries all of it. Clamped to
    // that range: past 1 the view would outrun the barrel it is supposed to be on.
    else if (!strcmp(key, "recoil_view"))
      c->recoil_view = parse_float(val, c->recoil_view, 0.0f, 1.0f);
    // The 0/1 flags stay on `? 1 : 0` rather than a parse_int(0, 1) clamp: a
    // hand-edited `vsync -1` means "on" today and a clamp would make it "off".
    else if (!strcmp(key, "vsync"))    c->vsync    = parse_long(val, c->vsync) ? 1 : 0;
    else if (!strcmp(key, "msaa"))     c->msaa     = msaa_snap(parse_long(val, c->msaa));
    else if (!strcmp(key, "ssao"))     c->ssao     = parse_long(val, c->ssao) ? 1 : 0;
    else if (!strcmp(key, "ssr"))      c->ssr      = parse_long(val, c->ssr) ? 1 : 0;
    else if (!strcmp(key, "fullscreen"))
      c->fullscreen = parse_long(val, c->fullscreen) ? 1 : 0;
    else if (!strcmp(key, "bloom"))    c->bloom    = parse_long(val, c->bloom) ? 1 : 0;
    else if (!strcmp(key, "dust"))     c->dust     = parse_long(val, c->dust) ? 1 : 0;
    else if (!strcmp(key, "grain"))    c->grain    = parse_long(val, c->grain) ? 1 : 0;
    // The one graphics key that CLAMPS instead of taking `? 1 : 0`: it is a
    // three-state cycle, and "shadows 7" has no reading as "on" the way a flag's
    // stray -1 does — it is a number out of range, so it snaps into range.
    else if (!strcmp(key, "shadows"))  c->shadows  = parse_int(val, c->shadows, 0, 2);
    // Half to one-and-a-half of the shipped exposure, which is the slider's own
    // range: past either end the picture is a black frame or a white one, and
    // neither is a setting a hand-edited file should be able to reach.
    else if (!strcmp(key, "brightness"))
      c->brightness = parse_float(val, c->brightness, 0.5f, 1.5f);
    else if (!strcmp(key, "fps_cap"))  c->fps_cap  = parse_int(val, c->fps_cap, 0, 1000);
    else if (!strcmp(key, "show_fps")) c->show_fps = parse_long(val, c->show_fps) ? 1 : 0;
    else if (!strcmp(key, "third_person")) {
      c->third_person = parse_long(val, c->third_person) ? 1 : 0;
    }
    // Runtime opt-in with no menu toggle on purpose: a knob that can switch
    // itself on from inside the menu is one stray click from the workbench.
    else if (!strcmp(key, "devmode")) {
      c->devmode = parse_long(val, c->devmode) ? 1 : 0;
    }
    else if (!strcmp(key, VOL_DEF.key)) {
      c->vol = parse_float(val, c->vol, VOL_DEF.lo, VOL_DEF.hi);
    }
    else if (!strcmp(key, "sp_bots") || !strcmp(key, "sp_difficulty") ||
             !strcmp(key, "sp_fraglimit")) {
      if      (key[3] == 'b') c->sp_bots = parse_int_def(parse_long(val, c->sp_bots),
                                                         &SP_BOTS_DEF);
      // sp_diff is a ui_cycle enum, not a slider, so it has no param_def_t.
      else if (key[3] == 'd') c->sp_diff = parse_int(val, c->sp_diff, 0, 2);
      else                    c->sp_frag = parse_int_def(parse_long(val, c->sp_frag),
                                                         &SP_FRAG_DEF);
    }
    else if (!strcmp(key, "player_name")) {
      int n = 0;
      for (const char *q = val; *q && n < (int)sizeof c->name - 1; q++) {
        char g = name_glyph(*q);
        if (g) c->name[n++] = g;
      }
      if (n) c->name[n] = '\0';  // empty/garbage keeps the default
    }
    else if (!strcmp(key, "mp_host")) {
      snprintf(c->mp_host, sizeof c->mp_host, "%s", val);   // 64 == val width
    }
    else if (!strcmp(key, "mp_port")) {
      c->mp_port = parse_int(val, c->mp_port, 1, 65535);
    }
    else if (!strcmp(key, "update_url")) {
      snprintf(c->update_url, sizeof c->update_url, "%s", val);
    }
    else if (!strcmp(key, "update_check")) {
      c->update_check = parse_long(val, c->update_check) ? 1 : 0;
    }
    // The opt-out flag mirrors update_check exactly: `? 1 : 0`, not a clamp, so
    // a hand-edited `telemetry -1` means "on" like every other flag here.
    else if (!strcmp(key, "telemetry")) {
      c->telemetry = parse_long(val, c->telemetry) ? 1 : 0;
    }
    // strtoul, NOT parse_long: long is 32-bit on MinGW, so every id with the
    // high bit set would saturate to LONG_MAX. Garbage parsing to 0 is correct.
    else if (!strcmp(key, "tele_id")) {
      c->tele_id = (unsigned)strtoul(val, NULL, 10);
    }
    else {
      int hit = 0;
      for (int i = 0; i < MV_COUNT; i++)
        if (!strcmp(key, MV_DEF[i].key)) {
          c->mv[i] = parse_float(val, c->mv[i], MV_DEF[i].lo, MV_DEF[i].hi);
          hit = 1;
        }
      if (!hit) hit = config_wp_key(c, key, val);
      for (int i = 0; i < PAD_COUNT && !hit; i++)
        if (!strcmp(key, PAD_DEF[i].key)) {
          c->pad[i] = parse_float(val, c->pad[i], PAD_DEF[i].lo, PAD_DEF[i].hi);
          hit = 1;
        }
      // Migration: pad_sens_x in deg/s (the pre-notch scheme) maps onto the
      // 1-10 scale; pad_sens_y is derived now and its old value is dropped.
      if (!hit && !strcmp(key, "pad_sens_x")) {
        c->pad[PAD_SENS] = (float)parse_int(val, (int)(c->pad[PAD_SENS] * PAD_SENS_DEG_X),
                                            60, 600) / PAD_SENS_DEG_X;
        c->pad[PAD_SENS] = roundf(f_clamp(c->pad[PAD_SENS], PAD_DEF[PAD_SENS].lo,
                                          PAD_DEF[PAD_SENS].hi));
        hit = 1;
      }
      if (!hit && !strcmp(key, "pad_sens_y")) hit = 1;
      if (!hit && !strcmp(key, "pad_curve")) { c->pad_curve = parse_int(val, c->pad_curve, 0, 2); hit = 1; }
      if (!hit && !strcmp(key, "pad_inv_y")) { c->pad_inv_y = parse_long(val, c->pad_inv_y) ? 1 : 0; hit = 1; }
      if (!hit && !strcmp(key, "pad_assist")) { c->pad_assist = parse_long(val, c->pad_assist) ? 1 : 0; hit = 1; }
      for (int i = 0; i < IN_COUNT && !hit; i++)
        if (!strcmp(key, ACTION_DEF[i].key)) {
            // Lowercase single-char tokens: key_from_token maps 'W' and 'w' to
            // one key id, so an uppercase token would pass the dedup below.
          char t0 = val[0];
          if (t0 >= 'A' && t0 <= 'Z' && !val[1]) t0 = (char)(t0 - 'A' + 'a');
            // %.*s, not %s: val is 64 wide, the token 12 — an unbounded %s is
            // a real -Wformat-truncation the LTO build and MinGW both swallow.
          snprintf(c->bind[i], sizeof c->bind[i], "%.*s", (int)sizeof c->bind[i] - 1, val);
          c->bind[i][0] = t0;
          hit = 1;
        }
      for (int i = 0; i < IN_COUNT && !hit; i++) {
        char pk[24];
        padbind_key(pk, sizeof pk, i);
        if (!strcmp(key, pk)) {
          snprintf(c->padbind[i], sizeof c->padbind[i], "%.*s",
                   (int)sizeof c->padbind[i] - 1, val);
          hit = 1;
        }
      }
      if (!hit) fprintf(stderr, "config: ignoring unknown key '%s'\n", key);
    }
  }
  fclose(f);

  // An existing untouched config otherwise pins a superseded weapon profile forever:
  // defaults only fill missing keys. Migrate an exact complete legacy row, never a
  // partially matching one, so one deliberate user tune protects all of it. Each
  // weapon is judged on its own row, and every superseded row a shipped build ever
  // wrote stays listed — a player who skipped a version arrives from the older one.
  int legacy_ar = 1, flat_trace_ar = 1, spring_ar = 1, placed_ar = 1, spring_sr = 1;
  for (int i = 0; i < WP_COUNT; i++) {
    if (fabsf(c->wp[WPN_AR][i] - WP_AR_DEFAULTS_PRE_PHYSICAL[i]) > 0.0001f)
      legacy_ar = 0;
    if (fabsf(c->wp[WPN_AR][i] - WP_AR_DEFAULTS_PRE_WALL_TRACE[i]) > 0.0001f)
      flat_trace_ar = 0;
    if (fabsf(c->wp[WPN_AR][i] - WP_AR_DEFAULTS_PRE_PATTERN[i]) > 0.0001f)
      spring_ar = 0;
    if (fabsf(c->wp[WPN_AR][i] - WP_AR_DEFAULTS_PRE_FOLLOWER[i]) > 0.0001f)
      placed_ar = 0;
    if (fabsf(c->wp[WPN_SR][i] - WP_SR_DEFAULTS_PRE_PATTERN[i]) > 0.0001f)
      spring_sr = 0;
  }
  if (legacy_ar || flat_trace_ar || spring_ar || placed_ar) {
    memcpy(c->wp[WPN_AR], WP_DEFAULTS[WPN_AR], sizeof c->wp[WPN_AR]);
    fprintf(stderr, "config: migrated untouched AR profile to follower defaults\n");
  }
  if (spring_sr) {
    memcpy(c->wp[WPN_SR], WP_DEFAULTS[WPN_SR], sizeof c->wp[WPN_SR]);
    fprintf(stderr, "config: migrated untouched SR profile to pattern defaults\n");
  }

  config_bind_migrate(c, 0);
  config_bind_migrate(c, 1);
}

// Directory of the running executable, with trailing slash, so config.cfg
// lands next to game.exe no matter where it is started from.
// A TRUNCATED PATH IS NOT A PATH — the same guard exe_path spells out below. Both
// queries report a full buffer exactly as they report a fitting one, and the walk
// back to the last slash then lands inside a cut component, i.e. a plausible
// directory that is not ours. Empty degrades to a relative "config.cfg", which is
// the behaviour the failure branch already had.
static void exe_dir(char *out, size_t n) {
#ifdef _WIN32
  DWORD len = GetModuleFileNameA(NULL, out, (DWORD)n);
  if (len == 0 || len >= (DWORD)n) { out[0] = '\0'; return; }
  while (len > 0 && out[len - 1] != '\\' && out[len - 1] != '/') len--;
  out[len] = '\0';
#else
  ssize_t len = readlink("/proc/self/exe", out, n - 1);
  if (len <= 0 || (size_t)len >= n - 1) { out[0] = '\0'; return; }
  out[len] = '\0';
  char *slash = strrchr(out, '/');
  if (slash) slash[1] = '\0';
#endif
}
