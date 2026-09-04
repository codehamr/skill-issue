// =============================================================================
// ui/settings.inc  —  panel widgets (ui_pill/ui_cycle/ui_slider), bind rows,
// ui_keyboard, session_pauses.
// Proof: uistat, click-band sweep, homeui. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: session_pauses() is the one predicate for "does this session
// freeze?" (windowed SP with the menu open does, networked never). The
// click band is 14 tall and half-open so no boundary click can touch two
// rows.
// =============================================================================
// ---------------------------------------------------------------------------
// Settings menu — immediate mode: layout, hit-testing and drawing in one pass
// per frame. The platform feeds cursor position (real px), left-button state,
// and captured key tokens for rebinding; everything else lives here.
// ---------------------------------------------------------------------------

// GRATICULE palette, cooled (display space — UI colors bypass the tonemap).
#define C_BONE   0.941f, 0.957f, 0.973f
#define C_HAZE   0.541f, 0.580f, 0.643f
#define C_BLAZE  1.000f, 0.549f, 0.102f
#define C_SIGNAL 0.310f, 0.765f, 0.910f
#define C_THREAT 1.000f, 0.294f, 0.227f
// Legacy role names, kept as the one copy every widget already reads:
#define C_TEXT   C_BONE
#define C_DIM    C_HAZE
#define C_ACCENT C_BLAZE
// Wrap a comma-triple palette macro + alpha into the float[4] the _c
// primitives take: C4(C_BLAZE, 0.8f).
#define C4(...) ((float[4]){__VA_ARGS__})

static int   g_ui_backdrop;   // frosted blur layer was painted this frame
[[maybe_unused]] static const char *g_connect_host;   // --connect HOST[:PORT] (CLI, Linux)
static const char *g_boot_connect;   // native_main kicks the join after start
static int  start_connect(const char *host, int port);   // begin a join
static void leave_match(void);                            // drop back to local
// ONE predicate for "does this session pause?": an open menu pauses a windowed
// SINGLEPLAYER session, but a networked one does NOT (you would stand there being shot)
// and never scales with hit-stop.
static int session_pauses(void) {
  if (g_online) return 0;         // a live session is never frozen
  // ...and neither is the BOOT SCREEN, which is a menu that is not a pause: it is
  // showing the player a live camera on a live figure, and every beat in it — the swap
  // timeline, the weapon coming to the eye, the idle sway — is a fixed-TICK_DT solver.
  return g_menu_open && !g_home;
}
static int   g_ui_rebind = -1;   // action currently being rebound, -1 = none
static int   g_ui_rebind_pad;    // ...and which bind table that row edits
static int   g_ui_quit;          // set by the QUIT tile
// TWO-PRESS DESTRUCTIVE ROWS, ONE LATCH, AND IT REMEMBERS WHICH ROW. A control that
// destroys STATE THE PLAYER CANNOT REBUILD — the round's scoreboard, a bind table —
// asks twice. TWO ROWS ARE DELIBERATELY NOT ON THIS LIST, for the same reason in two
// shapes: neither costs the player anything the second press would give back. Leaving
// a server ends in the rail it was pressed from, one press from rejoining (see the
// LEAVE MATCH tile); and QUIT asks for what the window manager already grants in one
// press — the title bar's close and Alt+F4 end this process without a confirm, and
// write the config on the way out exactly as that tile does.
typedef enum { UI_ARM_NONE, UI_ARM_RESTART,
               UI_ARM_RSTKB, UI_ARM_RSTPAD } ui_arm_t;
static ui_arm_t g_ui_arm;
static int   g_ui_root_focus;  // rail row to return to, THIS menu session only
// 1 = this row was already armed, i.e. GO. 0 = the press only armed it.
static int ui_arm_take(ui_arm_t which) {
  if (g_ui_arm == which) { g_ui_arm = UI_ARM_NONE; return 1; }
  g_ui_arm = which;
  return 0;
}
static float g_ui_cx, g_ui_cy;   // cursor in real framebuffer px
static int   g_ui_mdown;         // left button level state
static int   g_ui_drag;          // active slider, 0 = none

// --- Pad navigation. pad_sync (or the harness's `nav`) pushes events; ui_menu consumes
// them in its preamble.
enum { NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_OK, NAV_BACK, NAV_TABL, NAV_TABR };
static int g_nav_q[16], g_nav_n;
static int g_nav_kbd;   // last nav event came from the keyboard: the page hint
                        // then speaks ARROWS/ENTER/ESC instead of A/B
static void nav_push(int ev) {
  if (g_nav_n < (int)(sizeof g_nav_q / sizeof g_nav_q[0])) g_nav_q[g_nav_n++] = ev;
}
static int g_ui_focus;         // focused row within the active page
static int g_ui_nfocus;        // rows registered this frame...
static int g_ui_nfocus_last;   // ...and last frame (wrap-around runs pre-layout)
static float g_focus_sx, g_focus_sy, g_focus_sw, g_focus_sh;  // ring glide state
static int g_ui_pad_mode;      // last menu input was the pad: focus ring, no cursor
static int g_nav_act = -1;     // OK/LEFT/RIGHT pending for the focused row

// One row's geometry as the move sees it. `lr` is the one thing a rect cannot say:
// whether this row SPENDS left/right on its own value (a slider, a cycle, a segmented
// selector) or leaves it to the navigation (a tile, a button, a bind pill).
#define UI_MAX_FOCUS 64   // the longest page is 18 rows; a row past this keeps
                          // its focus id and simply contributes no geometry
typedef struct { float x, y, w, h; int lr; } focus_rect_t;
static focus_rect_t g_focus_r[UI_MAX_FOCUS];
static int g_focus_rn;        // rects recorded LAST frame (the move is pre-layout)
static float g_nav_ax = -1e9f, g_nav_ay;   // the VIRTUAL CURSOR: a vertical move
                                           // keeps the x it was given and a horizontal
                                           // one keeps the y, so walking down a two-
                                           // column screen and back up returns to the
                                           // column you left instead of to whichever
                                           // centre happens to be nearer. < -1e8 =
                                           // unset, adopt the row's own
static void nav_anchor_reset(void) { g_nav_ax = -1e9f; }

// Distance from `p` to the interval [lo, hi], 0 inside it.
static float nav_gap(float p, float lo, float hi) {
  return p < lo ? lo - p : p > hi ? p - hi : 0.0f;
}

