// =============================================================================
// ui/menu.inc  —  ui_menu and its page branches, rebind capture, nav_move.
// Proof: uistat, nav. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: g_ui_arm is an enum, not a bool, so RESTART and the two bind
// RESET rows cannot cross-confirm each other; ui_arm_take is the one
// chokepoint, and nav_move clears it. QUIT and LEAVE MATCH are NOT on that
// list — both are one press, each argued at its own tile in ui/settings.inc.
// SETTINGS splits GAME | VIDEO behind the shared sub-select. VIDEO offers a
// quality profile, AA and shadows; the profile owns AO, reflections, glow and
// dust. Film look is independent. Profiles compare effective AA so equivalent
// requests stay one choice on devices that round their sample counts.
// CUSTOM is derived from the six quality keys and is never written as a profile.
// uistat cannot name the settings sub-page: it sees it only as the focus-row
// count (8 on GAME, 11 on VIDEO), so a row added to either page moves that
// number and the proof's expectation moves with it.
// =============================================================================
static float ui_page_binds_kb (float px, float pw, float y, int click, float cx, float cy);
static float ui_page_settings_game (float px, float pw, float y, int click,
                                    float cx, float cy);
static float ui_page_settings_video(float px, float pw, float y, int click,
                                    float cx, float cy);
static float ui_page_binds_pad(float px, float pw, float y, int click, float cx, float cy);
static float ui_page_match    (float px, float pw, float y, int click, float cx, float cy);
static float ui_page_dev      (float px, float pw, float y, int click, float cx, float cy);

