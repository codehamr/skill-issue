// =============================================================================
// core/frame.inc  —  app_start/app_frame/app_sync/resolve_binds, app_pump,
// plus pad_look and pad_assist_friction (the aim-assist friction).
// Proof: appframe. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: the frame loop belongs to NEITHER backend; app_pump is the
// shared input+frame glue both native backends run and nothing else does.
// Aim assist is FRICTION-ONLY and pad-only, inside pad_look. leave_match is
// the ONE online->local teardown and it ends on the ENTRY SCREEN, whether the
// player pressed LEAVE MATCH or the server stopped answering (proof:
// netleave / netleave drop).
// =============================================================================
// ===========================================================================
// Frame step — the part of a game loop that is GAME rather than PLATFORM.
// ===========================================================================
// A backend owns a window, a clock, a keyboard and a swap chain. Everything
// between "here is dt and the input state" and "swap the buffers" is the same game on
// every target, and it used to live inside WinMain — which meant a second backend had
// to grow a second copy of the hitch clamp, the tick accumulator, the press-latch rule,
// the listener update and the event drain.

// --- Key ids and the bind vocabulary --------------------------------------
// ONE index space for both windowed backends.
#define KEY_TAB    0x09   // VK_TAB
#define KEY_SPACE  0x20   // VK_SPACE
#define KEY_LSHIFT 0xA0   // VK_LSHIFT
#define KEY_LCTRL  0xA2   // VK_LCONTROL
#define KEY_MOUSE1 256
#define KEY_MOUSE2 257
#define KEY_MOUSE3 258
#define KEY_MOUSE4 259
#define KEY_MOUSE5 260
// The WHEEL lives in the same latch array and deliberately NOT in the bind vocabulary.
#define KEY_MWUP   261
#define KEY_MWDN   262
// Gamepad buttons extend the same space (+ pad_btn_t index), so binds, press
// latches and the menu gate treat a controller exactly like a keyboard.
#define KEY_PAD0   263
#define KEY_COUNT  (KEY_PAD0 + PB_COUNT)

// The vocabulary is fixed by ACTION_DEF's defaults and by config_emit's own
// header comment; anything outside it returns 0, which is what makes
// resolve_binds fall back to the default rather than leave an action dead.
static int key_from_token(const char *tok) {
  if (strlen(tok) == 1) {
    char c = tok[0];
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    if (c >= 'A' && c <= 'Z') return c;
    if (c >= '0' && c <= '9') return c;
  }
  if (!strcmp(tok, "space"))  return KEY_SPACE;
  if (!strcmp(tok, "tab"))    return KEY_TAB;
  if (!strcmp(tok, "lshift")) return KEY_LSHIFT;
  if (!strcmp(tok, "lctrl"))  return KEY_LCTRL;
  if (!strcmp(tok, "mouse1")) return KEY_MOUSE1;
  if (!strcmp(tok, "mouse2")) return KEY_MOUSE2;
  if (!strcmp(tok, "mouse3")) return KEY_MOUSE3;
  if (!strcmp(tok, "mouse4")) return KEY_MOUSE4;
  if (!strcmp(tok, "mouse5")) return KEY_MOUSE5;
  if (!strncmp(tok, "pad_", 4))
    for (int i = 0; i < PB_COUNT; i++)
      if (!strcmp(tok, PAD_BTN_DEF[i].tok)) return KEY_PAD0 + i;
  return 0;
}

// Reverse mapping, for the menu's rebind capture and the name field. NULL = not a
// bindable key, which the backends use to decide whether to swallow the keystroke.
static const char *token_from_key(int key) {
  static char one[2];
  if (key >= 'A' && key <= 'Z') { one[0] = (char)(key - 'A' + 'a'); one[1] = 0; return one; }
  if (key >= '0' && key <= '9') { one[0] = (char)key; one[1] = 0; return one; }
  switch (key) {
    case KEY_SPACE:  return "space";
    case KEY_TAB:    return "tab";
    case KEY_LSHIFT: return "lshift";
    case KEY_LCTRL:  return "lctrl";
    case KEY_MOUSE1: return "mouse1";
    case KEY_MOUSE2: return "mouse2";
    case KEY_MOUSE3: return "mouse3";
    case KEY_MOUSE4: return "mouse4";
    case KEY_MOUSE5: return "mouse5";
  }
  if (key >= KEY_PAD0 && key < KEY_COUNT) return PAD_BTN_DEF[key - KEY_PAD0].tok;
  return NULL;
}