// Step the focus one row in `dir` (NAV_UP..NAV_RIGHT). Two tiers, and the
// second one is VERTICAL ONLY on purpose:
//   1. rows that overlap the current row on the perpendicular axis — the
//      aligned case, which is every page in this menu;
//   2. for up/down, the nearest row weighted by how far off-axis it is, so a
//      future staggered layout can never strand a row out of reach.
// A sideways step gets no such fallback: no overlap means nothing is beside the
// row, and a weighted LEFT would then fly across the screen into another row's
// business — reaching that row is what up/down is for. When up/down finds
// nothing it WRAPS to the far end, which is what the ±1 index did.
static void nav_move(int dir) {
  int n = g_focus_rn;
  if (n <= 0) return;
  if (g_ui_focus < 0 || g_ui_focus >= n) { g_ui_focus = 0; return; }
  int vert = dir == NAV_UP || dir == NAV_DOWN;
  float sgn = (dir == NAV_DOWN || dir == NAV_RIGHT) ? 1.0f : -1.0f;
  const focus_rect_t *c = &g_focus_r[g_ui_focus];
  if (g_nav_ax < -1e8f) {
    g_nav_ax = c->x + c->w * 0.5f;
    g_nav_ay = c->y + c->h * 0.5f;
  }
  float anch = vert ? g_nav_ax : g_nav_ay;
  float cmid = vert ? c->y + c->h * 0.5f : c->x + c->w * 0.5f;
  float clo  = vert ? c->x : c->y, cex = vert ? c->w : c->h;
  int best = -1, alt = -1;
  float bs = 0, bp = 0, as = 0;
  for (int i = 0; i < n; i++) {
    if (i == g_ui_focus) continue;
    const focus_rect_t *r = &g_focus_r[i];
    float rmid = vert ? r->y + r->h * 0.5f : r->x + r->w * 0.5f;
    float along = (rmid - cmid) * sgn;
    // Half a UI unit of slack is what makes a PAIR a pair: two tiles sharing a
    // y are neither above nor below one another, so a vertical step skips both
    // of its own siblings instead of picking the nearer half by rounding.
    if (along <= 0.5f) continue;
    float rlo = vert ? r->x : r->y, rex = vert ? r->w : r->h;
    float gap = nav_gap(anch, rlo, rlo + rex);
    if (rlo < clo + cex && clo < rlo + rex) {
      if (best < 0 || along < bs - 0.5f ||
          (along < bs + 0.5f && gap < bp)) { best = i; bs = along; bp = gap; }
    } else if (vert) {
      float sc = along + 2.0f * gap;
      if (alt < 0 || sc < as) { alt = i; as = sc; }
    }
  }
  if (best < 0) best = alt;
  if (best < 0 && vert) {
    float ext = 0;
    for (int i = 0; i < n; i++) {
      const focus_rect_t *r = &g_focus_r[i];
      float end = (r->y + r->h * 0.5f) * -sgn;   // `far` is a MACRO in windef.h
      float gap = nav_gap(anch, r->x, r->x + r->w);
      if (best < 0 || end > ext + 0.5f ||
          (end > ext - 0.5f && gap < bp)) { best = i; ext = end; bp = gap; }
    }
  }
  if (best < 0) return;
  const focus_rect_t *r = &g_focus_r[best];
  g_ui_focus = best;
  if (vert) g_nav_ay = r->y + r->h * 0.5f;   // the other axis is CARRIED
  else      g_nav_ax = r->x + r->w * 0.5f;
}

// Does the focused row spend left/right on its own value? An empty table (the
// first frame of a page) answers yes, which is the pre-geometry behaviour: park
// the event for whichever row is about to claim focus id 0.
static int nav_focus_lr(void) {
  if (g_focus_rn <= 0) return 1;
  return g_ui_focus >= 0 && g_ui_focus < g_focus_rn && g_focus_r[g_ui_focus].lr;
}

typedef enum { UI_PAGE_ROOT, UI_PAGE_MATCH, UI_PAGE_SETTINGS,
               UI_PAGE_CONTROLS, UI_PAGE_DEV, UI_PAGE_COUNT } ui_page_t;

static ui_page_t g_ui_page = UI_PAGE_ROOT;

// "Is the player looking at the entry screen rather than at one of its forms" —
// the updater's own question (see upd_auto_tick), answered where the page state
// lives so there is one reader of the enum outside this section and no copy of
// the enum's spelling in the updater.
static int ui_on_entry_screen(void) { return g_ui_page == UI_PAGE_ROOT; }
static int g_ui_control;  // CONTROLS page: 0 keyboard/mouse, 1 gamepad
static int g_ui_tab;      // legacy uistat field; mirrors the page enum
static int g_ui_pad_sub;  // CONTROLS/GAMEPAD: 0 sticks+aim, 1 buttons
static int g_ui_dev_sub;  // DEV page: 0 movement, 1 weapon, 2 assist
static int g_ui_set_sub;  // SETTINGS page: 0 game, 1 video

// Swallow currently-held pad buttons until they are released — the pad analogue of
// WM_MOUSEACTIVATE's MA_ACTIVATEANDEAT and the 150 ms refocus click swallow.
static void pad_eat_held(void);

// Defined by each platform backend: react to menu open/close (cursor
// clip/visibility, input reset), to a vsync toggle and to a fullscreen toggle.
// The row writes g_cfg first and calls the hook after, so a backend reads the
// new value rather than being handed it.
static void platform_menu_changed(void);
static void platform_vsync_changed(void);
static void platform_fullscreen_changed(void);

static int g_ui_name_edit;  // Settings page: the name field is capturing keys

// THE ONE FIELD IN THE GAME THAT NEEDS A KEYBOARD, ON THE ONE DEVICE THAT HAS NONE. The
// name editor took keys and nothing else, so a pad player armed it and was told to
// "TYPE ON KEYBOARD" — on a handheld.
#define UI_KB_COLS 9
#define UI_KB_GROWS 4                        // glyph rows
#define UI_KB_ROWS (UI_KB_GROWS + 1)         // ...plus DELETE / DONE
static const char UI_KB_ROW[UI_KB_GROWS][UI_KB_COLS + 1] = {
  "ABCDEFGHI", "JKLMNOPQR", "STUVWXYZ0", "123456789",
};
static int g_ui_kb;         // the on-screen keyboard is up with the name edit
static int g_ui_kb_r, g_ui_kb_c;   // cursor: row 0..UI_KB_ROWS-1, col in row

static int ui_kb_cols(int r) { return r < UI_KB_GROWS ? UI_KB_COLS : 2; }

// Feed a token to the name editor: single chars append (A-Z/0-9), "back"
// deletes, "enter"/"escape" finish. An empty name falls back to PLAYER.
static void ui_name_key(const char *tok) {
  size_t l = strlen(g_cfg.name);
  if (!strcmp(tok, "enter") || !strcmp(tok, "escape")) {
    if (!l) snprintf(g_cfg.name, sizeof g_cfg.name, "PLAYER");
    g_ui_name_edit = 0;
    g_ui_kb = 0;    // the keyboard exists only for as long as the field does
  } else if (!strcmp(tok, "back")) {
    if (l) g_cfg.name[l - 1] = '\0';
  } else if (tok[0] && !tok[1] && l < sizeof g_cfg.name - 1) {
    char g = name_glyph(tok[0]);
    if (g) {
      g_cfg.name[l] = g;
      g_cfg.name[l + 1] = '\0';
    }
  }
}

// A row change keeps the cursor's horizontal POSITION, not its index — the
// action row is two cells wide and the glyph rows are nine, and stepping down
// from "E" onto "DELETE" has to land under E rather than at the left edge.
static int ui_kb_remap(int c, int from, int to) {
  int n = ui_kb_cols(from), m = ui_kb_cols(to);
  int o = (int)(((float)c + 0.5f) * (float)m / (float)n);
  return o < 0 ? 0 : o >= m ? m - 1 : o;
}

// ONE press, two devices: the pad's A and a click on a key are the same act,
// and a second copy of this switch is how the two drift apart.
static void ui_kb_press(int r, int c) {
  if (r < UI_KB_GROWS) {
    char t[2] = {UI_KB_ROW[r][c], 0};
    ui_name_key(t);
  } else {
    ui_name_key(c == 0 ? "back" : "enter");
  }
}