static void ui_menu(void) {
  static int prev_down;
  static float pmx = -1e9f, pmy;
  // DEV is a runtime opt-in behind the existing compile-time and config gate.
  int dev = DEV_MENU && g_cfg.devmode;
  if (g_ui_page == UI_PAGE_DEV && !dev) ui_route(UI_PAGE_ROOT);

  // Pad navigation first, so the layout below already reflects the page and focus these
  // events produced.
  for (int i = 0; i < g_nav_n; i++) {
    int ev = g_nav_q[i];
    g_ui_pad_mode = 1;
    if (g_ui_kb) {
      ui_kb_nav(ev);
    } else if (g_ui_name_edit) {
      // A PAD EVENT RAISES THE KEYBOARD THE FIELD SHOULD HAVE HAD. g_ui_kb is decided
      // by the device that ARMED the field, and on a handheld the touchscreen and the
      // trackpad are cursor mode — so tapping the field and then reaching for the stick
      // used to hit a footer reading "TYPE ON KEYBOARD", which is the exact dead end
      // ui_keyboard exists to abolish.
      if (ev == NAV_BACK || ev == NAV_OK) {
        ui_name_key("enter");
      } else if (!g_nav_kbd) {
        g_ui_kb = 1;
        g_ui_kb_r = g_ui_kb_c = 0;
        ui_kb_nav(ev);
      }
    } else if (g_ui_rebind >= 0) {
      if (ev == NAV_BACK) g_ui_rebind = -1;
    } else switch (ev) {
      // All four directions through ONE mover: up/down and left/right differ in the
      // axis they search, not in what they mean.
      case NAV_LEFT: case NAV_RIGHT:
        if (nav_focus_lr()) { g_nav_act = ev; break; }
        [[fallthrough]];
      case NAV_UP: case NAV_DOWN:
        nav_move(ev);
        g_ui_arm = UI_ARM_NONE;
        break;
      // LB/RB and PgUp/PgDn no longer cycle hidden global tabs. Visible local
      // selectors on Controls and Dev own all lateral navigation.
      case NAV_TABL: case NAV_TABR:
        g_ui_arm = UI_ARM_NONE;
        break;
      case NAV_BACK: ui_on_back(); break;
      default: g_nav_act = ev; break;
    }
  }
  g_nav_n = 0;
  if (!g_menu_open) { g_nav_act = -1; return; }  // B closed it this frame

  int click = g_ui_mdown && !prev_down;
  prev_down = g_ui_mdown;
  // The on-screen keyboard is a MODAL, so it owns the frame's click.
  int kbclick = click;
  if (g_ui_kb) click = 0;
  if (!g_ui_mdown) g_ui_drag = 0;
  float cx = g_ui_cx / g_uis, cy = g_ui_cy / g_uis;
  // Real mouse motion or a click hands the menu back to the cursor.
  if (pmx > -1e8f && (fabsf(cx - pmx) > 0.5f || fabsf(cy - pmy) > 0.5f))
    g_ui_pad_mode = 0;
  if (click) g_ui_pad_mode = 0;
  pmx = cx; pmy = cy;
  g_ui_nfocus = 0;
  g_ui_tab = (int)g_ui_page;

  // THE ENTRY SCREEN IS NOT A PAGE OF THIS PANEL — boot OR pause.
  if (g_ui_page == UI_PAGE_ROOT) {
    ui_home(click, cx, cy);
  } else {

  // Only a PAUSING session dims the world — a networked session keeps world and HUD
  // readable behind the panel (you are standing in the open).
  if (session_pauses()) {
    float base = g_ui_backdrop ? 0.20f : 0.45f;
    ui_vgrad(0, 0, g_ui_vw, g_ui_vh, C4(0.016f, 0.024f, 0.043f, base + 0.12f),
             C4(0.016f, 0.024f, 0.043f, base));
  }

  // Every page shares one top anchor.
  #define UI_FOOTER_H 24.0f
  #define UI_PANEL_MAX 348.0f
  #define UI_BIND_ROW  14.0f   // see the keyboard binding page's loop
  // Self-sizing across FRAMES now, not within one: a rounded panel cannot be bottom-
  // patched the way the old flat quad was (ui_rect_bottom rewrote three vertices;
  // ui_panel is a hundred).
  static float ph_page[UI_PAGE_COUNT] = {276, 177, 344, 344, 344};
  ui_page_t drawn_page = g_ui_page;
  float pw = 340, px = g_ui_vw * 0.5f - pw * 0.5f;
  float ph = ph_page[drawn_page];
  // THE TOP IS ANCHORED, ONLY THE BOTTOM MOVES. Centring each page's own height would
  // slide the title by up to 118 px.
  float py = (g_ui_vh - UI_PANEL_MAX) * 0.5f;
  if (py < 6) py = 6;
  ui_panel(px, py, pw, ph, 0, 1);
  // ROOT is not one of these any more: this panel draws FORMS, and the entry
  // screen is not a form. Its slot stays so the array indexes by page.
  static const char *const PAGE_TITLE[UI_PAGE_COUNT] = {
    "", "SINGLEPLAYER", "SETTINGS", "CONTROLS", "DEV TOOLS"
  };
  ui_textb(PAGE_TITLE[drawn_page], px + 16, py + 9, 1.6f, C_BONE, 1);
  ui_rect(px + 16, py + 28, pw - 32, 1, C_BONE, 0.14f);
  // The label reads the SAME predicate the tick does, so it can never say PAUSED while
  // the world ticks or LIVE while it is frozen.
  int label = 1;
#ifndef _WIN32
  label = !g_headless;   // a proof shot carries no label at all
#endif
  if (label && session_pauses()) {
    float lw2 = ui_text_width_tr("PAUSED", 0.85f, 1.2f);
    ui_rrect_line(px + pw - 24 - lw2, py + 9, lw2 + 12, 12, 0, 0.5f, C_BLAZE, 0.7f);
    ui_text_ex("PAUSED", px + pw - 18 - lw2, py + 12, 0.85f, TXT_REG, 1.2f,
               C4(C_BLAZE, 0.95f));
  } else if (label && g_online) {
    float lw2 = ui_text_width_tr("LIVE", 0.85f, 1.2f);
    float bl = 0.7f + 0.3f * sinf(g_ui_time * 4.0f);
    ui_rrect_c(px + pw - 24 - lw2 - 8, py + 9, lw2 + 20, 12, 0,
               C4(C_THREAT, 0.9f), C4(0.80f, 0.18f, 0.13f, 0.9f));
    ui_diskn(px + pw - 24 - lw2 - 1, py + 15, 2, 10, C4(1, 1, 1, bl));
    ui_textb("LIVE", px + pw - 18 - lw2 + 2, py + 12, 0.85f, 1, 1, 1, 1);
  }

  float y = py + 34;
  if (drawn_page == UI_PAGE_CONTROLS) {
    static const char *const CONTROL[2] = {"KEYBOARD + MOUSE", "GAMEPAD"};
    ui_subsel(CONTROL, 2, &g_ui_control, px, pw, y, click, cx, cy);
    y += 18;
  } else if (drawn_page == UI_PAGE_SETTINGS) {
    // The CONTROLS sub-select, on the one other page that had outgrown a single
    // list: WHO IS PLAYING AND HOW IT FEELS on one side, WHAT THE PICTURE COSTS on
    // the other. Same widget, same 18 px, so the split reads as page grammar the
    // player already learned rather than as a second invention.
    static const char *const SETSUB[2] = {"GAME", "VIDEO"};
    ui_subsel(SETSUB, 2, &g_ui_set_sub, px, pw, y, click, cx, cy);
    y += 18;
  }
  if      (drawn_page == UI_PAGE_CONTROLS && g_ui_control == 0)
                                           y = ui_page_binds_kb (px, pw, y, click, cx, cy);
  else if (drawn_page == UI_PAGE_SETTINGS && g_ui_set_sub == 0)
                                       y = ui_page_settings_game (px, pw, y, click, cx, cy);
  else if (drawn_page == UI_PAGE_SETTINGS)
                                       y = ui_page_settings_video(px, pw, y, click, cx, cy);
  else if (drawn_page == UI_PAGE_CONTROLS) y = ui_page_binds_pad(px, pw, y, click, cx, cy);
  else if (drawn_page == UI_PAGE_MATCH)    y = ui_page_match    (px, pw, y, click, cx, cy);
  else if (drawn_page == UI_PAGE_DEV)      y = ui_page_dev      (px, pw, y, click, cx, cy);

  if (drawn_page == UI_PAGE_DEV) {
    y += 8;
    if (ui_button("RESET CURRENT", px + 16, y, pw - 32, 16, click, cx, cy, 0)) {
      if (g_ui_dev_sub == 0)
        for (int i = 0; i < MV_COUNT; i++) g_cfg.mv[i] = MV_DEF[i].def;
      else if (g_ui_dev_sub == 1) {
        for (int i = 0; i < GP_COUNT; i++) g_cfg.gun[i] = GP_DEF[i].def;
        for (int i = 0; i < WP_COUNT; i++)
          g_cfg.wp[g_ui_wpn][i] = WP_DEFAULTS[g_ui_wpn][i];
      } else
        for (int i = PAD_AS_FRIC; i <= PAD_AS_DIST; i++)
          g_cfg.pad[i] = PAD_DEF[i].def;
    }
    y += 20;
  }

  // Every focused subpage owns exactly one visible way back. NAV_BACK and ESC
  // route through the same bookkeeping without manufacturing extra controls.
  if (drawn_page != UI_PAGE_ROOT) {
    y += 8;
    if (ui_button("BACK", px + 16, y, pw - 32, 16, click, cx, cy, 0))
      ui_route(UI_PAGE_ROOT);
    y += 18;
  }

  // Close the panel under its page content: store this frame's measured height for the
  // NEXT frame's rounded panel (see the cache comment above).
  {
    float want = f_clamp((y - py) + UI_FOOTER_H, 150.0f,
                         g_ui_vh - py - 4.0f);
    ph_page[drawn_page] = want;
    ph = want;
  }

  // LAST, over everything, and over its own scrim — see ui_keyboard.
  if (g_ui_kb) ui_keyboard(kbclick, cx, cy);

  // Hints: the menu tells the player its own grammar, and the line follows the STATE —
  // while a capture is armed, the grammar it advertises ("B BACK") would be wrong for a
  // pad capture, where B is a bindable button and only START cancels.
  const char *h = NULL;
  if (g_ui_rebind >= 0) {
    // The device term matches the branch below: keyboard arrows drive the
    // menu in pad mode too (g_nav_kbd), and during a KEYBOARD capture the
    // advertised "B CANCEL" would BIND the B key — ESC is the cancel there.
    h = g_ui_pad_mode && !g_nav_kbd
      ? (g_ui_rebind_pad ? "PRESS A BUTTON   START CANCEL"
                         : "PRESS A KEY   B OR ENTER CANCEL")
      : (g_ui_rebind_pad ? "PRESS A PAD BUTTON   ESC CANCEL"
                         : "PRESS A KEY OR MOUSE BUTTON   ESC OR ENTER CANCEL");
  } else if (g_ui_pad_mode) {
    // The grammar follows the DEVICE: arrow keys drive the same focus system
    // (the Steam desktop layout emulates the dpad as arrows, and a desktop
    // keyboard user has no A or LB at all), so the hint speaks whichever
    // language the last navigation event actually arrived in.
    // No `g_ui_kb` arm here: the grid is raised only under `!g_nav_kbd` and lives only
    // as long as the name field, whose key path returns before anything can set
    // g_nav_kbd — so a keyboard-grammar hint for it is unreachable, and its promise
    // ("ARROWS MOVE") was false anyway, since arrow keysyms carry no single-char token
    // and are discarded. A keyboard player types into the field directly.
    if (g_nav_kbd)
      h = g_ui_name_edit ? "TYPE A-Z 0-9   ENTER DONE"
        : g_ui_arm       ? "ENTER CONFIRMS   ESC CANCELS"
                         : "ARROWS MOVE   ENTER OK   LEFT/RIGHT ADJUST   ESC BACK";
    else
      // The keyboard's own grammar, and B is the one line that has to be spelt out: it
      // DELETES here rather than backing out, which is the one place in this menu where
      // B does not mean "leave".
      h = g_ui_kb        ? "A SELECT   B DELETE   START DONE"
        : g_ui_name_edit ? "TYPE ON KEYBOARD   A/B DONE"
        : g_ui_arm       ? "A CONFIRMS   B CANCELS"
                         : "A OK   < > ADJUST   B BACK";
  } else if (g_ui_kb) {
    h = "CLICK A KEY   OR TYPE   ENTER DONE";
  } else if (g_ui_name_edit) {
    // Cursor mode: the accent-filled pill alone does not say typing works.
    h = "TYPE A-Z 0-9   ENTER DONE";
  } else {
    // The footer band exists on EVERY frame — an empty reserved strip reads
    // as a layout mistake, and the grammar chip doubles as the exit sign.
    h = g_ui_arm ? "CLICK AGAIN CONFIRMS   ESC CANCELS"
                      : "ESC BACK";
  }
  if (h) {
    // The hint line renders as key CHIPS, split on the triple-space grouping
    // the strings already carry — the wording stays the single copy above.
    char tmp[128];
    snprintf(tmp, sizeof tmp, "%s", h);
    const char *part[6];
    int np = 0;
    char *cur2 = tmp;
    while (np < 6 && cur2) {
      part[np++] = cur2;
      char *sep = strstr(cur2, "   ");
      if (sep) { *sep = '\0'; cur2 = sep + 3; } else cur2 = NULL;
    }
    float cwd[6], tw_all = 0;
    for (int i = 0; i < np; i++) {
      cwd[i] = ui_text_width(part[i], 0.9f) + 12;
      tw_all += cwd[i] + (i ? 6.0f : 0.0f);
    }
    float hx = px + (pw - tw_all) * 0.5f, hy = py + ph - 17;
    for (int i = 0; i < np; i++) {
      ui_rrect_c(hx, hy, cwd[i], 13, 0, C4(0, 0, 0, 0.30f), C4(0, 0, 0, 0.22f));
      ui_text(part[i], hx + 6, hy + 3, 0.9f, C_HAZE, 1);
      hx += cwd[i] + 6;
    }
  }

  }   // ...end of the panel path; the boot screen took the other branch.
  if (!g_ui_pad_mode) {
    // Software cursor — a graticule cross with an open centre, so the pixel under the
    // point stays visible.
    for (int i = 0; i < 4; i++) {
      float dx = (i == 0) - (i == 1), dy = (i == 2) - (i == 3);
      ui_capsule(cx + dx * 2.0f, cy + dy * 2.0f, cx + dx * 6.0f, cy + dy * 6.0f,
                 1.1f, 0, 0, 0, 0.8f);
      ui_capsule(cx + dx * 2.0f, cy + dy * 2.0f, cx + dx * 6.0f, cy + dy * 6.0f,
                 0.6f, 1, 1, 1, 1);
    }
    ui_rect(cx - 0.8f, cy - 0.8f, 1.6f, 1.6f, 1, 1, 1, 0.95f);  // machined dot
  }

  // The frame's focus-row census: wrap-around in next frame's preamble uses
  // it, and a page/selector switch that shrank the list must not leave focus dangling.
  g_ui_nfocus_last = g_ui_nfocus;
  g_focus_rn = g_ui_nfocus < UI_MAX_FOCUS ? g_ui_nfocus : UI_MAX_FOCUS;
  if (g_ui_focus >= g_ui_nfocus) {
    g_ui_focus = g_ui_nfocus > 0 ? g_ui_nfocus - 1 : 0;
    nav_anchor_reset();   // a shrunk list moved the focus, not the player
  }
  g_nav_act = -1;
}

static float ui_page_binds_kb(float px, float pw, float y, int click, float cx, float cy) {
  ui_section("BINDINGS", px, pw, y);
  y += 14;
  // UI_BIND_ROW, not the 15 every other list uses: IN_COUNT rows plus the
  // header and Back/hint block do not fit a 360-unit screen at 15, and what
  // that looked like was the last bind drawn straight through the permanent
  // version line — both illegible, on the row a player is most likely to be
  // rebinding last. 14 fits both bind pages with the tightest one (GAMEPAD ->
  // BUTTONS, which also carries the local selector) landing 1 px clear.
  for (int i = 0; i < IN_COUNT; i++, y += UI_BIND_ROW) {
    // zebra bridges the dead middle between label and key chip
    if ((i & 1) == 0) ui_rect(px + 12, y, pw - 24, 13, C_BONE, 0.03f);
    int armed = g_ui_rebind == i && !g_ui_rebind_pad;
    const char *tok = armed ? "PRESS KEY" : g_cfg.bind[i];
    if (ui_pill(ACTION_DEF[i].label, tok, px, pw, y, armed, 0, click, cx, cy)) {
      g_ui_rebind = i;
      g_ui_rebind_pad = 0;
    }
  }
  y += 3;
  // A RESET WIPES EVERY BIND ON THE TAB, and it sat one d-pad step below the last
  // bind row with no arm at all — a pad player walking down the list to the end
  // pressed it. Two presses, the same latch and the same CONFIRM vocabulary as QUIT /
  // LEAVE / RESTART. The THREAT fill stays off: that treatment is for ending the
  // process, the connection or the round, and a config wipe is recoverable.
  if (ui_button(g_ui_arm == UI_ARM_RSTKB ? "CONFIRM RESET" : "RESET KEYS",
                px + 16, y, pw - 32, 15, click, cx, cy, 0))
    if (ui_arm_take(UI_ARM_RSTKB)) ui_binds_reset(0);
  y += 18;
  // (The ROOT page is not drawn by this panel any more — it is the tile screen, boot or
  // pause; see ui_home.
  return y;
}