// Key LEVEL plus a press LATCH that survives until a sim tick consumes it — written by
// whichever windowed backend is compiled, read only through frame_input_keys.
static int g_keys[KEY_COUNT], g_pressed[KEY_COUNT];

typedef struct {
  float mouse_dx, mouse_dy;    // raw mouse counts accumulated since the last frame
  int   held[IN_COUNT];        // action held right now
  int   held_first[IN_COUNT];  // held OR latched — a tap shorter than one frame
} frame_input_t;               //   still has to produce a press edge in the sim

typedef struct {
  double acc;         // tick accumulator; its remainder is the render alpha
  double fps_window;
  int    fps, frames;
  GLsync fence;       // previous frame's GPU work, see app_sync
} app_t;

// Everything a playable session needs once a GL context is current.
static void app_start(player_t *p, int w, int h, uint32_t seed) {
  g_map_seed = seed;
  map_generate(seed);
  if (!g_server) {
    renderer_init(w, h);
    sfx_build_all();
  }
  player_spawn(p);
  match_init();
}

// THE WHEEL SCROLLS THROUGH THE WEAPONS, and it is wired to IN_SWAP rather than to a
// pair of NEXT/PREV actions on purpose.
static int input_wheel_swap(void) {
  return !g_menu_open && (g_pressed[KEY_MWUP] || g_pressed[KEY_MWDN]);
}

// held/held_first for one frame. The menu gate and the "held OR latched" rule are
// policy and belong here; only filling g_keys/g_pressed is the backend's.
static void frame_input_keys(frame_input_t *in, const int binds[IN_COUNT],
                             const int pbinds[IN_COUNT]) {
  for (int i = 0; i < IN_COUNT; i++) {
    int held = g_keys[binds[i]] || (pbinds[i] && g_keys[pbinds[i]]);
    int latch = g_pressed[binds[i]] || (pbinds[i] && g_pressed[pbinds[i]]);
    // The BOOT SCREEN substitutes the director's own finger for the player's.
    int hb = g_home ? home_input(i) : 0;
    in->held[i] = g_menu_open ? hb : held;
    in->held_first[i] = g_menu_open ? hb : (held || latch);
  }
  if (input_wheel_swap()) in->held_first[IN_SWAP] = 1;
}

// Config tokens -> key ids; a typo in the config falls back to the action's own default
// so no action can silently go dead.
static void resolve_binds(int binds[IN_COUNT], int pbinds[IN_COUNT]) {
  for (int i = 0; i < IN_COUNT; i++) {
    binds[i] = key_from_token(g_cfg.bind[i]);
    if (!binds[i]) binds[i] = key_from_token(ACTION_DEF[i].dflt);
    pbinds[i] = key_from_token(g_cfg.padbind[i]);
    if (!pbinds[i] && strcmp(g_cfg.padbind[i], "none") != 0)
      pbinds[i] = key_from_token(ACTION_DEF[i].pdflt);
  }
}

// --- Gamepad frame step — shared policy over the g_pad state the backend filled.

// Trigger-as-button hysteresis: ON above 25%, OFF below 15%.
#define PAD_TRIG_ON  0.25f
#define PAD_TRIG_OFF 0.15f
static void pad_btn_sync(void) {
  g_pad.btn[PB_LT] = g_pad.lt > (g_pad.btn[PB_LT] ? PAD_TRIG_OFF : PAD_TRIG_ON);
  g_pad.btn[PB_RT] = g_pad.rt > (g_pad.btn[PB_RT] ? PAD_TRIG_OFF : PAD_TRIG_ON);
}