// The keyboard OWNS navigation while it is up, the same way an armed rebind capture
// does — a grid's four directions are its own, and handing them to the page's nearest-
// rect mover would step the cursor to whatever happens to be geometrically closest
// instead of to the next letter.
static void ui_kb_nav(int ev) {
  int n = ui_kb_cols(g_ui_kb_r);
  switch (ev) {
    case NAV_LEFT:  g_ui_kb_c = (g_ui_kb_c + n - 1) % n; break;
    case NAV_RIGHT: g_ui_kb_c = (g_ui_kb_c + 1) % n; break;
    case NAV_UP: {
      int r = (g_ui_kb_r + UI_KB_ROWS - 1) % UI_KB_ROWS;
      g_ui_kb_c = ui_kb_remap(g_ui_kb_c, g_ui_kb_r, r);
      g_ui_kb_r = r;
      break;
    }
    case NAV_DOWN: {
      int r = (g_ui_kb_r + 1) % UI_KB_ROWS;
      g_ui_kb_c = ui_kb_remap(g_ui_kb_c, g_ui_kb_r, r);
      g_ui_kb_r = r;
      break;
    }
    case NAV_OK: ui_kb_press(g_ui_kb_r, g_ui_kb_c); break;
    case NAV_BACK: ui_name_key("back"); break;
    default: break;    // LB/RB carry no meaning on a grid
  }
}

[[maybe_unused]] static const char *ui_page_name(ui_page_t page) {
  static const char *const NAME[UI_PAGE_COUNT] = {
    "root", "match", "settings", "controls", "dev"
  };
  return page >= 0 && page < UI_PAGE_COUNT ? NAME[page] : "root";
}

static void ui_route(ui_page_t page) {
  if (page < 0 || page >= UI_PAGE_COUNT) page = UI_PAGE_ROOT;
  if (g_ui_name_edit) ui_name_key("enter");
  g_ui_rebind = -1;
  g_ui_drag = 0;
  // THE RAIL REMEMBERS WHERE YOU WERE. Coming back from SETTINGS used to land the focus
  // on SINGLEPLAYER — the tile whose page's first row is the only control that can end
  // a running match, so "change one setting and come back" ended two presses from a
  // wiped scoreboard.
  if (g_ui_page == UI_PAGE_ROOT && page != UI_PAGE_ROOT) g_ui_root_focus = g_ui_focus;
  g_ui_page = page;
  g_ui_tab = (int)page;
  g_ui_focus = page == UI_PAGE_ROOT ? g_ui_root_focus : 0;
  nav_anchor_reset();   // the carried column belongs to the page that set it
  g_ui_arm = UI_ARM_NONE;
  g_focus_sw = 0;   // the focus ring snaps to its new page, no cross-page glide
}

// The pause reaches the audio timeline too (see g_audio_pause at the mixer):
// only a windowed session pauses, so only there may the mixer freeze — a
// harness mixdown must keep playing through a scripted `menu`.
static void ui_audio_pause(int on) {
#ifndef _WIN32
  if (g_headless) return;
#endif
  atomic_store_explicit(&g_audio_pause, on, memory_order_relaxed);
}

static void ui_close_menu(void) {
  if (g_ui_name_edit) ui_name_key("enter");
  g_menu_open = 0;
  g_ui_root_focus = 0;   // next session's rail is not the same rows
  // ...and the BOOT SCREEN is gone for the session.
  g_home = 0;
  // THE telemetry ui->sp edge (tele_mode): the boot menu is the only menu with no
  // session behind it, and this is the moment the player leaves it.
  g_tele_played = 1;
  g_ui_rebind = -1;
  g_ui_drag = 0;
  g_ui_arm = UI_ARM_NONE;
  pad_eat_held();  // the button that closed the menu must not act in the game
  ui_audio_pause(0);
  config_write(g_cfg_path, &g_cfg);
  platform_menu_changed();
}

static void ui_open_menu(void) {
  ui_route(UI_PAGE_ROOT);
  g_menu_open = 1;
  g_nav_n = 0;  // events queued while closed must not replay into the session
  // The audio timeline freezes with the SIM, so it follows the same predicate:
  // a networked session's world keeps ticking, so its audio must too (a paused
  // mid-reload would otherwise play its bolt cue into a frozen world — but
  // online the world is not frozen).
  if (!g_online) ui_audio_pause(1);
  platform_menu_changed();
}

// "BACK", from ESC and from pad B alike — ONE copy, because the two used to spell the
// same two-line rule out separately.
//
// THE ENTRY SCREEN AT BOOT HAS NOTHING BEHIND IT, so back is not a way out of it. It
// used to be: closing it cleared g_home and handed the player the DIRECTOR's match,
// whose bots have been really scoring into g_match (match_on_kill has no g_home term —
// only the END test is suppressed, by sp_frag_limit), whose roster is HOME_BOTS rather
// than sp_bots, and which has him standing at home_stage's corner holding whatever
// weapon the reel last swapped to. The rail is all destinations; one of them is how you
// leave. In a PAUSE there IS a session behind the menu, and back returns to it.
static void ui_on_back(void) {
  g_ui_arm = UI_ARM_NONE;   // back is "never mind" on every screen, and on the boot
                            // rail neither branch below runs to clear it
  if (g_ui_page != UI_PAGE_ROOT) { ui_route(UI_PAGE_ROOT); return; }
  // ...and `g_home` ALONE DOES NOT MEAN "no session" — the same trap upd_auto_tick's
  // guards spell out. `--connect` kicks a join at boot and leaves the menu up, so the
  // boot screen can be showing with `g_online` already set by the first snapshot. That
  // session IS behind the menu, and back is how the player reaches it.
  if (!g_home || g_online) ui_close_menu();
}

// ESC: finish name entry / cancel a rebind, else back, else open.
static void ui_on_escape(void) {
  if (g_ui_name_edit) ui_name_key("escape");
  else if (g_ui_rebind >= 0) g_ui_rebind = -1;
  else if (g_menu_open) ui_on_back();
  else ui_open_menu();
}

// Assign a captured key token to the action being rebound. If another action already
// uses the token, the two swap — no bind can silently go dead.
static void ui_assign_bind(const char *tok) {
  if (g_ui_rebind < 0) return;
  int is_pad = !strncmp(tok, "pad_", 4);
  if (is_pad != g_ui_rebind_pad) return;
  if (is_pad && !strcmp(tok, "pad_start")) return;
  char (*tbl)[12] = g_ui_rebind_pad ? g_cfg.padbind : g_cfg.bind;
  char old[12];
  snprintf(old, sizeof old, "%s", tbl[g_ui_rebind]);
  for (int i = 0; i < IN_COUNT; i++)
    if (i != g_ui_rebind && !strcmp(tbl[i], tok))
      snprintf(tbl[i], sizeof tbl[i], "%s", old);
  snprintf(tbl[g_ui_rebind], sizeof tbl[g_ui_rebind], "%s", tok);
  g_ui_rebind = -1;
}

// EVERY BIND BACK TO ITS COMPILED DEFAULT, for ONE device.
static void ui_binds_reset(int pad) {
  char (*tbl)[12] = pad ? g_cfg.padbind : g_cfg.bind;
  for (int i = 0; i < IN_COUNT; i++)
    snprintf(tbl[i], sizeof tbl[i], "%s",
             pad ? ACTION_DEF[i].pdflt : ACTION_DEF[i].dflt);
  g_ui_rebind = -1;
}

// Pad token -> menu label ("pad_lb" -> "LB", "none" -> "--").
static const char *pad_disp(const char *tok) {
  if (!strcmp(tok, "none")) return "--";
  for (int i = 0; i < PB_COUNT; i++)
    if (!strcmp(tok, PAD_BTN_DEF[i].tok)) return PAD_BTN_DEF[i].label;
  return tok;
}