// Quality profiles own render cost. Film grain/vignette, brightness and FOV are
// independent picture preferences, preserved when the profile changes. HIGH
// matches config_defaults; device-equivalent AA requests match the same profile.
typedef struct { int msaa, shadows, ssao, ssr, bloom, dust; } gfx_preset_t;
static const gfx_preset_t GFX_PRESET[3] = {
  {0, 1, 0, 0, 0, 0},   // PERFORMANCE
  {2, 1, 1, 0, 1, 1},   // BALANCED
  {8, 2, 1, 1, 1, 1},   // HIGH
};

// A hand-edited quality key or a manual override reads CUSTOM. Merely viewing
// the menu never normalizes or overwrites the user's config.
static int gfx_preset_index(void) {
  for (int i = 0; i < 3; i++) { const gfx_preset_t *g = &GFX_PRESET[i];
    if (msaa_effective(g_cfg.msaa) == msaa_effective(g->msaa) &&
        g_cfg.shadows == g->shadows && g_cfg.ssao == g->ssao &&
        g_cfg.ssr == g->ssr && g_cfg.bloom == g->bloom && g_cfg.dust == g->dust) return i; }
  return 3;
}

static float ui_page_settings_game(float px, float pw, float y, int click,
                                   float cx, float cy) {
  // IDENTITY FIRST. The name field used to sit on the Singleplayer page, which is where
  // a MATCH is configured — but the name is not match property: it is who you are, it
  // persists across sessions, and online it is what every other player reads on the
  // scoreboard and in the killfeed.
  ui_section("PLAYER", px, pw, y);
  y += 14;
  {
    // Click it, type A-Z/0-9, backspace deletes, enter/esc done.
    char shown[16];
    // The pill's trailing '-' is its caret.
    if (g_ui_name_edit && !g_ui_kb) snprintf(shown, sizeof shown, "%s-", g_cfg.name);
    else                            snprintf(shown, sizeof shown, "%s", g_cfg.name);
    // 40 px minimum: a two-letter name must not shrink the field to a stub.
    if (ui_pill("NAME", shown, px, pw, y, g_ui_name_edit, 40, click, cx, cy)) {
      if (g_ui_name_edit) {
        ui_name_key("enter");
      } else {
        g_ui_name_edit = 1;
        // THE PAD BRINGS ITS OWN KEYBOARD. Gated on the device that armed the
        // field, not on the platform: a desktop player driving this menu with
        // the ARROW KEYS (g_nav_kbd — the Steam desktop layout emulates the
        // dpad as arrows) is sitting at a keyboard and should just type, while
        // a pad on a handheld has nothing to type with. Cursor mode types too.
        g_ui_kb = g_ui_pad_mode && !g_nav_kbd;
        g_ui_kb_r = g_ui_kb_c = 0;
      }
    }
    y += 15;
  }
  y += 4;
  ui_section("MOUSE", px, pw, y);
  y += 14;
  // Both of these hand-inlined ui_param_row's body three rows above Singleplayer
  // that already uses it, repeating the px+16 / px+130 / width-100 / px+240 /
  // y+15 layout constants the helper owns. Step 0.01 selects the same "%.2f".
  // `def` is unread here — ui_param_row takes only label/lo/hi/step — but a wrong
  // number in the file is a wrong number, so these carry config_defaults' own values.
  { param_def_t sd = {"", "SENSITIVITY", 1.0f, 0.05f, 2.0f, 0.01f};
    y = ui_param_row(&sd, &g_cfg.sense, DRAG_SENSE, px, y, click, cx, cy); }
  // The mouse's own ADS multiplier, directly under the sensitivity it multiplies — the
  // pad page has had this pair since it existed and the mouse page had only half of it.
  { param_def_t ad = {"", "ADS SENS", 1.0f, 0.10f, 2.0f, 0.05f};
    y = ui_param_row(&ad, &g_cfg.ads_sense, DRAG_ADS_SENSE, px, y, click, cx, cy); }

  y += 4;
  ui_section("AUDIO", px, pw, y);
  y += 14;
  {
    float pct = g_cfg.vol * 100.0f;
    param_def_t vd = VOL_DEF;
    vd.label = "VOLUME";
    vd.lo = 0; vd.hi = 100; vd.step = 5;
    y = ui_param_row(&vd, &pct, DRAG_VOL, px, y, click, cx, cy);
    g_cfg.vol = pct * 0.01f;
  }

  y += 4;
  // NOT graphics quality, which is why these two did not follow the rest of the old
  // GRAPHICS section onto VIDEO: neither costs a frame or changes how the picture is
  // built. One is a HUD element and the other is where the camera stands — both are
  // "how I want to play", the question this side of the sub-select answers.
  ui_section("VIEW", px, pw, y);
  y += 14;
  ui_toggle("SHOW FPS", &g_cfg.show_fps, px, pw, y, click, cx, cy);
  y += 15;
  ui_toggle("3RD PERSON", &g_cfg.third_person, px, pw, y, click, cx, cy);
  y += 15;
  return y;
}