// Menu navigation repeat: a held direction (dpad or left stick) fires once,
// waits, then repeats — the same feel every console front-end has.
#define PAD_NAV_DELAY 0.35f
#define PAD_NAV_REP   0.11f
#define PAD_NAV_ACCEL 4.0f   // extra repeat speed a long hold ramps up to
#define PAD_NAV_RAMP  2.5f   // seconds of repeating it takes to get there
static float g_pad_nav_t[4], g_pad_nav_hold[4];
static void pad_nav_dir(int idx, int level, float dt, int nav) {
  if (!level) { g_pad_nav_t[idx] = 0; g_pad_nav_hold[idx] = 0; return; }
  g_nav_kbd = 0;   // the pad reclaims the footer grammar from the keyboard
  if (g_pad_nav_t[idx] == 0) nav_push(nav);
  g_pad_nav_t[idx] += dt;
  g_pad_nav_hold[idx] += dt;
  if (g_pad_nav_t[idx] > PAD_NAV_DELAY) {
    // Hold-to-accelerate: 9 Hz at first repeat, ~45 Hz after PAD_NAV_RAMP of
    // holding — a FRAG LIMIT slider spanning 1..1000 at step 1 is otherwise
    // up to ~107 s of held input, while a short tap still steps exactly once.
    float held = g_pad_nav_hold[idx] - PAD_NAV_DELAY;
    float accel = 1.0f + PAD_NAV_ACCEL * f_clamp(held / PAD_NAV_RAMP, 0.0f, 1.0f);
    nav_push(nav);
    g_pad_nav_t[idx] -= PAD_NAV_REP / accel;
    // One push per frame is the ceiling anyway, so a frame dt LONGER than the
    // accelerated interval must not bank the difference: unclamped, a 30 fps
    // frame outruns the 22 ms interval and the backlog would burst out as a
    // slug of extra repeats the moment the frame rate recovers.
    if (g_pad_nav_t[idx] > PAD_NAV_DELAY + PAD_NAV_REP)
      g_pad_nav_t[idx] = PAD_NAV_DELAY + PAD_NAV_REP;
  }
}

// Swallow-until-release state, see pad_eat_held's forward declaration for the contract.
static int g_pad_eat[PB_COUNT];
static void pad_eat_held(void) {
  pad_btn_sync();  // triggers count as held too — an RT across an alt-tab
  for (int i = 0; i < PB_COUNT; i++)
    if (g_pad.btn[i]) g_pad_eat[i] = 1;
}

// Once per frame: trigger hysteresis, button levels into the shared key space (so binds
// see a controller as just more keys), and — with the menu open — the same buttons
// become navigation instead.
static void pad_sync(float dt) {
  static int prev[PB_COUNT];
  pad_btn_sync();
  // The eat gate applies to START too: prev[] goes stale while the game is unfocused
  // (the pad is pumped but pad_sync is skipped), so the first active frame sees a fresh
  // edge for a button pad_eat_held already swallowed — and an eaten START toggling the
  // menu is exactly the class the swallow exists to stop.
  if (g_pad.btn[PB_START] && !prev[PB_START] && !g_pad_eat[PB_START])
    ui_on_escape();
  for (int i = 0; i < PB_COUNT; i++) {
    int down = g_pad.btn[i];
    int edge = down && !prev[i];
    prev[i] = down;
    if (g_pad_eat[i]) {
      if (down) { g_keys[KEY_PAD0 + i] = 0; continue; }
      g_pad_eat[i] = 0;
    }
    if (edge && g_menu_open) {
      // An armed PAD capture owns every button: the press is the token, never
      // navigation — which is what makes B (and LB/RB/dpad) rebindable at all.
      if (g_ui_rebind >= 0 && g_ui_rebind_pad) {
        if (i != PB_START) ui_assign_bind(PAD_BTN_DEF[i].tok);
      } else if (i == PB_A)  { g_nav_kbd = 0; nav_push(NAV_OK); }
      else if (i == PB_B)  { g_nav_kbd = 0; nav_push(NAV_BACK); }
      else if (i == PB_LB) { g_nav_kbd = 0; nav_push(NAV_TABL); }
      else if (i == PB_RB) { g_nav_kbd = 0; nav_push(NAV_TABR); }
    }
    if (i == PB_START) continue;
    g_keys[KEY_PAD0 + i] = down;
    if (edge && !g_menu_open) g_pressed[KEY_PAD0 + i] = 1;
  }
  // Directional navigation: dpad OR the left stick past half deflection.
  if (g_menu_open && !(g_ui_rebind >= 0 && g_ui_rebind_pad)) {
    float sx, sy;
    pad_deadzone(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L], &sx, &sy);
    // The eat gate applies here too: this reads LEVELS, so without it a dpad held
    // across a menu close navigates the reopened menu while every other eaten button
    // correctly stays dead.
    float ah = fabsf(sx), av = fabsf(sy);
    pad_nav_dir(0, (g_pad.btn[PB_UP]    && !g_pad_eat[PB_UP])    || (sy >  0.5f && av >= ah), dt, NAV_UP);
    pad_nav_dir(1, (g_pad.btn[PB_DOWN]  && !g_pad_eat[PB_DOWN])  || (sy < -0.5f && av >= ah), dt, NAV_DOWN);
    pad_nav_dir(2, (g_pad.btn[PB_LEFT]  && !g_pad_eat[PB_LEFT])  || (sx < -0.5f && ah >  av), dt, NAV_LEFT);
    pad_nav_dir(3, (g_pad.btn[PB_RIGHT] && !g_pad_eat[PB_RIGHT]) || (sx >  0.5f && ah >  av), dt, NAV_RIGHT);
  } else {
    for (int i = 0; i < 4; i++) g_pad_nav_t[i] = g_pad_nav_hold[i] = 0;
  }
}