enum { DRAG_SENSE = 1, DRAG_FOV, DRAG_CAP, DRAG_VOL, DRAG_BOTS, DRAG_FRAG,
       DRAG_ADS_SENSE,  // 7, the one id left before the MV block starts
       DRAG_MV0 = 8, DRAG_GP0 = 24, DRAG_WP0 = 32, DRAG_PAD0 = 56,
       // The first id past the last block, derived so widening PAD_DEF cannot
       // collide with it the way a hand-written 63 would.
       DRAG_BRIGHT = DRAG_PAD0 + PAD_COUNT };

// Register one interactive row (layout order) and draw the focus ring when the pad
// drives the menu.
static int ui_focus_row(float x, float y, float w, float h, int lr) {
  int fid = g_ui_nfocus++;
  int foc = fid == g_ui_focus;
  if (fid < UI_MAX_FOCUS) g_focus_r[fid] = (focus_rect_t){x, y, w, h, lr};
  if (foc && g_ui_pad_mode) {
    // The ring GLIDES: its rect is smoothed toward the focused row on the presentation
    // clock (fixed-step in the harness), which is what makes pad navigation read as one
    // cursor moving instead of four rects teleporting.
    if (g_focus_sw <= 0) {
      g_focus_sx = x; g_focus_sy = y; g_focus_sw = w; g_focus_sh = h;
    }
    // THE GLIDE HAS TO SETTLE INSIDE ONE REPEAT INTERVAL. At 24 a one-row step needs
    // ~12 rendered frames while PAD_NAV_REP repeats from 9 Hz upward, so a held d-pad
    // kept the ring permanently mid-flight: its hairline scrolling THROUGH the row
    // labels and no row ever reading as focused.
    g_focus_sx = ui_smooth(g_focus_sx, x, 60);
    g_focus_sy = ui_smooth(g_focus_sy, y, 60);
    g_focus_sw = ui_smooth(g_focus_sw, w, 60);
    g_focus_sh = ui_smooth(g_focus_sh, h, 60);
    // The ring rides ui_panel's 6-point chamfer path (TR/BL cut): it is the
    // loudest element on a settings page and was the one square peer left
    // beside the chamfered slab. Same wash, same hairline, same accent bar.
    {
      float fx2 = g_focus_sx - 3, fy2 = g_focus_sy - 1.5f;
      float fw2 = g_focus_sw + 6, fh2 = g_focus_sh + 3;
      float qx[6], qy[6];
      float c = ui_cham_path(fx2, fy2, fw2, fh2, qx, qy);
      ui_cham_fill(qx, qy, (const float[4]){1.000f, 0.549f, 0.102f, 0.08f});
      ui_cham_edge(qx, qy, 0.5f, 1.000f, 0.549f, 0.102f, 0.85f);
      ui_rect(fx2, fy2, 2, fh2 - c, C_BLAZE, 0.95f);
    }
  }
  return foc;
}

static int ui_nav_take(int focused, int act) {
  if (!focused || g_nav_act != act) return 0;
  g_nav_act = -1;
  return 1;
}

// ONE CURSOR AT A TIME. In pad mode the software cursor is not drawn, so a trackpad or
// a touch that left the pointer somewhere else lights a second control with nothing on
// screen to explain it — two cursors, one invisible.
#define UI_HOVER(cond) (!g_ui_pad_mode && (cond))
static float ui_slider(int id, float x, float y, float w, float v,
                       float lo, float hi, int click, float cx, float cy) {
  int hot = UI_HOVER(cx >= x - 4 && cx <= x + w + 4 && cy >= y && cy <= y + 13);
  if (click && hot && g_ui_rebind < 0) g_ui_drag = id;
  if (g_ui_drag == id) v = lo + f_clamp((cx - x) / w, 0, 1) * (hi - lo);
  float t = f_clamp((v - lo) / (hi - lo), 0, 1);
  float ky = y + 6;
  // Hard track: hairline with quarter ticks, BLAZE fill, a square knob.
  ui_rect(x, ky - 0.7f, w, 1.4f, C_BONE, 0.14f);
  for (int i = 1; i < 4; i++)
    ui_rect(x + w * 0.25f * (float)i, ky - 2.6f, 1, 2, C_BONE, 0.18f);
  ui_rect(x, ky - 0.7f, t * w, 1.4f, C_BLAZE, 0.9f);
  if (g_ui_drag == id || hot) ui_rect(x + t * w - 2, ky - 5, 4, 10, C_BLAZE, 1);
  else                        ui_rect(x + t * w - 1.7f, ky - 4.5f, 3.4f, 9, C_BONE, 0.95f);
  return v;
}

// Click-to-cycle option pill, same look and placement as ui_toggle's ON/OFF;
// the pill is sized to the longest option so nothing ever overflows it.
static int ui_cycle(const char *label, const char *const *opts, int n, int *idx,
                    float px, float pw, float y, int click, float cx, float cy) {
  int foc = ui_focus_row(px + 10, y - 1, pw - 20, 15, 1);
  int chg = 0;
  if (ui_nav_take(foc, NAV_LEFT)) { *idx = (*idx + n - 1) % n; chg = 1; }
  if (ui_nav_take(foc, NAV_RIGHT) || ui_nav_take(foc, NAV_OK)) {
    *idx = (*idx + 1) % n;
    chg = 1;
  }
  ui_text(label, px + 16, y + 3, 1, C_TEXT, 1);
  const char *tok = opts[*idx];
  float bw = ui_text_width("OFF", 1);
  for (int i = 0; i < n; i++) {
    float w = ui_text_width(opts[i], 1);
    if (w > bw) bw = w;
  }
  bw += 16;
  float bx = px + pw - 16 - bw;
  // THE WHOLE ROW IS THE TARGET, not the value chip at the far right: the stripe and
  // the focus ring are row-wide, so a click on the label did nothing at all — a
  // fingernail target on a handheld touchscreen.
  int hot = UI_HOVER(cx >= px + 10 && cx <= px + pw - 10 &&
                     cy >= y && cy < y + 14);
  ui_rrect_c(bx, y + 1, bw, 12, 0, C4(1, 1, 1, hot ? 0.16f : 0.07f),
             C4(1, 1, 1, hot ? 0.12f : 0.05f));
  // Dim is the OFF look, not the index-0 look: a cycle of real alternatives
  // (LINEAR/STANDARD/DYNAMIC, EASY/NORMAL/HARD) has no "off" state, and its
  // first option rendering as disabled reads as a bug.
  if (strcmp(tok, "OFF") != 0)
    ui_textb(tok, bx + (bw - ui_text_width(tok, 1)) * 0.5f, y + 4, 1, C_BONE, 1);
  else
    ui_text(tok, bx + (bw - ui_text_width(tok, 1)) * 0.5f, y + 4, 1, C_DIM, 1);
  if (click && hot && g_ui_rebind < 0) { *idx = (*idx + 1) % n; return 1; }
  return chg;
}

// A toggle IS a two-option cycle, and keeping it as its own copy of the pill widget is
// how the two drift: the font is monospace, so max("OFF","ON") is "OFF" and the box
// comes out the same width; `if (*idx)` reproduces `if (*val)`; `(*idx + 1) % 2` is
// `!*val`.
static int ui_toggle(const char *label, int *val, float px, float pw, float y,
                     int click, float cx, float cy) {
  static const char *const ONOFF[2] = {"OFF", "ON"};
  return ui_cycle(label, ONOFF, 2, val, px, pw, y, click, cx, cy);
}