static float ui_page_settings_video(float px, float pw, float y, int click,
                                    float cx, float cy) {
  char buf[16];
  ui_section("DISPLAY", px, pw, y);
  y += 14;
  // Presentation settings remain independent of render quality.
  if (ui_toggle("FULLSCREEN", &g_cfg.fullscreen, px, pw, y, click, cx, cy))
    platform_fullscreen_changed();
  y += 15;
  if (ui_toggle("VSYNC", &g_cfg.vsync, px, pw, y, click, cx, cy))
    platform_vsync_changed();
  y += 15;

  // FPS cap: 30..500 in steps of 10; the right end of the track means OFF.
  // Not a param row (the OFF sentinel), so it registers its focus row itself.
  int capfoc = ui_focus_row(px + 10, y - 1, 320, 15, 1);
  ui_text("FPS CAP", px + 16, y + 3, 1, C_TEXT, 1);
  // OFF is one step PAST the top of the band, and the detent that reads it back sits
  // between the last real value and that step. Four coupled literals, so they are named
  // once: lower the band and forget the detent and either 500 or OFF becomes
  // unreachable — on the one control in this menu that has already shipped an
  // inversion bug (see below).
  #define FPS_CAP_LO   30
  #define FPS_CAP_TOP  500
  #define FPS_CAP_OFF  (FPS_CAP_TOP + 10)   // the slider's own top: the OFF sentinel
  #define FPS_CAP_DET  (FPS_CAP_TOP + 5)    // ...and the midpoint that reads it back
  float cap = g_cfg.fps_cap ? (float)g_cfg.fps_cap : (float)FPS_CAP_OFF;
  // A preserved out-of-range value (fps_cap 700 parses fine) enters the
  // slider's band BEFORE the step is applied: stepped first, LEFT went
  // 700 -> 690 -> clamp 510 -> the OFF sentinel — i.e. "lower the cap"
  // produced "no cap at all", the exact inversion of the input.
  cap = f_clamp(cap, FPS_CAP_LO, FPS_CAP_OFF);
  int captouch = 0;
  if (ui_nav_take(capfoc, NAV_LEFT))  { cap -= 10; captouch = 1; }
  if (ui_nav_take(capfoc, NAV_RIGHT)) { cap += 10; captouch = 1; }
  cap = f_clamp(cap, FPS_CAP_LO, FPS_CAP_OFF);
  cap = ui_slider(DRAG_CAP, px + 150, y, 140, cap, FPS_CAP_LO, FPS_CAP_OFF, click, cx, cy);
  // Written back only on interaction — same rule as ui_param_row's snap: a
  // hand-edited fps_cap 700 (the parser allows up to 1000) must not become
  // OFF because the menu was merely opened over it.
  if (captouch || g_ui_drag == DRAG_CAP)
    g_cfg.fps_cap = cap >= FPS_CAP_DET ? 0 : (int)(roundf(cap / 10) * 10);
  if (g_cfg.fps_cap) snprintf(buf, sizeof buf, "%d", g_cfg.fps_cap);
  else               snprintf(buf, sizeof buf, "OFF");
  ui_text(buf, px + 324 - ui_text_width(buf, 1), y + 3, 1, C_DIM, 1);
  y += 15;

  y += 4;
  ui_section("QUALITY", px, pw, y);
  y += 14;
  {
    // CUSTOM is displayed only for manual settings. Cycling always applies a
    // profile: forward from CUSTOM reaches PERFORMANCE, backward reaches HIGH.
    static const char *const PRESET_OPTS[4] = {"PERFORMANCE", "BALANCED", "HIGH", "CUSTOM"};
    int pi = gfx_preset_index();
    if (ui_cycle("PROFILE", PRESET_OPTS, pi == 3 ? 4 : 3, &pi,
                 px, pw, y, click, cx, cy)) {
      const gfx_preset_t *g = &GFX_PRESET[pi];
      g_cfg.msaa = g->msaa; g_cfg.shadows = g->shadows; g_cfg.ssao = g->ssao;
      g_cfg.ssr  = g->ssr;  g_cfg.bloom   = g->bloom;   g_cfg.dust = g->dust;
    }
    y += 15;
  }
  // Offer one row value per actual sample count. The request stays a portable
  // config value; existing aliases remain untouched until the player interacts.
  {
    char labels[4][24];
    const char *opts[4] = {"FXAA"};
    int requests[4] = {0}, samples[4] = {0}, n = 1, mi = 0;
    for (int i = 1; i < 4; i++) {
      int actual = R.msaa_samples[i], seen = 0;
      for (int j = 0; j < n; j++) if (samples[j] == actual) {
        seen = 1;
        // Save the named level when available, rather than a rounded alias,
        // so a chosen 4x stays a 4x request if the config moves to another GPU.
        if (actual == (1 << i)) requests[j] = 1 << i;
      }
      if (seen) continue;
      samples[n] = actual;
      requests[n] = 1 << i;
      snprintf(labels[n], sizeof labels[n], "MSAA %dX", actual);
      opts[n] = labels[n];
      n++;
    }
    for (int i = 0; i < n; i++)
      if (samples[i] == msaa_effective(g_cfg.msaa)) mi = i;
    if (ui_cycle("ANTI-ALIAS", opts, n, &mi, px, pw, y, click, cx, cy))
      g_cfg.msaa = requests[mi];
  }
  y += 15;
  // Three states, not a flag: the map's SIZE is the middle rung, so the row writes
  // g_cfg.shadows directly and shadow_update rebuilds the map on the next frame.
  static const char *const SHADOW_OPTS[3] = {"OFF", "NORMAL", "HIGH"};
  ui_cycle("SHADOWS", SHADOW_OPTS, 3, &g_cfg.shadows, px, pw, y, click, cx, cy);
  y += 15;
  ui_text("PROFILE SETS LIGHTING AND ATMOSPHERE.", px + 16, y + 2, 0.8f, C_DIM, 1);
  y += 12;

  y += 4;
  ui_section("PICTURE", px, pw, y);
  y += 14;
  { param_def_t fd = {"", "FOV", 100.0f, 60.0f, 120.0f, 1.0f};
    y = ui_param_row(&fd, &g_cfg.fov, DRAG_FOV, px, y, click, cx, cy); }
  {
    // Stored 0.5..1.5, shown 50..150 % — VOLUME's pattern. WRITTEN BACK ONLY WHEN
    // THE ROW MOVED: 100 * 0.01f is not 1.0f in float, so an unconditional assign
    // would walk brightness a little further from its default every frame the page
    // is merely drawn.
    float pct0 = g_cfg.brightness * 100.0f, pct = pct0;
    param_def_t bd = {"", "BRIGHTNESS", 100.0f, 50.0f, 150.0f, 5.0f};
    y = ui_param_row(&bd, &pct, DRAG_BRIGHT, px, y, click, cx, cy);
    if (pct != pct0) g_cfg.brightness = pct * 0.01f;
  }
  static const char *const LOOK_OPTS[2] = {"CLEAR", "FILMIC"};
  ui_cycle("LOOK", LOOK_OPTS, 2, &g_cfg.grain, px, pw, y, click, cx, cy);
  y += 15;
  ui_text("FILMIC ADDS GRAIN AND DARKER CORNERS.", px + 16, y + 2, 0.8f, C_DIM, 1);
  y += 12;
  return y;
}

static float ui_page_binds_pad(float px, float pw, float y, int click, float cx, float cy) {
    // Gamepad: deadzones and aim feel on one page, the button layout on the other.
    static const char *const PADSUB[2] = {"STICKS + AIM", "BUTTONS"};
    ui_subsel(PADSUB, 2, &g_ui_pad_sub, px, pw, y, click, cx, cy);
    y += 18;
    if (g_ui_pad_sub == 0) {
      ui_section("STICKS", px, pw, y);
      y += 14;
      y = ui_pad_row(PAD_DZ_L, px, y, click, cx, cy);
      y = ui_pad_row(PAD_DZ_R, px, y, click, cx, cy);

      y += 4;
      ui_section("AIM", px, pw, y);
      y += 14;
      ui_cycle("RESPONSE CURVE", PAD_CURVE_OPTS, 3, &g_cfg.pad_curve, px, pw, y, click, cx, cy);
      y += 15;
      // One line of what the selected curve DOES — a bare three-way cycle makes the
      // feature undiscoverable by name alone.
      static const char *const CURVE_DESC[3] = {
        "1:1 - STICK MAPS STRAIGHT TO TURN",
        "FINE AIM NEAR CENTRE - FAST EDGES",
        "QUICK START - FINE MID - FAST EDGES",
      };
      ui_text(CURVE_DESC[g_cfg.pad_curve], px + 16, y, 1, C_DIM, 1);
      y += 12;
      y = ui_pad_row(PAD_SENS, px, y, click, cx, cy);
      y = ui_pad_row(PAD_ADS_SENS, px, y, click, cx, cy);
      ui_toggle("INVERT Y", &g_cfg.pad_inv_y, px, pw, y, click, cx, cy);
      y += 15;
      ui_toggle("AIM ASSIST", &g_cfg.pad_assist, px, pw, y, click, cx, cy);
      y += 17;
      // Three states, because "no pad" and "Steam is sitting on the pad" have different
      // fixes and only the player can apply either.
      const char *st = g_pad.connected ? "PAD CONNECTED"
                     : g_pad_denied    ? "PAD HELD BY STEAM - PICK GAMEPAD LAYOUT"
                                       : "NO PAD DETECTED";
      if (g_pad.connected)
        ui_text(st, px + (pw - ui_text_width(st, 1)) * 0.5f, y, 1, C_DIM, 1);
      else
        ui_text(st, px + (pw - ui_text_width(st, 1)) * 0.5f, y, 1, C_ACCENT, 1);
      y += 7;    // past the status line: `y` is what sizes the panel
    } else {
      // Movement rows LAST: the stick moves you, so FORWARD/BACK/LEFT/RIGHT
      // are legitimately "--" — leading with four unbound rows reads as a
      // broken screen, and the section title says where movement actually is.
      static const int PAD_ROW[IN_COUNT] = {
        IN_JUMP, IN_CROUCH, IN_LEAN_L, IN_LEAN_R, IN_FIRE, IN_ADS, IN_RELOAD,
        IN_SWAP, IN_SLOT1, IN_SLOT2, IN_SCORE, IN_VIEW,
        IN_FWD, IN_BACK, IN_LEFT, IN_RIGHT,
      };
      // The pad status belongs on THIS page too — a player arming a PRESS BTN capture
      // that can never complete while Steam holds the pad deserves to be told here, not
      // one sub-page away.
      ui_section(g_pad.connected  ? "BUTTONS"
                 : g_pad_denied   ? "BUTTONS - PAD HELD BY STEAM"
                                  : "BUTTONS - NO PAD DETECTED",
                 px, pw, y);
      y += 14;
      // The four movement rows are GONE from this page, not merely last: the stick
      // moves you, and four dead "--" chips read as a broken screen.
      for (int k = 0; k < IN_COUNT - 4; k++, y += UI_BIND_ROW) {
        int i = PAD_ROW[k];
        if ((k & 1) == 0) ui_rect(px + 12, y, pw - 24, 13, C_BONE, 0.03f);
        int armed = g_ui_rebind == i && g_ui_rebind_pad;
        const char *tok = armed ? "PRESS BTN" : pad_disp(g_cfg.padbind[i]);
        if (ui_pill(ACTION_DEF[i].label, tok, px, pw, y, armed, 0, click, cx, cy)) {
          g_ui_rebind = i;
          g_ui_rebind_pad = 1;
        }
      }
      y += 2;
      ui_text_ex("MOVE + AIM LIVE ON THE STICKS", px + 16, y, 0.8f, TXT_REG, 1.0f,
                 C4(C_HAZE, 0.8f));
      y += 11;
      // Its own arm value, not the keyboard tab's: ui_subsel switches tab without
      // clearing g_ui_arm, so one shared value would let an arm taken here confirm
      // the other tab's reset.
      if (ui_button(g_ui_arm == UI_ARM_RSTPAD ? "CONFIRM RESET" : "RESET BUTTONS",
                    px + 16, y, pw - 32, 15, click, cx, cy, 0))
        if (ui_arm_take(UI_ARM_RSTPAD)) ui_binds_reset(1);
      y += 17;
    }
  return y;
}