// ADS slows the stick by the ZOOM RATIO (tan of the half-FOVs), which is what keeps a
// flick that crosses the same screen distance the same thumb motion aimed and unaimed.
#define PAD_SR_SCOPE_K 0.28f
static float pad_ads_look_scale(const player_t *p) {
  float ads = p->wp.ads_s;
  if (ads <= 0) return 1.0f;
  float fov = g_cfg.fov;
  float s;
  if (p->wp.cur == WPN_SR) {
    s = 1.0f + (PAD_SR_SCOPE_K - 1.0f) * ads * ads;
  } else {
    float fov_ads = fov * g_cfg.wp[p->wp.cur][WP_ADS_ZOOM] * 0.01f;
    float f = fov + (fov_ads - fov) * ads;
    s = tanf(f * 0.5f * DEG2RAD) / tanf(fov * 0.5f * DEG2RAD);
  }
  // The player's ADS SENS factor rides on top of the zoom ratio, blended by
  // the same ramp so hip aim is never touched.
  return s * (1.0f + (g_cfg.pad[PAD_ADS_SENS] - 1.0f) * ads);
}

// --- Aim assist (pad only, toggleable, FRICTION ONLY) ------------------------
// Stick look slows inside a bubble around a target — what lets a thumb stop ON a 12 m/s
// strafer instead of skating past it.

// The look-rate multiplier at angular offset ang inside a cone of size eng, dist metres
// from the target: 1 - STRENGTH * ease(ang) * fade(dist).
static float pad_assist_friction(float ang, float eng, float dist) {
  float fall = smooth01(ang / eng);
  float ds = 1.0f - f_clamp(dist / g_cfg.pad[PAD_AS_DIST], 0.0f, 1.0f);
  return 1.0f - g_cfg.pad[PAD_AS_FRIC] * (1.0f - fall * fall) * ds;
}

// Nearest-by-angle live entity inside the FIXED engage cone (BUBBLE degrees — v2
// dropped the apparent-size cone: a fixed angle is the unit the tuner thinks in, and
// the linear DIST fade owns the far-range weakening).
static int pad_assist_target(const player_t *p, v3 *tp_out, float *ang_out,
                             float *eng_out, float *dist_out) {
  v3 eye = player_eye_pos(p);
  float cp = cosf(p->pitch);
  v3 fwd = {{sinf(p->yaw) * cp, sinf(p->pitch), -cosf(p->yaw) * cp}};
  float eng = g_cfg.pad[PAD_AS_BUBBLE] * DEG2RAD;
  int best = -1;
  // ENT_FOREACH, returning an ENTITY id: iterating g_bots alone gave
  // a pad player on the deploy handheld aim assist on bots and NONE on the
  // humans he actually fights — worse than no help, it actively pulls away.
  ENT_FOREACH(e) {
    if (e == g_local_ent || !ent_alive(e)) continue;
    v3 tp = ent_aimpoint(e);
    v3 d = v3_sub(tp, eye);
    float dist = v3_len(d);
    if (dist < 1.0f || dist > g_cfg.pad[PAD_AS_DIST]) continue;
    float ang = acosf(f_clamp(v3_dot(fwd, d) / dist, -1.0f, 1.0f));
    if (ang > eng) continue;
    if (best >= 0 && ang >= *ang_out) continue;
    if (!los_clear(eye, tp)) continue;   // the trace is the expensive test: last
    best = e;
    *tp_out = tp;
    *ang_out = ang;
    *eng_out = eng;
    *dist_out = dist;
  }
  return best;
}