// Right-aligned value pill: label on the left, a box at the panel's right edge holding
// `tok`. `active` = being edited, which fills the box with the accent and darkens the
// text.
static int ui_pill(const char *label, const char *tok, float px, float pw,
                   float y, int active, float minw, int click,
                   float cx, float cy) {
  int foc = ui_focus_row(px + 10, y - 1, pw - 20, 15, 0);
  ui_text(label, px + 16, y + 3, 1, C_TEXT, 1);
  float bw = ui_text_width(tok, 1) + 12;
  if (bw < minw) bw = minw;
  float bx = px + pw - 16 - bw;
  int hot = UI_HOVER(cx >= px + 10 && cx <= px + pw - 10 &&
                     cy >= y && cy < y + 14);   // the row, not the chip; see ui_cycle
  if (active) {
    // An armed capture breathes BLAZE — a live socket, not a filled box.
    float bl = 0.75f + 0.25f * sinf(g_ui_time * 6.0f);
    ui_rrect_c(bx, y + 1, bw, 12, 0, C4(C_BLAZE, 0.95f * bl), C4(C_BLAZE, 0.80f * bl));
    ui_textb(tok, bx + 6, y + 4, 1, 0.10f, 0.07f, 0.03f, 1);
  } else {
    ui_rrect_c(bx, y + 1, bw, 12, 0, C4(1, 1, 1, hot ? 0.16f : 0.07f),
               C4(1, 1, 1, hot ? 0.12f : 0.05f));
    ui_text(tok, bx + 6, y + 4, 1, C_TEXT, 1);
  }
  return (click && hot && g_ui_rebind < 0 && !g_ui_drag) ||
         ui_nav_take(foc, NAV_OK);
}

// Action row: left-aligned label with a chevron marker, quiet until hot — the direct-
// action-list grammar (never tiles).
static int ui_button(const char *label, float x, float y, float w, float h,
                     int click, float cx, float cy, int danger) {
  int foc = ui_focus_row(x, y, w, h, 0);
  int hot = UI_HOVER(cx >= x && cx <= x + w && cy >= y && cy <= y + h);
  float ty = y + (h - 7) * 0.5f;
  if (hot) {
    if (danger) {
      ui_rrect_c(x, y, w, h, 0, C4(C_THREAT, 0.95f), C4(0.85f, 0.20f, 0.15f, 0.95f));
      ui_text("\x01", x + 8, ty, 1, 1, 1, 1, 0.9f);
      ui_textb(label, x + 17, ty, 1, 1, 1, 1, 1);
    } else {
      ui_rrect_c(x, y, w, h, 0, C4(0.96f, 0.97f, 0.98f, 0.96f),
                 C4(0.86f, 0.89f, 0.92f, 0.96f));
      ui_text("\x01", x + 8, ty, 1, 0.05f, 0.06f, 0.08f, 0.9f);
      ui_textb(label, x + 17, ty, 1, 0.03f, 0.04f, 0.05f, 1);
    }
  } else {
    ui_rrect_c(x, y, w, h, 0, C4(1, 1, 1, 0.045f), C4(1, 1, 1, 0.03f));
    if (danger) {
      ui_rect(x, y, 2, h, C_THREAT, 0.8f);
      ui_text("\x01", x + 8, ty, 1, C_THREAT, 0.75f);
      ui_text(label, x + 17, ty, 1, C_TEXT, 0.92f);
    } else {
      ui_text("\x01", x + 8, ty, 1, C_HAZE, 0.8f);
      ui_text(label, x + 17, ty, 1, C_TEXT, 0.92f);
    }
  }
  return (click && hot && g_ui_rebind < 0) || ui_nav_take(foc, NAV_OK);
}

// Section eyebrow: small, wide-tracked, cool — a label and one hairline.
// Structure whispers; only data and focus get color.
static void ui_section(const char *title, float px, float pw, float y) {
  ui_text_ex(title, px + 16, y - 1, 0.8f, TXT_BOLD, 2.2f, C4(C_HAZE, 0.95f));
  float tw = ui_text_width_tr(title, 0.8f, 2.2f);
  ui_rect(px + 16 + tw + 8, y + 2, pw - 32 - tw - 8, 1, C_BONE, 0.10f);
}

// One tuning-parameter row: label, slider over the def's range, value.
// Values snap to the parameter's step and display in matching precision.
static float ui_param_row(const param_def_t *d, float *val, int drag_id,
                          float px, float y, int click, float cx, float cy) {
  char buf[16];
  int foc = ui_focus_row(px + 10, y - 1, 320, 15, 1);
  int touched = 0;
  if (ui_nav_take(foc, NAV_LEFT))  { *val = f_clamp(*val - d->step, d->lo, d->hi); touched = 1; }
  if (ui_nav_take(foc, NAV_RIGHT)) { *val = f_clamp(*val + d->step, d->lo, d->hi); touched = 1; }
  ui_text(d->label, px + 16, y + 3, 1, C_TEXT, 1);
  float v = ui_slider(drag_id, px + 150, y, 140, *val, d->lo, d->hi, click, cx, cy);
  // Snap to the step grid ONLY on interaction.
  float st = d->step;
  if (g_ui_drag == drag_id || touched) *val = roundf(v / st) * st;
  snprintf(buf, sizeof buf, st < 0.099f ? "%.2f" : st < 1.0f ? "%.1f" : "%.0f",
           (double)*val);
  // ONE value column: right-aligned on the chip edge every other row uses.
  ui_text(buf, px + 324 - ui_text_width(buf, 1), y + 3, 1, C_DIM, 1);
  return y + 15;
}

static float ui_mv_row(int i, float px, float y, int click, float cx, float cy) {
  return ui_param_row(&MV_DEF[i], &g_cfg.mv[i], DRAG_MV0 + i, px, y, click, cx, cy);
}

static float ui_gun_row(int i, float px, float y, int click, float cx, float cy) {
  return ui_param_row(&GP_DEF[i], &g_cfg.gun[i], DRAG_GP0 + i, px, y, click, cx, cy);
}

static int g_ui_wpn;  // which weapon the DEV/WEAPON view edits (WPN_AR / WPN_SR)

static float ui_wp_row(int i, float px, float y, int click, float cx, float cy) {
  return ui_param_row(&WP_DEF[i], &g_cfg.wp[g_ui_wpn][i], DRAG_WP0 + i,
                      px, y, click, cx, cy);
}

static float ui_pad_row(int i, float px, float y, int click, float cx, float cy) {
  return ui_param_row(&PAD_DEF[i], &g_cfg.pad[i], DRAG_PAD0 + i,
                      px, y, click, cx, cy);
}

// Horizontal sub-selector (device, STICKS/BUTTONS, MOVEMENT/WEAPON, weapon): one focus
// row, LEFT/RIGHT or A cycles, the mouse clicks a cell directly.
static void ui_subsel(const char *const *opts, int n, int *idx, float px,
                      float pw, float y, int click, float cx, float cy) {
  int foc = ui_focus_row(px + 10, y - 1, pw - 20, 15, 1);
  if (ui_nav_take(foc, NAV_LEFT)) *idx = (*idx + n - 1) % n;
  if (ui_nav_take(foc, NAV_RIGHT) || ui_nav_take(foc, NAV_OK))
    *idx = (*idx + 1) % n;
  float selw = (pw - 32) / (float)n;
  // Segmented control: one quiet container, the selected cell stark WHITE
  // with black bold text — same hard state grammar as the buttons.
  ui_rrect_c(px + 16, y - 1, pw - 32, 15, 0, C4(0, 0, 0, 0.30f), C4(0, 0, 0, 0.22f));
  for (int i = 0; i < n; i++) {
    float sx = px + 16 + (float)i * selw;
    int hot = UI_HOVER(cx >= sx && cx <= sx + selw && cy >= y && cy <= y + 13);
    float lw = ui_text_width(opts[i], 1);
    if (*idx == i) {
      ui_rrect_c(sx + 1, y, selw - 2, 13, 0, C4(0.96f, 0.97f, 0.98f, 0.96f),
                 C4(0.86f, 0.89f, 0.92f, 0.96f));
      ui_textb(opts[i], sx + (selw - lw) * 0.5f, y + 3, 1, 0.03f, 0.04f, 0.05f, 1);
    } else {
      if (hot) ui_rrect_c(sx + 1, y, selw - 2, 13, 0, C4(1, 1, 1, 0.10f),
                          C4(1, 1, 1, 0.07f));
      ui_text(opts[i], sx + (selw - lw) * 0.5f, y + 3, 1, C_HAZE, 1);
    }
    if (click && hot && g_ui_rebind < 0 && !g_ui_drag) *idx = i;
  }
}