static float ui_page_match(float px, float pw, float y, int click, float cx, float cy) {
    // SINGLEPLAYER — the mode's own page, and since the entry screen stopped carrying a
    // PLAY tile this is also where a match is started.
    y += 4;
    // ONE BUTTON, THREE LABELS, AND ONLY ONE OF THEM ASKS TWICE. Online this page's
    // subject — the LOCAL match — is not the one being played, so the row is the way
    // out of the server and it goes where the rail's LEAVE MATCH tile goes: the entry
    // screen, one press, never into a local round nobody asked to start. At the boot
    // screen there is no round to lose either. A RESTART is the one that ends one.
    int rarm = g_ui_arm == UI_ARM_RESTART;
    int one_press = g_online || g_home;
    int threat = g_online || !g_home;   // the two labels that END a session wear it
    const char *ml = g_online ? "LEAVE MATCH"
                   : g_home   ? "START MATCH"
                   : rarm     ? "CONFIRM RESTART" : "RESTART MATCH";
    if (ui_button(ml, px + 16, y, pw - 32, 18, click, cx, cy, threat)) {
      if (one_press || ui_arm_take(UI_ARM_RESTART)) {
        g_ui_arm = UI_ARM_NONE;
        if (g_online) leave_match();      // ...which opens the rail itself
        else { g_match.restart_req = 1;   // serviced by the next sim tick
               ui_close_menu(); }
      }
    }
    y += 24;
    ui_section("RULES", px, pw, y);
    y += 14;
    ui_text("MODE", px + 16, y + 3, 1, C_TEXT, 1);
    {  // every enum value wears the chip, even a read-only one
      float mw2 = ui_text_width("DEATHMATCH", 1) + 16;
      ui_rrect_c(px + pw - 16 - mw2, y + 1, mw2, 12, 0, C4(1, 1, 1, 0.07f),
                 C4(1, 1, 1, 0.05f));
      ui_textb("DEATHMATCH", px + pw - 16 - mw2 + 8, y + 4, 1, C_BONE, 1);
    }
    y += 15;
    {
      float v = (float)g_cfg.sp_bots;
      y = ui_param_row(&SP_BOTS_DEF, &v, DRAG_BOTS, px, y, click, cx, cy);
      g_cfg.sp_bots = (int)v;
    }
    static const char *const DIFF_OPTS[3] = {"EASY", "NORMAL", "HARD"};
    ui_cycle("DIFFICULTY", DIFF_OPTS, 3, &g_cfg.sp_diff, px, pw, y, click, cx, cy);
    y += 15;
    {
      float v = (float)g_cfg.sp_frag;
      y = ui_param_row(&SP_FRAG_DEF, &v, DRAG_FRAG, px, y, click, cx, cy);
      g_cfg.sp_frag = (int)v;
    }
  return y;
}

static float ui_page_dev(float px, float pw, float y, int click, float cx, float cy) {
    // DEV — the developer tuning workbench: every knob a PLAYER never touches (movement
    // feel, weapon feel).
    static const char *const DEVSUB[3] = {"MOVEMENT", "WEAPON", "ASSIST"};
    ui_subsel(DEVSUB, 3, &g_ui_dev_sub, px, pw, y, click, cx, cy);
    y += 18;
    if (g_ui_dev_sub == 0) {
    ui_section("SPEED", px, pw, y);
    y += 14;
    y = ui_mv_row(MV_RUN, px, y, click, cx, cy);
    y = ui_mv_row(MV_CROUCH_SP, px, y, click, cx, cy);

    y += 4;
    ui_section("FEEL", px, pw, y);
    y += 14;
    y = ui_mv_row(MV_RESPONSE_MS, px, y, click, cx, cy);

    y += 4;
    ui_section("AIR", px, pw, y);
    y += 14;
    y = ui_mv_row(MV_JUMP_H, px, y, click, cx, cy);
    y = ui_mv_row(MV_AIR_CTL, px, y, click, cx, cy);

    y += 4;
    ui_section("SLIDE", px, pw, y);
    y += 14;
    y = ui_mv_row(MV_SLIDE_BOOST, px, y, click, cx, cy);
    y = ui_mv_row(MV_SLIDE_LEN, px, y, click, cx, cy);

    } else if (g_ui_dev_sub == 1) {
    // Weapon selector: same editor, per-weapon parameter set underneath.
    const char *wopts[WPN_COUNT];
    for (int i = 0; i < WPN_COUNT; i++) wopts[i] = WPN_DEF[i].name;
    ui_subsel(wopts, WPN_COUNT, &g_ui_wpn, px, pw, y, click, cx, cy);
    y += 18;

    ui_section("GLOBAL", px, pw, y);
    y += 14;
    y = ui_gun_row(GP_ACTION_SPREAD, px, y, click, cx, cy);
    y = ui_gun_row(GP_RECENTER, px, y, click, cx, cy);

    y += 4;
    ui_section("RECOIL", px, pw, y);
    y += 14;
    y = ui_wp_row(WP_RECOIL_UP, px, y, click, cx, cy);
    y = ui_wp_row(WP_RECOIL_SIDE, px, y, click, cx, cy);

    y += 4;
    ui_section("SPREAD", px, pw, y);
    y += 14;
    y = ui_wp_row(WP_HIP_SPREAD, px, y, click, cx, cy);

    y += 4;
    ui_section("HANDLING", px, pw, y);
    y += 14;
    y = ui_wp_row(WP_RPM, px, y, click, cx, cy);
    y = ui_wp_row(WP_ADS_MS, px, y, click, cx, cy);
    y = ui_wp_row(WP_ADS_FOV, px, y, click, cx, cy);
    } else {
    // ASSIST — the friction-only aim-assist workbench (see PAD_DEF's PAD_AS_* comment
    // for what each knob means).
    ui_section("AIM ASSIST", px, pw, y);
    y += 14;
    y = ui_pad_row(PAD_AS_FRIC, px, y, click, cx, cy);
    y = ui_pad_row(PAD_AS_BUBBLE, px, y, click, cx, cy);
    y = ui_pad_row(PAD_AS_DIST, px, y, click, cx, cy);
    }
  return y;
}