// Right-stick look, applied per RENDER frame like the mouse (lowest latency; rate * dt
// makes the integral frame-rate independent).
static void pad_look(player_t *p, float dt) {
  float x, y;
  float m = pad_deadzone(g_pad.rx, g_pad.ry, g_cfg.pad[PAD_DZ_R], &x, &y);
  if (m <= 0) return;
  // FRICTION has no deflection gate: it multiplies an input-proportional step (zero
  // stick times anything is zero), so "no input, no assist" holds by construction — and
  // gating it carved a felt cliff into the exact micro-correction band it exists for
  // (measured: 0.01 of stick travel flipped an 84% authority drop on and off).
  float assist = 1.0f;
  v3 tp = {{0, 0, 0}};                       // out-params; the inits also quiet
  float ang = 0.0f, eng = 1.0f, dst = 0.0f;  // MinGW's inlined-flow analysis
  if (g_cfg.pad_assist && p->alive &&
      pad_assist_target(p, &tp, &ang, &eng, &dst) >= 0)
    assist = pad_assist_friction(ang, eng, dst);
  float k = pad_curve_apply(m, g_cfg.pad_curve) / m *
            pad_ads_look_scale(p) * assist * dt * DEG2RAD;
  float ydir = g_cfg.pad_inv_y ? -1.0f : 1.0f;
  p->yaw += x * k * g_cfg.pad[PAD_SENS] * PAD_SENS_DEG_X;
  float dp = y * k * g_cfg.pad[PAD_SENS] * PAD_SENS_DEG_Y * ydir;
  p->pitch = f_clamp(p->pitch + dp, -PITCH_MAX, PITCH_MAX);
}

// Listener update + event drain, once per frame: the ears travel with the peek, then
// every event of the frame's ticks is routed into the mixer.
static void audio_frame_drain(const player_t *p) {
  // The listener IS g_local_ent; a dedicated server has none and must not
  // route audio at all (the ring is drained per tick there instead).
  if (g_local_ent < 0) { g_num_events = 0; return; }
  g_listen = player_eye_pos(p);
  g_listen_r = (v3){{cosf(p->yaw), 0, sinf(p->yaw)}};
  for (int e = 0; e < g_num_events; e++) audio_event(&g_events[e]);
  g_num_events = 0;
}

// Advances the sim by `dt` seconds and renders one frame; the backend swaps.
static player_t *local_player(void) {
  int h = g_local_ent > MAX_BOTS ? g_local_ent - MAX_BOTS : 0;
  return &g_players[h];
}

// Solve the pose for every on-roster figure from its current (snapshot-driven or
// predicted) state, so scene_build can draw them.
static void client_death_launch(rag_t *r, v3 pos, v3 vel, int grounded, int ent) {
  const typeof(g_wound[0]) *w = &g_wound[ent];
  // A death whose EV_HIT* was lost (or arrived outside the resend window)
  // still has to look like a death, so the fallback is a plausible round from
  // in front at chest height rather than a body that drops straight down.
  int fresh = w->tick > 0 && g_tick - w->tick <= 60;
  v3 at   = fresh ? w->at   : v3_add(pos, (v3){{0.0f, 1.05f, 0.0f}});
  v3 from = fresh ? w->from : v3_add(pos, (v3){{0.0f, 1.35f, 2.5f}});
  rag_launch(r, pos, vel, grounded, from, at, fresh ? w->dmg : 34, w->hs);
}