// ---------------------------------------------------------------------------
// The BOOT SCREEN's own layout. Every other page is a FORM — a list of settings with a
// title over it — and this one is a poster: destinations and a camera, so it gets its
// own grammar rather than a variant of the panel's.
// ---------------------------------------------------------------------------

// Text that CANNOT run out of its element.
static void ui_text_fit(const char *t, float x, float y, float maxw, float scale,
                        float thr, float track, const float c[4]) {
  float sc = scale;
  for (int i = 0; i < 8 && ui_text_width_tr(t, sc, track * sc / scale) > maxw; i++)
    sc *= 0.94f;
  ui_text_ex(t, x, y, sc, thr, track * sc / scale, c);
}

#ifndef _WIN32
// Defined with the evdev pump in the Linux backend.
static void nat_pad_report(int frames);
#endif

// The boot screen's layout sits with the rest of the menu, but its director lives with
// the renderer — a camera is not a widget.
static player_t *local_player(void);
static const char *home_caption(float *fade);
static float home_dip(void);

// A tile. The machined slab ui_panel established, a label at the size the destination
// deserves, and a caption saying what pressing it does.
static int ui_tile(const char *label, const char *sub, float x, float y,
                   float w, float h, float ls, int click, float cx, float cy,
                   int danger) {
  int foc = ui_focus_row(x, y, w, h, 0);
  int hot = UI_HOVER(cx >= x && cx <= x + w && cy >= y && cy <= y + h) ||
            (foc && g_ui_pad_mode);
  // STATE BY VALUE, ON ONE GEOMETRY. The hovered tile and the resting tile must be the
  // SAME SHAPE — a tile that changes outline is a tile that moves, and six of them make
  // a rail that twitches.
  float lx = x + 13;
  float lh = ls * 7.0f;
  float ty = sub ? y + (h - lh - 9) * 0.5f : y + (h - lh) * 0.5f;
  float chevw = ls > 1.6f ? ls * 7.0f : 0.0f;
  (void)danger;
  ui_panel(x, y, w, h, 0, hot ? 1.0f : 0.92f);
  // NO BAR. The hover used to add a hairline down the leading edge on top of the wash,
  // and a bar is a bar whatever colour it is — six of them appearing and vanishing
  // under a moving cursor is the flicker the accent rules were removed to stop.
  if (hot) ui_panel_wash(x, y, w, h, 0.086f, 0.098f, 0.112f, 0.88f);
  ui_text_fit(label, lx, ty, w - 26 - chevw, ls, hot ? TXT_BLACK : TXT_BOLD,
              ls * 1.05f, C4(C_BONE, hot ? 1.0f : 0.86f));
  if (chevw > 0)
    ui_text_ex("\x01", x + w - 13 - ls * 5.0f, ty + lh * 0.14f, ls * 0.8f,
               TXT_BOLD, 0, C4(C_BONE, hot ? 0.85f : 0.40f));
  if (sub)
    ui_text_fit(sub, lx, ty + lh + 4, w - 26, 0.72f, TXT_REG, 1.35f,
                C4(C_HAZE, hot ? 0.95f : 0.75f));
  return (click && hot && g_ui_rebind < 0) || ui_nav_take(foc, NAV_OK);
}

// The HOME readout's one layout contract, shared by the draw and its headless proof.
// Keeping the clamp here means the proof cannot bless a rectangle different from the
// one players see.
#define HOME_RAIL_X 34.0f
#define HOME_RAIL_W 208.0f
typedef struct { float x, y, w, h; } home_readout_box_t;

static int ui_home_readout_box(float vw, float vh, home_readout_box_t *box) {
  float lim = HOME_RAIL_X + HOME_RAIL_W + 20.0f;
  float cw = 208, ch = 44;
  float cxr = vw - 34 - cw;
  if (cxr < lim) { cxr = lim; cw = vw - 34 - lim; }
  *box = (home_readout_box_t){cxr, vh - 40 - ch, cw, ch};
  return cw >= 96;                    // no room at all: the shot speaks for itself
}

static float ui_home_fit_width(const char *text, float maxw,
                               float *scale, float *track) {
  for (int i = 0; i < 8 && ui_text_width_tr(text, *scale, *track) > maxw; i++) {
    *scale *= 0.94f;
    *track *= 0.94f;
  }
  return ui_text_width_tr(text, *scale, *track);
}

// Descriptions, not trademarks. Compact calibre copy and one shadow per line stay
// within the HOME readout vertex budget at every required aspect ratio. The proof
// reads these same strings.
static const char *const HOME_WNAME[WPN_COUNT] = {
  "CARBINE", "SNIPER"
};
static const char *const HOME_WSPEC[WPN_COUNT] = {
  "5.56", ".338"
};

// An editorial lower third: information over the shot, never a destination.
static void ui_home_readout(float vw, float vh) {
  const player_t *lp = local_player();
  int wc = lp ? lp->wp.cur : WPN_AR;
  home_readout_box_t box;
  // At 4:3 the right corner is 160 units narrower than at 16:9;
  // ui_home_readout_box supplies the shared clamp and fallback that keep the block
  // off the QUIT tile.
  if (!ui_home_readout_box(vw, vh, &box)) return;
  float right = box.x + box.w;

  // OPEN, QUIET, RIGHT-ANCHORED. The existing floor vignette is the contrast
  // treatment and the small ink shadow keeps type readable over bright SAND. A short
  // rule is punctuation, not a boundary.
  float rule = fminf(box.w, 58.0f);
  ui_rect(right - rule, box.y, rule, 0.8f, C_BONE, 0.32f);

  float ns = 1.35f, nt = 1.5f;
  float nw = ui_home_fit_width(HOME_WNAME[wc], box.w, &ns, &nt);
  ui_text_sh(HOME_WNAME[wc], right - nw, box.y + 8, ns, TXT_BOLD, nt,
             C4(C_BONE, 1));

  float ss = 0.72f, st = 1.3f;
  float sw = ui_home_fit_width(HOME_WSPEC[wc], box.w, &ss, &st);
  ui_text_sh(HOME_WSPEC[wc], right - sw, box.y + 27, ss, TXT_REG, st,
             C4(C_HAZE, 0.9f));

  float f;
  const char *cap = home_caption(&f);
  if (cap) {
    float cs = 0.8f, ct = 2.4f;
    float capw = ui_home_fit_width(cap, box.w, &cs, &ct);
    ui_text_sh(cap, right - capw, box.y - 13, cs, TXT_BOLD, ct,
               C4(C_BONE, 0.92f * f));
  }
}