// Scoreboard — hold the SCOREBOARD bind (default TAB).
static void ui_scoreboard(void) {
  // ENT_FOREACH, not `1 + nbots`: idx[i] = i assumed a DENSE id space — humans
  // on 21+ simply vanished from the board while empty bot slots showed as 0/0 rows.
  int n = 0;
  int idx[MAX_ENTS];
  ENT_FOREACH(e) idx[n++] = e;
  for (int i = 1; i < n; i++) {  // insertion sort: kills desc, deaths asc
    int v = idx[i], j = i;
    while (j > 0 && (g_match.kills[idx[j - 1]] < g_match.kills[v] ||
                     (g_match.kills[idx[j - 1]] == g_match.kills[v] &&
                      g_match.deaths[idx[j - 1]] > g_match.deaths[v]))) {
      idx[j] = idx[j - 1];
      j--;
    }
    idx[j] = v;
  }
  // A FULL ROSTER DOES NOT FIT, and it must not be allowed to try. 28 entities at 11
  // units plus the header is 354 against a 360-unit screen, and the winner banner
  // pushes the panel down another 40 — so the bottom rows were drawn off the viewport
  // entirely.
  float top = g_match.over ? 96.0f : 24.0f;
  int fit = (int)((g_ui_vh - top - 12.0f - 46.0f) / 11.0f);
  if (fit < 1) fit = 1;
  int shown = n < fit ? n : fit;
  int hidden = n - shown;
  // Online the panel grows a PING column (the row data arrives via NP_SCORES
  // at 4 Hz); offline the layout stays byte-identical to the historical board.
  int show_ping = g_online;
  float pw = show_ping ? 336 : 300,
        ph = 42 + (float)shown * 11 + 8 + (hidden ? 10.0f : 0.0f);
  float px = g_ui_vw * 0.5f - pw * 0.5f;
  // Centred on the REAL virtual height — 180 is only the centre when the
  // height is a multiple of 360 (the same trap the settings panel documents:
  // the Deck's 800 px viewport is 400 virtual units tall).
  float py = g_ui_vh * 0.5f - ph * 0.5f;
  if (py < top) py = top;
  ui_panel(px, py, pw, ph, 0, 1);
  // ONE PROCESS, MANY ARENAS: online the board says WHICH one, because
  // "am I in the same arena as my friend" is otherwise unanswerable from
  // inside the game — every lobby shares the host and the port.
  char title[32];
  if (g_online && g_cl.lobbies > 1)
    snprintf(title, sizeof title, "DEATHMATCH \x05 ARENA %d/%d",
             (g_cl.lobby + 1) & 31, g_cl.lobbies & 31);
  else snprintf(title, sizeof title, "DEATHMATCH");
  ui_textb(title, px + 12, py + 9, 1.2f, C_BONE, 1);
  char sub[28];
  // "N PLAYERS · M BOTS" makes 1-human-vs-7-bots legible instead of sad: the player
  // knows exactly what he fights, and a second human arriving is an event.
  int nh = 0, nb = 0;
  ENT_FOREACH(e) { if (human_of_ent(e) >= 0) nh++; else nb++; }
  if (nh > 1) snprintf(sub, sizeof sub, "%d PLAYERS \x05 %d BOTS", nh & 63, nb & 63);
  else        snprintf(sub, sizeof sub, "FIRST TO %d",
                       g_online && g_net_fraglimit ? g_net_fraglimit
                                                   : g_cfg.sp_frag);
  ui_text_ex(sub, px + pw - 12 - ui_text_width_tr(sub, 0.9f, 0.8f), py + 10, 0.9f,
             TXT_REG, 0.8f, C4(C_HAZE, 1));
  ui_rect(px + 12, py + 24, pw - 24, 1, C_BONE, 0.14f);
  float y = py + 30;
  // Numeric columns are right-aligned to a per-column edge — the rank number already
  // was, and left-aligned digits left ragged edges ("100%" overshot its header while
  // "0%" fell short).
  float xn = px + 34, xk = px + 180, xd = px + 208, xkd = px + 244, xa = px + 288,
        xp = px + 324;
  ui_text_ex("K", xk - ui_text_width_tr("K", 0.85f, 0.8f), y, 0.85f, TXT_REG, 0.8f,
             C4(C_HAZE, 1));
  ui_text_ex("D", xd - ui_text_width_tr("D", 0.85f, 0.8f), y, 0.85f, TXT_REG, 0.8f,
             C4(C_HAZE, 1));
  ui_text_ex("K/D", xkd - ui_text_width_tr("K/D", 0.85f, 0.8f), y, 0.85f, TXT_REG,
             0.8f, C4(C_HAZE, 1));
  ui_text_ex("ACC", xa - ui_text_width_tr("ACC", 0.85f, 0.8f), y, 0.85f, TXT_REG,
             0.8f, C4(C_HAZE, 1));
  if (show_ping)
    ui_text_ex("PING", xp - ui_text_width_tr("PING", 0.85f, 0.8f), y, 0.85f,
               TXT_REG, 0.8f, C4(C_HAZE, 1));
  y += 10;
  // A human is MARKED, not a bot: there are one or two humans and twenty bot names, so
  // the rarer mark reads as a badge — the chevron glyph, tinted; other humans go
  // SIGNAL, the local player keeps his BLAZE highlight bar.
  int humans_present = nh;   // the same walk two lines up; one count, not two
  for (int i = 0; i < shown; i++) {
    int e = idx[i];
    char buf[16];
    int is_human = human_of_ent(e) >= 0;
    int marked = is_human && humans_present > 1;   // only mark when it means smth
    if (e == g_local_ent) {
      ui_rrect_c(px + 6, y - 2, pw - 12, 11, 0,
                 C4(C_BLAZE, 0.16f), C4(C_BLAZE, 0.10f));
      ui_rect(px + 6, y - 2, 2, 11, C_BLAZE, 0.95f);
    } else if ((i & 1) == 0) {
      ui_rect(px + 6, y - 2, pw - 12, 11, C_BONE, 0.03f);
    }
    snprintf(buf, sizeof buf, "%d", i + 1);
    ui_text_ex(buf, px + 26 - ui_text_width(buf, 0.9f), y + 0.5f, 0.9f, TXT_REG, 0,
               e == g_local_ent ? C4(C_BLAZE, 1) : C4(C_HAZE, 0.8f));
    float nx = xn + (marked ? 8.0f : 0.0f);
    if (e == g_local_ent) {
      if (marked) ui_text("\x01", xn, y, 1, C_BLAZE, 1);
      ui_textb(ent_name(e), nx, y, 1, C_BLAZE, 1);
    } else if (marked) {
      ui_text("\x01", xn, y, 1, C_SIGNAL, 1);
      ui_text(ent_name(e), nx, y, 1, C_SIGNAL, 1);
    } else {
      ui_text(ent_name(e), xn, y, 1, C_BONE, 1);
    }
    snprintf(buf, sizeof buf, "%d", g_match.kills[e]);
    ui_textb(buf, xk - ui_text_width(buf, 1), y, 1, C_BONE, 1);
    snprintf(buf, sizeof buf, "%d", g_match.deaths[e]);
    ui_text(buf, xd - ui_text_width(buf, 1), y, 1, C_HAZE, 1);
    snprintf(buf, sizeof buf, "%.1f", (double)g_match.kills[e] /
             (double)(g_match.deaths[e] ? g_match.deaths[e] : 1));
    ui_text(buf, xkd - ui_text_width(buf, 1), y, 1, C_HAZE, 1);
    snprintf(buf, sizeof buf, "%d%%", g_match.shots[e] ?
             g_match.hits[e] * 100 / g_match.shots[e] : 0);
    ui_text(buf, xa - ui_text_width(buf, 1), y, 1, C_HAZE, 1);
    if (show_ping) {
      // Humans carry a measured conn RTT; bots are simulated in-process and a
      // number there would be an invented statistic — they get a dash.
      if (is_human) snprintf(buf, sizeof buf, "%d", g_net_ping[e]);
      else          snprintf(buf, sizeof buf, "-");
      ui_text(buf, xp - ui_text_width(buf, 1), y, 1, C_HAZE, 1);
    }
    y += 11;
  }
  if (hidden) {
    char more[24];
    snprintf(more, sizeof more, "+%d MORE", hidden);
    ui_text(more, px + 12, y + 1, 0.9f, C_HAZE, 1);
  }
}

// MSAA render target — rebuilt lazily whenever the setting or framebuffer size changes,
// so the menu toggle takes effect on the next frame without a platform hook.
static void msaa_update(void) {
  if (R.msaa_cfg == g_cfg.msaa && R.msaa_w == R.width && R.msaa_h == R.height) return;
  if (R.msaa_fbo) {
    glDeleteFramebuffers(1, &R.msaa_fbo);
    glDeleteRenderbuffers(2, R.msaa_rb);
    R.msaa_fbo = 0;
  }
  R.msaa_cfg = g_cfg.msaa;
  R.msaa_w = R.width;
  R.msaa_h = R.height;
  GLsizei n = msaa_effective(g_cfg.msaa);
  if (!n) return;
  glGenFramebuffers(1, &R.msaa_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.msaa_fbo);
  glGenRenderbuffers(2, R.msaa_rb);
  glBindRenderbuffer(GL_RENDERBUFFER, R.msaa_rb[0]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_RGBA8, R.width, R.height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, R.msaa_rb[0]);
  glBindRenderbuffer(GL_RENDERBUFFER, R.msaa_rb[1]);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, n, GL_DEPTH_COMPONENT24, R.width, R.height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, R.msaa_rb[1]);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    fprintf(stderr, "msaa: %dx framebuffer incomplete - disabled\n", (int)n);
    glDeleteFramebuffers(1, &R.msaa_fbo);
    glDeleteRenderbuffers(2, R.msaa_rb);
    R.msaa_fbo = 0;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
}

// Quarter-res ping-pong chain for the frosted menu backdrop — lazily rebuilt
// on size change, exactly like the MSAA target. blur_fbo[0] == 0 means "no
// blur": ui_menu falls back to its heavier scrim, never to a broken frame.
static void blur_update(void) {
  int bw = R.width / 4, bh = R.height / 4;
  if (bw < 8) bw = 8;
  if (bh < 8) bh = 8;
  if (R.blur_fbo[0] && R.blur_w == bw && R.blur_h == bh) return;
  if (R.blur_fbo[0]) {
    glDeleteFramebuffers(2, R.blur_fbo);
    glDeleteTextures(2, R.blur_tex);
    R.blur_fbo[0] = R.blur_fbo[1] = 0;
  }
  R.blur_w = bw;
  R.blur_h = bh;
  glGenTextures(2, R.blur_tex);
  glGenFramebuffers(2, R.blur_fbo);
  for (int i = 0; i < 2; i++) {
    glBindTexture(GL_TEXTURE_2D, R.blur_tex[i]);
    // RGBA: the alpha is the scene's bloom weight, blitted down with the colour.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, R.blur_fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           R.blur_tex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glDeleteFramebuffers(2, R.blur_fbo);
      glDeleteTextures(2, R.blur_tex);
      R.blur_fbo[0] = R.blur_fbo[1] = 0;
      break;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
}

// One colour or depth texture of the screen-space kind: no mips, clamped, and
// the filter the caller asks for (depth and normals are read NEAREST — a
// bilinear depth is a surface that exists nowhere).
static GLuint post_tex(GLenum ifmt, GLenum fmt, GLenum type, int w, int h,
                       GLenum filter) {
  GLuint t;
  glGenTextures(1, &t);
  glBindTexture(GL_TEXTURE_2D, t);
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint)ifmt, w, h, 0, fmt, type, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t;
}