static void client_anim_all(void) {
  for (int h = 0; h < MAX_HUMANS; h++) {
    if (!g_humans_on[h]) continue;
    player_t *p = &g_players[h];
    int ent = ent_of_human(h);
    if (g_cl_alive_prev[ent] && !p->alive) {
      client_death_launch(&g_player_rag[h], p->pos, p->vel, p->grounded, ent);
      g_player_corpse_q[h] = 1;
    }
    g_cl_alive_prev[ent] = p->alive ? 1 : 0;
    if (!p->alive) { player_present_tick(p, ent); continue; }   // THE one dead
                                                                // branch: pose freeze +
                                                                // the corpse handover
    // THE CARRIAGE, AND WHERE EACH HALF OF IT COMES FROM. A foreign player's two
    // intents rode in on the snapshot (NEF_ADS/NEF_READY) and net_cl_present_advance has
    // already turned the aim one back into a blend; OUR OWN body is predicted, so no
    // snapshot describes it and the carry predicate runs here exactly as
    // player_present_tick runs it on the server.
    if (ent == g_local_ent) p->wp.ready_want = (unsigned char)player_carry_ready(p);
    // ...AND THE FLASH COUNTS DOWN HERE for the same reason, on the same line
    // player_present_tick runs: nothing else on a client ticks a body's presentation,
    // and the dead branch above already took it through player_present_tick.
    if (p->flash > 0) p->flash--;
    anim_tick(&g_player_anim[h], p->pos, p->vel, p->yaw, p->pitch, p->grounded,
              p->crouched && !p->sliding, p->tuck_y, p->sliding, p->wp.ads_s,
              p->lean, p->wp.ready_want, ent_of_human(h));
  }
  for (int b = 0; b < g_match.nbots; b++) {
    bot_t *bt = &g_bots[b];
    if (!bt->active) continue;
    if (bt->flash > 0) bt->flash--;   // bot_tick's countdown; it does not run here
    // A RESPAWN HAS TO UNDO THE DEATH, and offline nothing has to say so: a bot
    // respawns through bot_spawn, whose `memset(b, 0, sizeof *b)` clears b->rag and
    // b->topple along with everything else.
    if (bt->alive && g_cl_body[b + 1]) {
      bt->rag = (rag_t){0};
      bt->topple = bt->prev_topple = 0.0f;
      g_cl_body[b + 1] = 0;                  // the body goes back to the server
      anim_reset(&bt->anim, bt->pos, bt->yaw);
    }
    if (!bt->alive && !g_cl_body[b + 1]) {
      client_death_launch(&bt->rag, bt->pos, bt->vel, bt->grounded, b + 1);
      g_cl_body[b + 1] = 1;                  // ...and this client owns it now
    }
    if (!bt->alive) {   // scene_draw_bot renders b->rag, so a bot ragdolls as
      bt->anim.prev_pose = bt->anim.pose;   // an ENTITY here, as it does offline
      // ...and offline `bot_tick` OPENS with the prev_ pair and closes the dead branch
      // with grounded.
      bt->prev_pos = bt->pos;
      bt->prev_topple = bt->topple;
      rag_advance(&bt->rag, &bt->pos, bt->prev_pos, &bt->topple, &bt->anim);
      bt->grounded = bt->rag.grounded;
      continue;
    }
    anim_tick(&bt->anim, bt->pos, bt->vel, bt->yaw, bt->pitch, bt->grounded,
              bt->crouch, 0.0f, bt->sliding, bt->wp.ads_s, 0.0f,
              bt->wp.ready_want, b + 1);
  }
}

// One online client tick: assemble the local input, predict + send it. Foreign
// state has already been applied by net_cl_poll before the tick loop.
static void client_online_tick(player_t *p, const int held[IN_COUNT]) {
  float wx = 0.0f, wy = 0.0f;
  if (g_pad.connected && p->alive && !g_menu_open) {
    float mx, my;
    if (pad_deadzone(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L], &mx, &my) > 0) { wx = mx; wy = my; }
  }
  // Presentation-only REDEPLOY countdown: the sim's respawn_t is server state
  // (set on the snapshot's death edge); the real respawn arrives as an alive
  // flip, never from this counter reaching zero.
  if (!p->alive && p->respawn_t > 0) p->respawn_t--;
  // The CLIENT half of the match-over countdown, for the same reason: this fork
  // replaces player_tick, so match_tick — where the server runs the same one copy —
  // never executes here.
  match_over_countdown();
  net_cl_tick(held, wx, wy, p->yaw, p->pitch);
  net_cl_extrapolate();        // foreign entities advance at tick cadence too
}

// Begin a join: the LOCAL match keeps running (the caller is already in one), the
// handshake is driven each frame by app_frame's poll below, and the switch to the
// server happens only when its first snapshot lands.
static int start_connect(const char *host, int port) {
  uint32_t ip = net_resolve(host);
  if (!ip) {           // resolve failure reports like any other failed join:
    g_cl.reject = 0;   // the menu tile's generic message, never silence
    g_cl.state = NC_FAILED;
    return 0;
  }
  g_net_secret = 0;                          // client uses the server's cookie
  return net_cl_connect(ip, (uint16_t)port, g_cfg.name);
}

// --connect HOST[:PORT]: begin a join while the local match keeps running. ONE parse
// for both native mains — a main that never set g_boot_connect is a no-op here.
static void boot_connect_kick(void) {
  if (!g_boot_connect || !g_boot_connect[0]) return;
  char host[64]; int port = g_cfg.mp_port;
  snprintf(host, sizeof host, "%s", g_boot_connect);
  char *colon = strchr(host, ':');
  if (colon) { *colon = '\0'; port = atoi(colon + 1); }
  start_connect(host, port);
}