static void ui_home(int click, float cx, float cy) {
  float vw = g_ui_vw, vh = g_ui_vh;
  int dev = DEV_MENU && g_cfg.devmode;
  // THE CUT, and it goes down FIRST — over the shot, under the whole interface.
  float dip = g_home ? home_dip() : 0.0f;
  if (dip > 0.002f) ui_rect(0, 0, vw, vh, 0.008f, 0.010f, 0.016f, dip);
  // A graded scrim down the left edge only.
  float sk = g_home ? 0.86f : 0.55f;
  ui_hgrad(0, 0, vw * 0.62f, vh, C4(0.010f, 0.014f, 0.024f, sk),
           C4(0.010f, 0.014f, 0.024f, 0.0f));
  ui_vgrad(0, 0, vw, 54, C4(0.010f, 0.014f, 0.024f, 0.60f),
           C4(0.010f, 0.014f, 0.024f, 0.0f));
  // ...and a floor under the readout, which only exists while the camera does.
  if (g_home)
    ui_vgrad(0, vh * 0.62f, vw, vh * 0.38f, C4(0.010f, 0.014f, 0.024f, 0.0f),
             C4(0.010f, 0.014f, 0.024f, 0.62f));

  float rx = HOME_RAIL_X, rw = HOME_RAIL_W;
  ui_text_ex("SKILL", rx, 30, 3.1f, TXT_BOLD, 3.0f, C4(C_BONE, 1));
  float sw = ui_text_width_tr("SKILL", 3.1f, 3.0f);
  ui_text_ex("ISSUE", rx + sw + 11, 30, 3.1f, TXT_BOLD, 3.0f, C4(C_BLAZE, 1));
  ui_rect(rx, 58, rw, 1.2f, C_BONE, 0.30f);

  // The rail is bottom-anchored: the wordmark owns the top, the tiles sit where
  // the eye lands after it, and the block grows UPWARD when DEV TOOLS adds a
  // row so nothing under it ever moves.
  float th = 52, sh2 = 32, qh = 24, gap = 7;
  float rows = th + gap + sh2 + gap + sh2 + gap + qh + (dev ? sh2 + gap : 0);
  float y = vh * 0.80f - rows;
  float hw = (rw - gap) * 0.5f;

  // ASCII only: the HARDLINE atlas is printable ASCII plus its icon block, so
  // a typographic middot is two bytes of nothing and the caption silently
  // loses its separators.
  static const char *const DIFF[3] = {"EASY", "NORMAL", "HARD"};
  char sub[48];
  snprintf(sub, sizeof sub, "DEATHMATCH / %d BOT%s / %s / %d FRAGS",
           g_cfg.sp_bots, g_cfg.sp_bots == 1 ? "" : "S",
           DIFF[g_cfg.sp_diff < 0 || g_cfg.sp_diff > 2 ? 1 : g_cfg.sp_diff],
           g_cfg.sp_frag);
  // TWO DESTINATIONS, AND NEITHER OF THEM IS "GO BACK".
  if (ui_tile("SINGLEPLAYER", sub, rx, y, rw, th, 2.1f, click, cx, cy, 0)) {
    g_ui_arm = UI_ARM_NONE;
    ui_route(UI_PAGE_MATCH);
  }
  y += th + gap;

  {
    int st = g_cl.state;
    int busy = st == NC_CONNECTING || st == NC_JOINING || st == NC_PLAYING;
    const char *ml = busy ? "CONNECTING" : "MULTIPLAYER";
    const char *ms = busy ? "PLEASE WAIT"
                   : st == NC_FAILED
                   ? (g_cl.reject == NR_FULL  ? "ALL ARENAS FULL"
                    : g_cl.reject == NR_PROTO ? "UPDATE REQUIRED"
                    : g_cl.reject == NR_WORLD ? "ARENA MISMATCH"
                    // NR_LOST is the only one of these that speaks about a match that
                    // was actually PLAYED: it is set by app_frame after the teardown,
                    // and it is why the tile the player lands back on is not silent.
                    : g_cl.reject == NR_LOST  ? "CONNECTION LOST - REJOIN"
                                              : "SERVER DOWN - TRY LATER")
                   : "QUICK JOIN";
    // ...and online the same slot is the way OUT of the match, which is the one
    // destination a live session has and a boot screen does not.
    //
    // ONE PRESS, AND THE UNDO IS THE SAME PRESS AGAIN. Leaving a server ends nothing:
    // the destination IS this rail, with the tile under the cursor already reading
    // QUICK JOIN. A confirm on the way out only made a player who wanted OUT press
    // twice — which is the argument the QUIT tile below now makes for itself. What
    // still arms on this rail is the round's scoreboard and a bind table (g_ui_arm).
    if (g_online) {
      if (ui_tile("LEAVE MATCH", "BACK TO THE ENTRY SCREEN",
                  rx, y, rw, sh2, 1.25f, click, cx, cy, 1)) {
        g_ui_arm = UI_ARM_NONE;
        leave_match();   // ...which lands on this rail; nothing closes the menu here
      }
    } else if (ui_tile(ml, ms, rx, y, rw, sh2, 1.25f, click, cx, cy, 0) && !busy) {
      g_ui_arm = UI_ARM_NONE;
      // THE MENU STAYS OPEN FOR THE WHOLE HANDSHAKE. This tile is the one
      // place a failed join is reported (the FAILED messages above), so
      // closing here would hide the verdict — the local bots keep playing and
      // read as multiplayer. Success closes the menu instead: app_frame does
      // it the moment the first snapshot flips g_online.
      start_connect(g_cfg.mp_host, g_cfg.mp_port);
    }
  }
  y += sh2 + gap;

  if (ui_tile("SETTINGS", "VIDEO / AUDIO", rx, y, hw, sh2, 1.25f, click, cx, cy, 0))
    ui_route(UI_PAGE_SETTINGS);
  if (ui_tile("CONTROLS", "KEYS / PAD", rx + hw + gap, y, hw, sh2, 1.25f,
              click, cx, cy, 0))
    ui_route(UI_PAGE_CONTROLS);
  y += sh2 + gap;

  if (dev) {
    if (ui_tile("DEV TOOLS", "TUNING", rx, y, rw, sh2, 1.25f, click, cx, cy, 1))
      ui_route(UI_PAGE_DEV);
    y += sh2 + gap;
  }

  // ONE PRESS, AND THE CONFIG GOES WITH IT. This tile is not the only way out of the
  // process and never was — the title bar's close and Alt+F4 end it without asking,
  // and both write the config on the way out (see the native backends' teardown), so a
  // confirm here only taxed the player who came to the rail to leave.
  if (ui_tile("QUIT", NULL, rx, y, rw, qh, 1.1f, click, cx, cy, 1)) {
    config_write(g_cfg_path, &g_cfg);
    g_ui_quit = 1;
  }
  y += qh + gap;

  int ust = atomic_load(&g_upd_state);
  int offer = ust == UPD_UPDATE || ust == UPD_RECUT;
  // AN UPDATE IS A ROW ONLY WHEN THERE IS SOMETHING LEFT TO DO WITH IT: once the boot
  // screen applies them by itself, the row exists for the verdict that arrived LATE,
  // after the player left the reel.
  if (offer || ust == UPD_APPLYING) {
    // The label is the STATE: arming stores UPD_APPLYING and the frame that is
    // already in flight draws it, which is the whole reason the apply is two
    // phases (see upd_arm_apply).
    if (ui_tile(offer ? "UPDATE & RESTART" : "UPDATING...",
                offer ? g_upd_remote_date : NULL, rx, y, rw, qh, 1.1f,
                click, cx, cy, 0) && offer) {
      g_ui_arm = UI_ARM_NONE;
      config_write(g_cfg_path, &g_cfg);
      upd_arm_apply();
    }
    y += qh + gap;
  }
  y += 12 - gap;

  // The pad grammar, and only in pad mode: the tiles say what they do, so a
  // permanent "CLICK TO PLAY" would be noise, while a controller has no visible
  // affordance at all until something names its buttons. NO CONFIRM GRAMMAR ON THIS
  // RAIL: nothing here arms any more, and ui_route clears the latch on the way in, so
  // the line would be unreachable. The panel pages own it (see ui_menu's footer).
  if (g_ui_pad_mode) {
    const char *ph = g_nav_kbd ? "ARROWS MOVE   ENTER OK" : "A OK   D-PAD MOVE";
    // THE HINT LIVES IN THE GAP, NOT ABOVE IT. A stroke overshoots its box by about a
    // unit, so a line asked for one unit above the tile block's bottom edge draws two
    // units of cap height INSIDE the QUIT tile.
    ui_text_ex(ph, rx, y - 8.25f, 0.75f, TXT_REG, 1.5f, C4(C_HAZE, 0.75f));
  }
  ui_text_ex(g_cfg.name, rx, y, 0.85f, TXT_BOLD, 1.6f, C4(C_BONE, 0.72f));
  float nw = ui_text_width_tr(g_cfg.name, 0.85f, 1.6f);
  // THE VERSION LIVES HERE AND NOWHERE ELSE. It used to be printed under the match-end
  // hero as well, as a full VERSION/commit/check-age status line — on the one screen
  // whose subject is who won, in a game that updates itself.
  char vl[40];
  snprintf(vl, sizeof vl, "V %s%s", BUILD_VERSION,
           ust == UPD_FAILED ? " - UPDATE CHECK FAILED" : "");
  ui_text_ex(vl, rx + nw + 12, y + 1, 0.7f, TXT_REG, 1.0f, C4(C_HAZE, 0.5f));

  // The state chip, on the same predicate the tick reads, so it can never say PAUSED
  // while the world runs.
  int chip = 1;
#ifndef _WIN32
  chip = !g_headless;   // a proof shot carries no label at all
#endif
  if (chip && !g_home) {
    const char *cl = session_pauses() ? "PAUSED" : g_online ? "LIVE" : NULL;
    if (cl) {
      float clw = ui_text_width_tr(cl, 0.85f, 1.8f);
      ui_rect(rx, 65, clw + 16, 15, 0.02f, 0.03f, 0.04f, 0.55f);
      ui_text_ex(cl, rx + 8, 69, 0.85f, TXT_BOLD, 1.8f,
                 C4(C_BONE, session_pauses() ? 0.85f : 1.0f));
      // ...AND THE WAY BACK IN, because no TILE says it any more.
      const char *back = g_ui_pad_mode && !g_nav_kbd ? "B RETURNS TO MATCH"
                                                     : "ESC RETURNS TO MATCH";
      ui_text_ex(back, rx + clw + 24, 69, 0.7f, TXT_REG, 1.0f, C4(C_HAZE, 0.7f));
    }
  }
  if (g_home) ui_home_readout(vw, vh);
}