// The scene target, the prepass target and the AO pair — lazily rebuilt on a
// size change like the MSAA target and the blur chain above. The scene and
// the prepass SHARE the depth texture: the prepass fills it, the AO reads it,
// and the lit pass (MSAA off) overwrites it with the same values. Half the
// frame's resolution for the AO, floored so a tiny window still has a target.
static void post_update(void) {
  if (R.scene_fbo && R.scene_w == R.width && R.scene_h == R.height) return;
  if (R.scene_fbo) {
    glDeleteFramebuffers(1, &R.scene_fbo);
    glDeleteFramebuffers(1, &R.pre_fbo);
    glDeleteFramebuffers(2, R.ao_fbo);
    glDeleteFramebuffers(4, R.bloom_fbo);
    glDeleteFramebuffers(1, &R.pic_fbo);
    glDeleteFramebuffers(1, &R.ssr_fbo);
    GLuint t[7] = {R.scene_tex, R.depth_tex, R.nrm_tex, R.ao_tex[0], R.ao_tex[1],
                   R.pic_tex, R.ssr_tex};
    glDeleteTextures(7, t);
    glDeleteTextures(4, R.bloom_tex);
    R.scene_fbo = 0;
  }
  R.scene_w = R.width;
  R.scene_h = R.height;
  R.ao_w = R.width / 2 > 8 ? R.width / 2 : 8;
  R.ao_h = R.height / 2 > 8 ? R.height / 2 : 8;
  R.scene_tex = post_tex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, R.width, R.height, GL_LINEAR);
  R.depth_tex = post_tex(GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT,
                         R.width, R.height, GL_NEAREST);
  R.nrm_tex = post_tex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, R.width, R.height, GL_NEAREST);
  for (int i = 0; i < 2; i++)
    R.ao_tex[i] = post_tex(GL_RG16F, GL_RG, GL_HALF_FLOAT, R.ao_w, R.ao_h, GL_LINEAR);
  R.pic_tex = post_tex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, R.width, R.height, GL_LINEAR);
  // The march runs at the AO's half resolution: a film of water blurs what it
  // mirrors anyway, and the apply pass reads it bilinearly at full size.
  R.ssr_tex = post_tex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, R.ao_w, R.ao_h, GL_LINEAR);
  for (int i = 0; i < 4; i++) {
    R.bloom_w[i] = R.width >> (2 + i) > 4 ? R.width >> (2 + i) : 4;
    R.bloom_h[i] = R.height >> (2 + i) > 4 ? R.height >> (2 + i) : 4;
    R.bloom_tex[i] = post_tex(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
                              R.bloom_w[i], R.bloom_h[i], GL_LINEAR);
  }
  glGenFramebuffers(1, &R.scene_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.scene_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.scene_tex, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, R.depth_tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    fatal("scene framebuffer incomplete");
  glGenFramebuffers(1, &R.pre_fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, R.pre_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.nrm_tex, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, R.depth_tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    fatal("prepass framebuffer incomplete");
  glGenFramebuffers(2, R.ao_fbo);
  for (int i = 0; i < 2; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, R.ao_fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.ao_tex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fatal("ao framebuffer incomplete");
  }
  { GLuint fb[2]; GLuint tx[2] = {R.pic_tex, R.ssr_tex};
    glGenFramebuffers(2, fb);
    R.pic_fbo = fb[0]; R.ssr_fbo = fb[1];
    for (int i = 0; i < 2; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx[i], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fatal("reflection framebuffer incomplete");
    } }
  glGenFramebuffers(4, R.bloom_fbo);
  for (int i = 0; i < 4; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, R.bloom_fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, R.bloom_tex[i], 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fatal("bloom framebuffer incomplete");
  }
  glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
}

// Finish the picture into the SCENE target — ONCE PER FRAME, and the latch is
// what makes that true. With MSAA on this is the resolve blit (an 8x resolve
// of 1280x720 is the single most expensive blit in the frame, and running it
// twice cost 12 ms on the llvmpipe proxy for a second copy of a picture that
// had not changed); with it off the picture was drawn into the scene target
// directly and there is nothing to copy. Three callers want a finished frame
// (the composite, the frosted menu backdrop, and the end of render_frame).
//
// The consequence is the rule below it: once the frame is finished, EVERYTHING
// after it is drawn into the PRESENT buffer. Nothing after this point is
// geometry — the composite is a fullscreen triangle that reads the scene
// target and the HUD is SDF text that carries its own antialiasing — so there
// is nothing left for MSAA to do but be copied again.
static void msaa_resolve(void) {
  if (R.frame_resolved) return;
  if (R.msaa_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, R.msaa_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.scene_fbo);
    glBlitFramebuffer(0, 0, R.width, R.height, 0, 0, R.width, R.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
  }
  R.frame_resolved = 1;
}

// Where the frame is being built RIGHT NOW. One predicate, so a pass that draws
// after the resolve cannot land on the buffer that is no longer being presented:
// the multisample target or the scene target while the picture is being made,
// the present buffer once it is finished.
static GLuint frame_fbo(void) {
  if (R.frame_resolved) return R.present_fbo;
  return R.msaa_fbo ? R.msaa_fbo : R.scene_fbo;
}

// A render target is an FBO AND its viewport; binding one without the other is how a
// scope pass ends up drawn at window resolution.
static void rt_bind(GLuint fbo, int w, int h) {
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glViewport(0, 0, w, h);
}

// EVERYTHING THE GLOW SETTING OWNS: the quarter-res copy, the four-octave bloom
// chain and the sun shafts, in one function so the setting is one branch and not
// six. It hands the composite nothing: the finished shafts are always in
// blur_tex[1], because the Kawase taps are an EVEN number and the ping-pong ends
// back in the half the shafts pass wrote.
//
// It reuses the frosted menu's ping-pong pair rather than owning targets:
// scene -> blur[0] (the blit), bright+shafts -> blur[1], two Kawase taps back
// and forth. Nothing here allocates.
static void glare_chain(float sx, float sy, float rays, const mat4 *vp,
                        v3 eye, int shadow_on) {
  glBindFramebuffer(GL_READ_FRAMEBUFFER, R.scene_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.blur_fbo[0]);
  glBlitFramebuffer(0, 0, R.width, R.height, 0, 0, R.blur_w, R.blur_h,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glViewport(0, 0, R.blur_w, R.blur_h);
  // THE BLOOM CHAIN, from the quarter-res copy: four 13-tap downsamples (the
  // first one weighted by the scene's own bloom alpha and Karis-averaged), then
  // three 9-tap tent upsamples ADDED back up the chain, so the finest level
  // ends up carrying all four octaves of halo.
  glUseProgram(R.prog_bdown);
  glUniform1i(R.u_bd_tex, 0);
  for (int i = 0; i < 4; i++) {
    int sw = i ? R.bloom_w[i - 1] : R.blur_w, shh = i ? R.bloom_h[i - 1] : R.blur_h;
    rt_bind(R.bloom_fbo[i], R.bloom_w[i], R.bloom_h[i]);
    glBindTexture(GL_TEXTURE_2D, i ? R.bloom_tex[i - 1] : R.blur_tex[0]);
    glUniform2f(R.u_bd_px, 1.0f / (float)sw, 1.0f / (float)shh);
    glUniform1f(R.u_bd_first, i ? 0.0f : 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glUseProgram(R.prog_bup);
  glUniform1i(R.u_bu_tex, 0);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  for (int i = 2; i >= 0; i--) {
    rt_bind(R.bloom_fbo[i], R.bloom_w[i], R.bloom_h[i]);
    glBindTexture(GL_TEXTURE_2D, R.bloom_tex[i + 1]);
    glUniform2f(R.u_bu_px, 1.0f / (float)R.bloom_w[i + 1], 1.0f / (float)R.bloom_h[i + 1]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
  }
  glDisable(GL_BLEND);
  // The shafts: a true camera-ray integration through the shadowed atmosphere,
  // quarter res. The depth stops the integral at the first visible surface and
  // the shadow map carves the sunlit medium into spatial beams.
  glViewport(0, 0, R.blur_w, R.blur_h);
  glUseProgram(R.prog_volume);
  glUniform1i(R.u_vol_depth, 1);
  glUniform1i(R.u_vol_shadow, 0);
  { mat4 inv = m4_inverse(vp);
    v3 at = atmo_tint();
    glUniformMatrix4fv(R.u_vol_invvp, 1, GL_FALSE, inv.m);
    glUniformMatrix4fv(R.u_vol_lightvp, 1, GL_FALSE, R.light_vp.m);
    glUniform3f(R.u_vol_eye, eye.x, eye.y, eye.z);
    glUniform3f(R.u_vol_sundir, g_sun.dir.x, g_sun.dir.y, g_sun.dir.z);
    glUniform3f(R.u_vol_suncol, g_sun.col.x, g_sun.col.y, g_sun.col.z);
    glUniform3f(R.u_vol_atmo, at.x, at.y, at.z);
    glUniform4f(R.u_vol_p, rays, shadow_on ? 1.0f : 0.0f,
                g_sun.exposure * g_cfg.brightness, atmo_haze()); }
  rt_bind(R.blur_fbo[1], R.blur_w, R.blur_h);
  gl_active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, R.depth_tex);
  gl_active_texture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, R.sm_tex);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  // A restrained radial component remains as lens scatter around the source.
  // It adds shape near the solar disc while the shadowed volume carries the
  // world-space structure, and is intentionally much weaker than the old shaft.
  glUseProgram(R.prog_glare);
  glUniform1i(R.u_gl_tex, 0);   // mode 0 reads this one and no other
  glUniform1f(R.u_gl_mode, 0.0f);
  glUniform3f(R.u_gl_sun, sx * 0.5f + 0.5f, sy * 0.5f + 0.5f, rays);
  glUniform2f(R.u_gl_knee, 0.78f, 0.97f);
  glUniform3f(R.u_gl_gain, 0.0f, 0.18f, 0.0f);   // lens scatter; bloom has its chain
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE);
  glBindFramebuffer(GL_FRAMEBUFFER, R.blur_fbo[1]);
  glBindTexture(GL_TEXTURE_2D, R.blur_tex[0]);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glDisable(GL_BLEND);
  // Two growing dual-Kawase taps: a ten-tap march at quarter res is banded,
  // and this is the filter that was already here to fix that.
  glUseProgram(R.prog_blur);
  glUniform1i(R.u_blur_tex, 0);
  int src = 1;
  for (int i = 0; i < 2; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, R.blur_fbo[1 - src]);
    glBindTexture(GL_TEXTURE_2D, R.blur_tex[src]);
    glUniform2f(R.u_blur_px, (float)(i + 1) / (float)R.blur_w,
                (float)(i + 1) / (float)R.blur_h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    src = 1 - src;
  }
}

// The glare and the composite, and it is the last thing that happens to the
// PICTURE before the HUD is drawn on top of it. Order is load-bearing in both
// directions: after the viewmodel, so a muzzle flash and the weapon's own
// highlights spill like everything else; before the HUD, because a bloomed ammo
// counter is a smeared ammo counter.
//
// The glare itself is glare_chain and is optional; the ONE full-screen composite
// draw below is not — it is how the scene target reaches the present buffer.
static void glare_pass(float yaw, float pitch, float roll, float fovy, float aspect,
                       const mat4 *vp, v3 eye, int shadow_on) {
  blur_update();
  msaa_resolve();   // the glare reads a FINISHED frame; also final-safe
  if (!R.blur_fbo[0] || !R.blur_fbo[1]) {
    // No chain: no glare, but the picture still has to reach the present
    // buffer — a plain copy is the composite with everything off.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, R.scene_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.present_fbo);
    glBlitFramebuffer(0, 0, R.width, R.height, 0, 0, R.width, R.height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, R.present_fbo);
    return;
  }
  // WHERE THE SUN IS ON SCREEN. The sun is a direction, not a point, so it
  // projects through the camera basis rather than through the view matrix — and
  // it has to be behind nothing: with the sun behind the camera the march would
  // run toward a mirrored phantom and paint shafts converging on empty sky.
  v3 cf, cr, cu;
  cam_basis(yaw, pitch, roll, &cf, &cr, &cu);
  float zs = v3_dot(g_sun.dir, cf);
  float ty = tanf(fovy * 0.5f), tx = ty * aspect;
  float sx = 0.0f, sy = 0.0f, rays = 0.0f;
  if (zs > 0.02f) {
    sx = v3_dot(g_sun.dir, cr) / (zs * tx);
    sy = v3_dot(g_sun.dir, cu) / (zs * ty);
    // Two fades, and they answer different questions. The first is "is the sun
    // in front of me at all" — near the 90-degree edge the projection stretches
    // without bound and a hard cut there would pop shafts on as the player
    // turns. The second is "is it anywhere near the frame": past about two
    // screens out the march direction is nearly the same for every pixel, which
    // is a screen-wide directional smear and not a shaft.
    float off = fmaxf(fabsf(sx), fabsf(sy));
    rays = f_smoothstep(0.02f, 0.32f, zs) * (1.0f - f_smoothstep(1.1f, 2.8f, off));
    // A LOW SUN THROWS THE LONG SHAFTS, and that is the same physics the haze
    // band is built on: a shallow ray crosses more dust, so more of the beam
    // scatters toward the eye. Tied to the arena's own dust as well, so a clear
    // theme does not get a dusty theme's beams.
    rays *= (0.55f + 0.95f * (1.0f - g_sun.dir.y)) * (0.55f + 1.15f * atmo_haze());
  }
  // The state both halves want: the chain and the composite are full-screen
  // triangles that neither blend nor test depth.
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glBindVertexArray(R.vao_sky);   // attribute-less fullscreen triangle
  // GLOW OFF: neither the chain nor the shafts run — those two passes are the
  // whole setting — but the composite is not optional. It is the pass that gets
  // the picture from the scene target into the present buffer, and it is also
  // the grade, the FXAA and the grain, so it runs with both glare gains at zero
  // and the reads behind them gated in GLARE_FS.
  if (g_cfg.bloom) glare_chain(sx, sy, rays, vp, eye, shadow_on);
  // The composite: the scene target on unit 1, the glare on unit 0, the
  // present buffer as the target. No blending — see the branch in GLARE_FS.
  rt_bind(frame_fbo(), R.width, R.height);
  glUseProgram(R.prog_glare);
  // THE UNIT ASSIGNMENT BELONGS TO THE PASS THAT READS, and this pass runs on
  // every frame while the chain above does not: declared anywhere but here,
  // uScene is unset on a frame with GLOW off.
  glUniform1i(R.u_gl_tex, 0);
  glUniform1i(R.u_gl_scene, 1);
  glUniform1i(R.u_gl_bloom, 3);
  glUniform1f(R.u_gl_mode, g_dbg_view == 1 ? 2.0f : g_dbg_view == 2 ? 3.0f
                         : g_dbg_view == 3 ? 4.0f : 1.0f);
  // x = the bloom's weight in the composite: four accumulated octaves of a
  // weight that is already a fraction of the colour, so a third of it is a
  // visible halo on the sun, the water and a tracer and nothing on a wall.
  // The two settings scale the shipped numbers rather than choosing between two
  // sets of them, so there is still one copy of each: GLOW off zeroes both glare
  // gains (and GLARE_FS then skips the reads), FILM GRAIN off zeroes the grain
  // AND the vignette it belongs to.
  { float glow = g_cfg.bloom ? 1.0f : 0.0f;
    glUniform3f(R.u_gl_gain, 0.30f * glow, 0.0f, 0.56f * glow); }
  // The seed walks with the sim tick, so it is deterministic per tick and yet
  // never still in play. 8 ticks is 15 Hz of grain, which is about where film
  // sits: at 120 it strobes, at 1 it is dirt on the lens.
  { float film = g_cfg.grain ? 1.0f : 0.0f;
    glUniform4f(R.u_gl_grade, 0.20f * film, 0.045f * film,
                (float)((g_tick / 8) % 733) * 7.13f,
                (float)R.width / (float)R.height); }
  // FXAA only where there is no multisample target to do the job.
  glUniform3f(R.u_gl_aa, R.msaa_fbo ? 0.0f : 1.0f,
              1.0f / (float)R.width, 1.0f / (float)R.height);
  gl_active_texture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, R.bloom_tex[0]);
  gl_active_texture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_dbg_view == 1 ? R.ao_tex[0]
                             : g_dbg_view == 2 ? R.bloom_tex[0] : R.scene_tex);
  gl_active_texture(GL_TEXTURE0);
  // blur_tex[1] is where the shafts finish: the Kawase pair is an even number of
  // taps, so the ping-pong lands back in the half the shafts pass wrote. With
  // GLOW off nothing wrote it this frame and uGain.z is 0, so it is not read.
  glBindTexture(GL_TEXTURE_2D, R.blur_tex[1]);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glEnable(GL_DEPTH_TEST);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
}