// LEAVING AN ONLINE MATCH IS A TEARDOWN, NOT JUST A DISCONNECT.
// net_cl_apply_ent switches a human slot ON for every foreign player a snapshot
// mentions, and NOTHING ever switched them back off: net_cl_disconnect wipes the
// transport, and match_restart deliberately preserves the roster (it must — a server's
// players have to survive a rematch).
//
// ...AND IT ENDS ON THE ENTRY SCREEN, WHICHEVER WAY IT WAS ENTERED. The teardown has
// exactly two callers — the player pressing LEAVE MATCH and the client noticing the
// server stopped answering — and neither of them asked for a singleplayer round. It
// used to hand one over anyway: a fresh local arena, bots already shooting, the HUD
// live, which reads as "the server is still there" for however long it takes to notice
// the names are gone. The rail is where a session is chosen, so the rail is where a
// session ends.
static void leave_match(void) {
  if (g_cl.state != NC_OFF) net_cl_disconnect();
  g_online = 0;
  g_local_ent = 0;
  // Everything above slot 0 belonged to the SERVER's roster.
  for (int h = 1; h < MAX_HUMANS; h++) {
    g_humans_on[h] = 0;
    g_human_name[h][0] = '\0';
    memset(&g_players[h], 0, sizeof g_players[h]);
    memset(&g_player_anim[h], 0, sizeof g_player_anim[h]);
    memset(&g_player_rag[h], 0, sizeof g_player_rag[h]);
    g_player_corpse_q[h] = 0;
    memset(g_human_held[h], 0, sizeof g_human_held[h]);
    g_human_wish[h][0] = g_human_wish[h][1] = 0.0f;
    g_human_move[h] = 1;   // back to the singleplayer default (one input =
    g_human_view[h] = -1;  // one move is a networked rule; locally nothing
                           // gates a move)
  }
  g_humans_on[0] = 1;                    // the local player exists again
  // Scoreboard furniture is cleared HERE, not only in net_cl_disconnect: that call sits
  // behind an `if (state != NC_OFF)` guard, so a session whose transport had already
  // died left the last server's ping column standing in the local match.
  memset(g_net_ping, 0, sizeof g_net_ping);
  g_net_fraglimit = 0;
  memset(g_spawn_seq, 0, sizeof g_spawn_seq);
  memset(g_cl_body, 0, sizeof g_cl_body);   // no body of the server's survives
  memset(g_cl_alive_prev, 0, sizeof g_cl_alive_prev);
  match_scores_clear();
  match_restart();     // fresh local arena; it also runs vfx_match_reset,
                       // which clears the corpses of players who are gone
  // The boot sequence, in the boot order: the rail first (it releases the pointer grab
  // and re-seeds the software cursor through platform_menu_changed — this can be
  // reached from a grabbed, menu-less frame), the BOOT SCREEN second, because
  // ui_open_menu reads g_home to decide whether it is a pause.
  ui_open_menu();
  home_enter(local_player());
}