// THE ON-SCREEN KEYBOARD, drawn LAST and over a scrim, because it is a modal and the
// scrim is what says so.
static void ui_keyboard(int click, float cx, float cy) {
  const float kw = 340, pad = 13, gap = 3, keyh = 22, nameh = 26;
  float cellw = (kw - 2 * pad - (UI_KB_COLS - 1) * gap) / UI_KB_COLS;
  float actw  = (kw - 2 * pad - gap) * 0.5f;
  float body  = 12 + 18 + nameh + 12 + UI_KB_ROWS * (keyh + gap) - gap + 13;
  float kx = g_ui_vw * 0.5f - kw * 0.5f;
  float ky = g_ui_vh * 0.5f - body * 0.5f;
  if (ky < 4) ky = 4;

  // The page behind stays visible as CONTEXT and stops competing for the eye.
  // 0.80 rather than the 0.62 this started at: the settings panel is a stack of
  // sliders whose right-hand value column is bright type on a dark plate, and
  // at the lighter scrim those numbers read straight through the modal and sat
  // in the gaps between the keys.
  ui_rect(0, 0, g_ui_vw, g_ui_vh, 0.012f, 0.018f, 0.030f, 0.80f);
  ui_panel(kx, ky, kw, body, 0, 1);

  float y = ky + 12;
  ui_textb("PLAYER NAME", kx + pad, y, 1.1f, C_BONE, 0.92f);
  y += 18;

  // The field.
  ui_rrect_c(kx + pad, y, kw - 2 * pad, nameh, 0,
             C4(1, 1, 1, 0.055f), C4(1, 1, 1, 0.030f));
  {
    float sc = 2.2f, trk = 0.9f;
    for (int i = 0; i < 8 &&
         ui_text_width_tr(g_cfg.name, sc, sc * trk) > kw - 2 * pad - 30; i++)
      sc *= 0.94f;
    float tx = kx + pad + 11, ty = y + (nameh - sc * 7.0f) * 0.5f;
    float tw = ui_text_width_tr(g_cfg.name, sc, sc * trk);
    ui_text_ex(g_cfg.name, tx, ty, sc, TXT_BOLD, sc * trk, C4(C_BONE, 1));
    // A STATIC caret, deliberately not a blinking one: g_ui_time advances once
    // per RENDERED frame, so a blink would make every harness `uiframe` and
    // `shot` of this screen depend on how many frames happened to precede it.
    ui_rect(tx + tw + (tw > 0 ? 3.0f : 0.0f), ty, 2, sc * 7.0f, C_BONE, 0.55f);
  }
  y += nameh + 12;

  for (int r = 0; r < UI_KB_ROWS; r++) {
    int n = ui_kb_cols(r);
    float w = r < UI_KB_GROWS ? cellw : actw;
    for (int c = 0; c < n; c++) {
      float x = kx + pad + (float)c * (w + gap);
      int over = cx >= x && cx <= x + w && cy >= y && cy <= y + keyh;
      // Hovering ALSO moves the cursor, so putting the mouse down and picking the pad
      // back up does not teleport the highlight to wherever the grid was last
      // navigated.
      if (over && !g_ui_pad_mode) { g_ui_kb_r = r; g_ui_kb_c = c; }
      int lit = over || ((r == g_ui_kb_r && c == g_ui_kb_c) && g_ui_pad_mode);
      char lab[8];
      if (r < UI_KB_GROWS) { lab[0] = UI_KB_ROW[r][c]; lab[1] = '\0'; }
      else snprintf(lab, sizeof lab, "%s", c == 0 ? "DELETE" : "DONE");
      if (lit)
        ui_rrect_c(x, y, w, keyh, 0, C4(0.96f, 0.97f, 0.98f, 0.96f),
                   C4(0.86f, 0.89f, 0.92f, 0.96f));
      else
        ui_rrect_c(x, y, w, keyh, 0, C4(1, 1, 1, 0.050f), C4(1, 1, 1, 0.032f));
      float ls = r < UI_KB_GROWS ? 1.5f : 1.0f;
      float lw = ui_text_width(lab, ls);
      ui_text_ex(lab, x + (w - lw) * 0.5f, y + (keyh - ls * 7.0f) * 0.5f, ls,
                 lit ? TXT_BLACK : TXT_BOLD,
                 0, lit ? C4(0.03f, 0.04f, 0.05f, 1) : C4(C_BONE, 0.88f));
      if (click && over) ui_kb_press(r, c);
    }
    y += keyh + gap;
  }
}