// Frosted-glass backdrop for the PAUSING menu (a networked session never gets one):
// flush the HUD batch, resolve the scene into the present buffer, pull it down to
// quarter res, run four growing dual-Kawase taps, and paint the result back as one
// full-screen image quad — then hand GL state back to the UI batch.
static void ui_menu_backdrop(void) {
  g_ui_backdrop = 0;
  blur_update();
  if (!R.blur_fbo[0] || !R.blur_fbo[1]) return;
  ui_flush();
  msaa_resolve();   // what has been drawn so far (also final-safe)
  glBindFramebuffer(GL_READ_FRAMEBUFFER, R.present_fbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, R.blur_fbo[0]);
  glBlitFramebuffer(0, 0, R.width, R.height, 0, 0, R.blur_w, R.blur_h,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  glDisable(GL_BLEND);
  glUseProgram(R.prog_blur);
  glUniform1i(R.u_blur_tex, 0);
  glBindVertexArray(R.vao_sky);   // attribute-less fullscreen triangle
  glViewport(0, 0, R.blur_w, R.blur_h);
  int src = 0;
  for (int i = 0; i < 4; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, R.blur_fbo[1 - src]);
    glBindTexture(GL_TEXTURE_2D, R.blur_tex[src]);
    glUniform2f(R.u_blur_px, (float)(i + 1) / (float)R.blur_w,
                (float)(i + 1) / (float)R.blur_h);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    src = 1 - src;
  }
  rt_bind(frame_fbo(), R.width, R.height);
  glEnable(GL_BLEND);
  glUseProgram(R.prog_hud);
  glBindVertexArray(R.vao_hud);
  glUniform1i(R.u_img, 1);
  glBindTexture(GL_TEXTURE_2D, R.blur_tex[src]);
  ui_begin();
  // The scene's v axis runs bottom-up, the HUD's top-down: v is flipped.
  // Tinted slightly cool and dim — glass takes light, the scope learned that.
  ui_quad(0, 0, g_ui_vw, g_ui_vh, 0, 1, 1, 0, (float[4]){0.82f, 0.85f, 0.90f, 1.0f});
  ui_flush();
  glUniform1i(R.u_img, 0);
  glBindTexture(GL_TEXTURE_2D, R.font_tex);
  ui_begin();
  g_ui_backdrop = 1;
}