static int app_frame(app_t *a, player_t *p, double dt, const frame_input_t *in) {
  tele_tick();             // anonymous usage ping; self-gated, never blocks
  if (dt > 0.1) dt = 0.1;  // hitch clamp: never spiral the sim
  // Presentation motion clock: real dt while playing; the harness never gets here for
  // uiframe/shot, so those keep the fixed 1/60 default and stay deterministic.
  g_ui_dt = dt > 0.05 ? 0.05f : (float)dt;

  // Drive an in-progress handshake even while still playing locally:
  // the switch to online happens on the first snapshot, inside net_cl_poll.
  if (!g_online && g_cl.state != NC_OFF && g_cl.state != NC_FAILED) {
    net_cl_poll();
    // The join landing is the success signal the menu waits on: the
    // MULTIPLAYER tile keeps the menu open through the handshake so a failure
    // has a visible place to be seen — success closes it here instead.
    if (g_online && g_menu_open) ui_close_menu();
  }

  // ONLINE FORK: the local player is predicted and sent, foreign entities come from
  // snapshots, and the AI/present halves of the SP tick do not run.
  if (g_online) {
    net_cl_poll();
    if (g_cl.state == NC_FAILED) {          // server gone: back to the rail
      leave_match();                        // the ONE teardown (see there)
      // AND THE VERDICT OUTLIVES THE TEARDOWN. net_cl_disconnect wipes g_cl, so one
      // frame after a drop the MULTIPLAYER tile read QUICK JOIN and the arena had
      // simply evaporated with no account of itself. Re-latching FAILED here is free:
      // the fd is closed, so net_cl_poll returns at its own guard and the handshake
      // poll above excludes FAILED by name.
      g_cl.state = NC_FAILED;
      g_cl.reject = NR_LOST;
      // fall through to the singleplayer path this frame
    } else {
      p = local_player();
      if (!g_menu_open) { player_look(p, in->mouse_dx, in->mouse_dy, g_cfg.sense); pad_look(p, (float)dt); }
      // CLOCK NUDGE: scale the accumulator so the SERVER-side input queue rests at
      // NET_QDEPTH_TARGET. Crystals disagree by ~100 ppm and jitter parks the queue
      // wherever the last burst left it; unregulated, the depth either drains to
      // starvation (missed moves) or climbs into the backlog trim (discarded moves) —
      // both are visible corrections. +-0.4%/tick-of-error, capped at -3%/+1%: a
      // 10-tick spike drains in ~3 s, and at most 1% fast-forward keeps starvation
      // recovery gentle.
      {
        float e = NET_QDEPTH_TARGET - g_cl.qdepth_ema;
        a->acc += dt * (1.0 + (double)f_clamp(e * 0.004f, -0.03f, 0.01f));
      }
      int nt = 0;
      while (a->acc >= TICK_DT) {
        g_tick++;
        client_online_tick(p, nt ? in->held : in->held_first);
        // VFX ARE TICK-DRIVEN, so an online tick has to run them too.
        vfx_tick();
        // THE PRESENT HALF IS PART OF THE TICK, not of the frame.
        client_anim_all();
        a->acc -= TICK_DT;
        nt++;
      }
      audio_frame_drain(p);
      a->frames++; a->fps_window += dt;
      if (a->fps_window >= 0.25) { a->fps = (int)((double)a->frames / a->fps_window + 0.5); a->frames = 0; a->fps_window = 0; }
      render_frame(p, (float)(a->acc / TICK_DT), a->fps, NULL);
      return nt;
    }
  }

  // Mouse look is applied per RENDER frame (lowest latency); the fixed-step
  // sim below then uses the current yaw for its wish direction.
  if (!g_menu_open) {
    player_look(p, in->mouse_dx, in->mouse_dy, g_cfg.sense);
    pad_look(p, (float)dt);
  }

  // THE MENU IS THE PAUSE (this is a singleplayer game): while it is open the
  // accumulator FREEZES — no ticks run, and the frozen sub-tick remainder holds the
  // render alpha, so the world stands as a stable freeze-frame (all prev/cur
  // interpolation twins and every tick-side VFX timer hold together).
  if (!session_pauses())
    a->acc += dt * (g_hitstop > 0 && !g_online ? (double)HITSTOP_SCALE
                  : g_home ? (double)home_timescale() : 1.0);
  if (g_hitstop > 0 && !session_pauses()) g_hitstop--;
  int ticks = 0;
  while (a->acc >= TICK_DT) {
    player_tick(p, ticks ? in->held : in->held_first);
    a->acc -= TICK_DT;
    ticks++;
  }

  audio_frame_drain(p);

  a->frames++;
  a->fps_window += dt;
  if (a->fps_window >= 0.25) {
    a->fps = (int)((double)a->frames / a->fps_window + 0.5);
    a->frames = 0;
    a->fps_window = 0;
  }

  render_frame(p, (float)(a->acc / TICK_DT), a->fps, NULL);
  return ticks;
}

// One frame of the shared input glue plus the frame itself: pad frame step, bind
// resolution, the held/held_first snapshot, app_frame, and the latch-clear contract —
// clear g_pressed ONLY when a tick consumed it, or a tap shorter than a frame is
// dropped (the exact trap the app_frame comment documents).
static int app_pump(app_t *a, player_t *p, double dt, float mdx, float mdy) {
  // The updater's per-frame half: apply what the boot screen armed.
  upd_auto_tick();
  int binds[IN_COUNT], pbinds[IN_COUNT];
  pad_sync((float)dt);
  resolve_binds(binds, pbinds);
  frame_input_t in = {.mouse_dx = mdx, .mouse_dy = mdy};
  frame_input_keys(&in, binds, pbinds);
  if (app_frame(a, p, dt, &in))
    memset(g_pressed, 0, sizeof g_pressed);  // a tick consumed the latches
  return g_ui_quit;
}

// Cap the driver's render-ahead queue at one frame: block until the PREVIOUS frame's
// GPU work is done, then fence this one.
static void app_sync(app_t *a) {
  if (a->fence) {
    glClientWaitSync(a->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
    glDeleteSync(a->fence);
  }
  a->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

