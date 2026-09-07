// =============================================================================
// core/frame.inc — application frame, native input, diagnostics and fixtures.
// Proof: appframe, framepace, spstart, netleave. Included once in definition order.
// Contract: app_pump joins native input to app_frame; the harness can drive
// app_frame directly. pad_look owns pad-only aim friction. New-local-arena
// intent is consumed before handshake polling at a frame boundary. Cleanup is
// shared; explicit arena requests enter play, while leave_match returns HOME.
// =============================================================================
// ===========================================================================
// Frame step — the part of a game loop that is GAME rather than PLATFORM.
// ===========================================================================
// Backends own devices and presentation. Shared code owns input interpretation,
// tick accumulation, listener updates and event draining before the buffer swap.

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
  int   physical_held[IN_COUNT]; // device levels, without HOME director input
  int   held[IN_COUNT];        // action held right now
  int   held_first[IN_COUNT];  // held OR latched — a tap shorter than one frame
} frame_input_t;               //   still has to produce a press edge in the sim

typedef struct {
  double acc;         // tick accumulator; its remainder is the render alpha
  double fps_window;
  int    fps, frames;
  GLsync fence;       // previous frame's GPU work, see app_sync
} app_t;

#define SP_START_SAMPLES 64
static uint64_t g_sp_serial;
static double g_sp_requested_at, g_sp_ready_ms;
static struct { double start, render_ms, present_ms; unsigned mask; } g_sp_start[SP_START_SAMPLES];
static unsigned g_sp_start_count, g_sp_start_drops, g_sp_present_failed;
static int g_sp_present_index = -1;
static int sp_elapsed_ms(double start, double end, double *out) {
  if (f_is_nonfinite64(start) || f_is_nonfinite64(end) || start < 0 || end < start) return 0;
  double elapsed = (end - start) * 1000.0;
  if (f_is_nonfinite64(elapsed)) return 0;
  *out = elapsed;
  return 1;
}
// Called immediately after the existing swap, without adding a fence or wait.
static void sp_frame_presented(int presented) {
  if (g_sp_present_index < 0) return;
  unsigned i = (unsigned)g_sp_present_index;
  g_sp_present_index = -1;
  if (!presented) { g_sp_present_failed++; return; }
  if (sp_elapsed_ms(g_sp_start[i].start, diag_now(), &g_sp_start[i].present_ms))
    g_sp_start[i].mask |= 2;
  else if (g_diag.enabled) g_diag.timer_errors++;
}

#define NATIVE_DIAG_HELP \
  "  --diagnostics PATH  create a local native timing report on exit\n" \
  "  --native-fixture WARMUP RUN  deterministic native window workload; seconds\n" \
  "                     warmup 0..120, run 1..900; requires explicit --seed,\n" \
  "                     --config and --diagnostics; profile is never rewritten\n"
static struct {
  int fixture, have_seed, have_config, have_diagnostics;
  uint32_t seed;
  char config_path[512], diagnostics_path[512];
  double warmup, seconds;
} g_native_options;
[[maybe_unused]] static FILE *g_diag_out;
#define FIXTURE_CHECKPOINTS 2048
#define FIXTURE_PERIOD 1440u
static struct {
  int started, cancelled, clock_failed, watchdog_started;
  double start, last_clock;
  double watchdog_start, watchdog_last, watchdog_primary, watchdog_progress;
  long start_tick, last_tick;
  uint64_t ticks, actions, checkpoint_drops;
  unsigned checkpoint_count;
  uint64_t checkpoint_tick[FIXTURE_CHECKPOINTS];
  char checkpoint_hash[FIXTURE_CHECKPOINTS][65];
  char profile_hash[65], binary_hash[65];
} g_fixture;

static int native_number(const char *text, double lo, double hi, double *out) {
  char *end = NULL;
  errno = 0;
  double value = strtod(text, &end);
  if (!text[0] || end == text || *end || errno || f_is_nonfinite64(value) ||
      value < lo || value > hi) return 0;
  *out = value;
  return 1;
}
[[maybe_unused]] static int native_option(int argc, char *const argv[], int *index) {
  const char *name = argv[*index];
  int values = !strcmp(name, "--native-fixture") ? 2 :
      (!strcmp(name, "--diagnostics") || !strcmp(name, "--seed") ||
       !strcmp(name, "--config")) ? 1 : 0;
  if (!values) return 0;
  if (*index + values >= argc) goto bad;
  const char *value = argv[*index + 1];
  if (!strcmp(name, "--native-fixture")) {
    double warmup, seconds;
    if (!native_number(value, 0, 120, &warmup) ||
        !native_number(argv[*index + 2], 1, 900, &seconds) ||
        (g_native_options.fixture && (warmup != g_native_options.warmup ||
                                     seconds != g_native_options.seconds))) goto bad;
    g_native_options.fixture = 1;
    g_native_options.warmup = warmup;
    g_native_options.seconds = seconds;
  } else if (!strcmp(name, "--seed")) {
    if (!value[0]) goto bad;
    for (const char *p = value; *p; p++) if (*p < '0' || *p > '9') goto bad;
    errno = 0;
    unsigned long long n = strtoull(value, NULL, 10);
    if (errno || n > UINT32_MAX ||
        (g_native_options.have_seed && g_native_options.seed != (uint32_t)n)) goto bad;
    g_native_options.seed = (uint32_t)n;
    g_native_options.have_seed = 1;
  } else {
    int config = !strcmp(name, "--config");
    char *path = config ? g_native_options.config_path : g_native_options.diagnostics_path;
    int *have = config ? &g_native_options.have_config : &g_native_options.have_diagnostics;
    if (!value[0] || strlen(value) >= 512 || (*have && strcmp(path, value))) goto bad;
    snprintf(path, 512, "%s", value);
    *have = 1;
  }
  *index += values;
  return 1;
bad:
  fprintf(stderr, "native options: invalid or conflicting %s\n", name);
  return -1;
}
static int native_profile_hash(const char *path, char out[65]) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  sha256_t sha;
  sha256_init(&sha);
  char bytes[4096];
  size_t n, total = 0;
  while ((n = fread(bytes, 1, sizeof bytes, f)) != 0) {
    total += n;
    if (total > 1048576) { fclose(f); return 0; }
    sha256_update(&sha, bytes, n);
  }
  int ok = !ferror(f);
  fclose(f);
  if (ok) sha256_hex(&sha, out);
  return ok;
}
[[maybe_unused]] static int native_options_validate(int is_server, int has_script,
                                                    int wants_connect, int print_config) {
  if ((g_native_options.fixture || g_native_options.have_diagnostics) &&
      (is_server || has_script || print_config)) {
    fprintf(stderr, "native options: diagnostics require native play\n");
    return 0;
  }
  if (g_native_options.fixture &&
      (!g_native_options.have_seed || !g_native_options.have_config ||
       !g_native_options.have_diagnostics || wants_connect)) {
    fprintf(stderr, "native fixture: explicit seed, config and diagnostics required; no connection\n");
    return 0;
  }
  if (g_native_options.fixture &&
      !native_profile_hash(g_native_options.config_path, g_fixture.profile_hash)) {
    fprintf(stderr, "native fixture: profile is unreadable or exceeds 1 MiB\n");
    return 0;
  }
  g_diag.enabled = g_native_options.have_diagnostics;
  return 1;
}
[[maybe_unused]] static void fixture_prepare(void) {
  if (!g_native_options.fixture) return;
  char loaded_hash[65];
  if (!native_profile_hash(g_native_options.config_path, loaded_hash) ||
      strcmp(loaded_hash, g_fixture.profile_hash))
    fatal("native fixture: profile changed while loading");
  g_cfg.telemetry = 0;
  g_cfg.update_check = 0;
  g_cfg.sp_frag = 1000;
}
[[maybe_unused]] static void fixture_start(void) {
  if (!g_native_options.fixture) return;
  g_home = 0;
  if (g_menu_open) ui_close_menu();
  memset(g_keys, 0, sizeof g_keys);
  memset(g_pressed, 0, sizeof g_pressed);
  g_fixture.start = g_fixture.last_clock = diag_now();
  g_fixture.start_tick = g_fixture.last_tick = g_tick;
  g_fixture.started = 1;
  g_diag.collect_after = g_fixture.start + g_native_options.warmup;
}
[[maybe_unused]] static void fixture_cancel(void) { g_fixture.cancelled = 1; }
[[maybe_unused]] static void fixture_fail_clock(void) {
  if (g_diag.enabled) g_diag.timer_errors++;
  if (g_native_options.fixture) g_fixture.clock_failed = g_fixture.cancelled = 1;
}
static int fixture_done_at(double now) {
  if (!g_native_options.fixture || !g_fixture.started) return 0;
  if (g_fixture.cancelled) return 1;
  if (!diag_seconds_valid(now) || !diag_seconds_valid(g_fixture.start) ||
      !diag_seconds_valid(g_fixture.last_clock) || now < g_fixture.last_clock) {
    fixture_fail_clock();
    return 1;
  }
  g_fixture.last_clock = now;
  return now - g_fixture.start >= g_native_options.warmup + g_native_options.seconds;
}
[[maybe_unused]] static int fixture_done(void) {
  if (!g_native_options.fixture || !g_fixture.started) return 0;
  return fixture_done_at(diag_now());
}
// Native loops supply an independent clock after fixture_done updates the primary
// clock. This bounds uncapped and inactive fixtures too. Two seconds allow normal
// scheduling jitter; a stopped primary or missed wall deadline fails explicitly.
[[maybe_unused]] static int fixture_watchdog_at(double independent_seconds) {
  if (!g_native_options.fixture || !g_fixture.started) return 0;
  if (g_fixture.cancelled) return 1;
  int bad = !diag_seconds_valid(independent_seconds) ||
      !diag_seconds_valid(g_fixture.last_clock);
  if (bad) { fixture_fail_clock(); return 1; }
  if (!g_fixture.watchdog_started) {
    g_fixture.watchdog_started = 1;
    g_fixture.watchdog_start = g_fixture.watchdog_last = independent_seconds;
    g_fixture.watchdog_primary = g_fixture.last_clock;
    g_fixture.watchdog_progress = independent_seconds;
    return 0;
  }
  bad |= independent_seconds < g_fixture.watchdog_last ||
      g_fixture.last_clock < g_fixture.watchdog_primary;
  if (!bad && g_fixture.last_clock > g_fixture.watchdog_primary) {
    g_fixture.watchdog_primary = g_fixture.last_clock;
    g_fixture.watchdog_progress = independent_seconds;
  }
  bad |= independent_seconds - g_fixture.watchdog_progress > 2.0 ||
      independent_seconds - g_fixture.watchdog_start >
          g_native_options.warmup + g_native_options.seconds + 2.0;
  if (bad) { fixture_fail_clock(); return 1; }
  g_fixture.watchdog_last = independent_seconds;
  return 0;
}
// The timeline is a pure function of ordinal simulation tick, never frame cadence.
static void fixture_actions(uint64_t tick, int held[IN_COUNT], float *dx, float *dy) {
  memset(held, 0, sizeof(int) * IN_COUNT);
  unsigned t = (unsigned)(tick % FIXTURE_PERIOD);
  held[IN_FWD] = t < 240 || (t >= 960 && t < 1200);
  held[IN_RIGHT] = t >= 240 && t < 420;
  held[IN_LEFT] = t >= 1200 && t < 1320;
  held[IN_ADS] = (t >= 420 && t < 720) || (t >= 840 && t < 960);
  held[IN_FIRE] = (t >= 480 && t < 564) || t == 870 || t == 930;
  held[IN_RELOAD] = t == 600;
  held[IN_SWAP] = t == 780 || t == 1020;
  held[IN_CROUCH] = t >= 1080 && t < 1200;
  held[IN_JUMP] = t == 180 || t == 1000;
  *dx = t < 720 ? 0.65f : -0.65f;
  *dy = t < 360 ? 0.02f : t < 720 ? -0.02f : 0.0f;
}
static void fixture_tick_input(player_t *p, int held[IN_COUNT]) {
  float dx, dy;
  fixture_actions(g_fixture.ticks, held, &dx, &dy);
  player_look(p, dx, dy, g_cfg.sense);
  g_fixture.ticks++;
  for (int i = 0; i < IN_COUNT; i++) g_fixture.actions += held[i] != 0;
}
static void fixture_state_hash(char out[65]) {
  sha256_t sha;
  sha256_init(&sha);
  unsigned char bytes[128];
  ENT_FOREACH(e) {
    net_ent_t entity;
    net_ent_from_live(&entity, e);
    np_wr wr = {bytes, sizeof bytes, 0, 0};
    net_ent_encode(&wr, &entity);
    if (!wr.bad) sha256_update(&sha, bytes, wr.at);
  }
  bot_suppress_state_hash(&sha);
  bot_tactical_state_hash(&sha);
  // Integer RNG streams are independent of rendered geometry and native audio.
  sha256_update(&sha, &g_rng_bot, sizeof g_rng_bot);
  sha256_update(&sha, g_rng_gun, sizeof g_rng_gun);
  sha256_hex(&sha, out);
}
static void fixture_tick_done(void) {
  g_fixture.last_tick = g_tick;
  if (g_fixture.ticks % TICK_HZ) return;
  if (g_fixture.checkpoint_count >= FIXTURE_CHECKPOINTS) {
    g_fixture.checkpoint_drops++;
    return;
  }
  unsigned i = g_fixture.checkpoint_count++;
  g_fixture.checkpoint_tick[i] = g_fixture.ticks;
  fixture_state_hash(g_fixture.checkpoint_hash[i]);
}
static void diag_profile_observe(void);
[[maybe_unused]] static void diag_capture_context(void) {
  if (!g_diag.enabled) return;
  const GLubyte *vendor = glGetString(GL_VENDOR), *renderer = glGetString(GL_RENDERER),
                *version = glGetString(GL_VERSION);
  snprintf(g_diag.vendor, sizeof g_diag.vendor, "%s", vendor ? (const char *)vendor : "unknown");
  snprintf(g_diag.renderer, sizeof g_diag.renderer, "%s", renderer ? (const char *)renderer : "unknown");
  snprintf(g_diag.version, sizeof g_diag.version, "%s", version ? (const char *)version : "unknown");
  char lower[160];
  for (unsigned i = 0; i < sizeof lower; i++) {
    char c = g_diag.renderer[i];
    lower[i] = c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c;
  }
  g_diag.software = strstr(lower, "llvmpipe") || strstr(lower, "softpipe") ||
                    strstr(lower, "software") || strstr(lower, "swiftshader");
  diag_msaa_observe(R.msaa_fbo ? msaa_effective(g_cfg.msaa) : 0);
  diag_profile_observe();
  if (!vendor || !renderer || !version) g_diag.platform_errors++;
  if (!upd_own_sha(g_fixture.binary_hash)) g_fixture.binary_hash[0] = '\0';
}

static void diag_json_string(FILE *out, const char *text) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (*p == '"' || *p == '\\') { fputc('\\', out); fputc(*p, out); }
    else if (*p < 32) fprintf(out, "\\u%04x", *p);
    else fputc(*p, out);
  }
  fputc('"', out);
}
static void diag_profile_json(char out[8192]) {
  size_t at = 0;
#define DIAG_PROFILE(...) do { \
  int written = snprintf(out + at, 8192 - at, __VA_ARGS__); \
  if (written < 0 || (size_t)written >= 8192 - at) fatal("diagnostic profile capacity"); \
  at += (size_t)written; \
} while (0)
  DIAG_PROFILE("{\"seed\":%u,\"bots\":%d,\"difficulty\":%d,\"fraglimit\":%d,"
      "\"fov\":%.9g,\"sense\":%.9g,\"ads_sense\":%.9g,"
      "\"cap\":%d,\"vsync\":%d,\"fullscreen_requested\":%d,\"msaa_requested\":%d,"
      "\"ssao\":%d,\"ssr\":%d,\"shadows\":%d,\"bloom\":%d,\"dust\":%d,"
      "\"filmic\":%d,\"brightness\":%.9g,\"third_person\":%d,\"movement\":[",
      g_map_seed, sp_bot_count(), sp_difficulty(), sp_frag_limit(),
      (double)g_cfg.fov, (double)g_cfg.sense, (double)g_cfg.ads_sense,
      g_cfg.fps_cap, g_cfg.vsync, g_cfg.fullscreen, g_cfg.msaa,
      g_cfg.ssao, g_cfg.ssr, g_cfg.shadows, g_cfg.bloom, g_cfg.dust,
      g_cfg.grain, (double)g_cfg.brightness, g_cfg.third_person);
  for (int i = 0; i < MV_COUNT; i++) DIAG_PROFILE("%s%.9g", i ? "," : "", (double)g_sim_mv[i]);
  DIAG_PROFILE("],\"gun\":[");
  for (int i = 0; i < GP_COUNT; i++) DIAG_PROFILE("%s%.9g", i ? "," : "", (double)g_sim_gun[i]);
  DIAG_PROFILE("],\"weapons\":[");
  for (int w = 0; w < WPN_COUNT; w++) {
    DIAG_PROFILE("%s[", w ? "," : "");
    for (int i = 0; i < WP_COUNT; i++) DIAG_PROFILE("%s%.9g", i ? "," : "", (double)g_sim_wp[w][i]);
    DIAG_PROFILE("]");
  }
  DIAG_PROFILE("],\"assist\":[%.9g,%.9g,%.9g]}",
      (double)g_cfg.pad[PAD_AS_FRIC], (double)g_cfg.pad[PAD_AS_BUBBLE],
      (double)g_cfg.pad[PAD_AS_DIST]);
#undef DIAG_PROFILE
}
// Pin the first effective profile. A mixed report is explicit even when settings
// return to their initial values before export; frame boundaries identify the span.
static void diag_profile_observe(void) {
  if (!g_diag.enabled) return;
  char current[8192];
  diag_profile_json(current);
  if (!g_diag.profile_pinned) {
    snprintf(g_diag.initial_profile, sizeof g_diag.initial_profile, "%s", current);
    sha256_t sha;
    sha256_init(&sha);
    sha256_update(&sha, current, strlen(current));
    sha256_hex(&sha, g_diag.initial_profile_hash);
    g_diag.profile_pinned = 1;
  } else if (strcmp(current, g_diag.last_profile)) {
    if (!g_diag.profile_changes) g_diag.profile_first_change = g_diag.serial;
    g_diag.profile_last_change = g_diag.serial;
    g_diag.profile_changes++;
  }
  snprintf(g_diag.last_profile, sizeof g_diag.last_profile, "%s", current);
}

static void diag_summary(FILE *out, const char *name, double values[DIAG_SAMPLES], unsigned n) {
  diag_json_string(out, name);
  fprintf(out, ":{\"count\":%u,\"p50_ms\":", n);
  if (!n) { fputs("null,\"p95_ms\":null,\"p99_ms\":null}", out); return; }
  qsort(values, n, sizeof *values, diag_double_cmp);
  fprintf(out, "%.9g,\"p95_ms\":%.9g,\"p99_ms\":%.9g}",
      diag_quantile(values, n, 50) * 1000.0,
      diag_quantile(values, n, 95) * 1000.0,
      diag_quantile(values, n, 99) * 1000.0);
}
[[maybe_unused]] static void diag_report(FILE *out) {
  if (!out || !g_diag.enabled) return;
  static double values[DIAG_SAMPLES];
  diag_profile_observe();
  fputs("{\"schema\":1,\"build_version\":", out); diag_json_string(out, BUILD_VERSION);
  fputs(",\"build_commit\":", out); diag_json_string(out, BUILD_COMMIT);
  fputs(",\"binary_sha256\":", out); diag_json_string(out, g_fixture.binary_hash);
  fputs(",\"gl_vendor\":", out); diag_json_string(out, g_diag.vendor);
  fputs(",\"gl_renderer\":", out); diag_json_string(out, g_diag.renderer);
  fputs(",\"gl_version\":", out); diag_json_string(out, g_diag.version);
  fprintf(out, ",\"software_renderer\":%s,\"actual_msaa\":%d,\"profile\":%s,"
      "\"effective_profile_sha256\":\"%s\",\"input_profile_sha256\":\"%s\",",
      g_diag.software ? "true" : "false", g_diag.initial_msaa, g_diag.initial_profile,
      g_diag.initial_profile_hash, g_fixture.profile_hash);
  fprintf(out, "\"profile_mixed\":%s,\"profile_changes\":%llu,"
      "\"profile_change_frames\":[%llu,%llu],\"actual_msaa_final\":%d,"
      "\"msaa_changes\":%llu,\"msaa_change_frames\":[%llu,%llu],",
      g_diag.profile_changes || g_diag.msaa_changes ? "true" : "false",
      (unsigned long long)g_diag.profile_changes,
      (unsigned long long)g_diag.profile_first_change,
      (unsigned long long)g_diag.profile_last_change, g_diag.actual_msaa,
      (unsigned long long)g_diag.msaa_changes,
      (unsigned long long)g_diag.msaa_first_change,
      (unsigned long long)g_diag.msaa_last_change);
  fprintf(out, "\"fixture\":{\"enabled\":%s,\"timeline\":1,\"period_ticks\":%u,"
      "\"warmup_seconds\":%.9g,\"run_seconds\":%.9g,\"cancelled\":%s,\"clock_failed\":%s,"
      "\"start_tick\":%ld,\"last_tick\":%ld,\"ticks\":%llu,\"actions\":%llu,"
      "\"checkpoint_drops\":%llu,\"checkpoints\":[",
      g_native_options.fixture ? "true" : "false", FIXTURE_PERIOD,
      g_native_options.warmup, g_native_options.seconds,
      g_fixture.cancelled ? "true" : "false", g_fixture.clock_failed ? "true" : "false",
      g_fixture.start_tick, g_fixture.last_tick,
      (unsigned long long)g_fixture.ticks, (unsigned long long)g_fixture.actions,
      (unsigned long long)g_fixture.checkpoint_drops);
  for (unsigned i = 0; i < g_fixture.checkpoint_count; i++)
    fprintf(out, "%s[%llu,\"%s\"]", i ? "," : "",
        (unsigned long long)g_fixture.checkpoint_tick[i], g_fixture.checkpoint_hash[i]);
  fprintf(out, "]},\"sample_total\":%llu,\"sample_evicted\":%llu,"
      "\"gpu_total\":%llu,\"gpu_evicted\":%llu,\"timer_errors\":%llu,"
      "\"unclosed_frames\":%llu,\"fence_timeouts\":%llu,\"fence_errors\":%llu,"
      "\"swap_errors\":%llu,\"platform_errors\":%llu,\"gpu_skipped\":%llu,"
      "\"gpu_unavailable\":%llu,\"gpu_errors\":%llu,\"platform_dropped\":%llu,"
      "\"output_source_names\":[\"unknown\",\"desktop_root\",\"window_monitor\",\"fallback\"],\"rate_source_names\":[\"unknown\",\"selected_mode\",\"platform_api\"],\"cap_reason_names\":[\"none\",\"explicit\",\"refresh\",\"unknown_60\"],"
      "\"platform_events\":[",
      (unsigned long long)g_diag.total, (unsigned long long)g_diag.evicted,
      (unsigned long long)g_diag.gpu_total, (unsigned long long)g_diag.gpu_evicted,
      (unsigned long long)g_diag.timer_errors, (unsigned long long)g_diag.missing_frames,
      (unsigned long long)g_diag.fence_timeouts, (unsigned long long)g_diag.fence_errors,
      (unsigned long long)g_diag.swap_errors, (unsigned long long)g_diag.platform_errors,
      (unsigned long long)g_diag.gpu_skipped, (unsigned long long)g_diag.gpu_unavailable,
      (unsigned long long)g_diag.gpu_errors, (unsigned long long)g_diag.platform_dropped);
  for (unsigned i = 0; i < g_diag.platform_count; i++) {
    const diag_platform_t *p = &g_diag.platform_events[i];
    fprintf(out, "%s{\"frame\":%llu,\"logical\":[%d,%d],\"drawable\":[%d,%d],"
        "\"output\":[%d,%d,%d,%d],\"source\":%d,\"refresh_hz\":",
        i ? "," : "", (unsigned long long)g_diag.platform_frame[i],
        p->logical_w, p->logical_h, p->drawable_w, p->drawable_h,
        p->output_x, p->output_y, p->output_w, p->output_h, p->output_source);
    if (diag_seconds_valid(p->refresh_hz) && p->refresh_hz > 0)
      fprintf(out, "%.9g", p->refresh_hz);
    else fputs("null", out);
    fprintf(out, ",\"swap_requested\":%d,\"swap_accepted\":%d,\"swap_queried\":%d,\"focused\":%d,\"fullscreen\":%d,\"output_id\":%llu,\"rate_source\":%d,\"cap_reason\":%d,\"effective_cap_hz\":%.9g}",
        p->swap_requested, p->swap_accepted, p->swap_queried, p->focused, p->fullscreen,
        (unsigned long long)p->output_id, p->rate_source, p->cap_reason,
        diag_seconds_valid(p->effective_cap_hz) ? p->effective_cap_hz : 0.0);
  }
  fputs("],\"cpu\":{", out);
  for (int channel = 0; channel < DIAG_CHANNELS; channel++) {
    unsigned n = 0;
    for (unsigned i = 0; i < g_diag.count; i++)
      if (g_diag.samples[i].mask & (1u << channel)) values[n++] = g_diag.samples[i].value[channel];
    if (channel) fputc(',', out);
    diag_summary(out, DIAG_NAME[channel], values, n);
  }
  fputs("},\"gpu\":{", out);
  for (int pass = 0; pass < RF_PASSES; pass++) {
    unsigned n = 0;
    for (unsigned i = 0; i < g_diag.gpu_count; i++)
      if (g_diag.gpu[i].mask & (1u << pass)) values[n++] = g_diag.gpu[i].value[pass];
    if (pass) fputc(',', out);
    diag_summary(out, RF_PASS_NAME[pass], values, n);
  }
  fputs("},\"cpu_columns\":[\"frame\",\"start_seconds\",\"active\"", out);
  for (int channel = 0; channel < DIAG_CHANNELS; channel++) {
    fputc(',', out); diag_json_string(out, DIAG_NAME[channel]);
  }
  fputs("],\"cpu_seconds\":[", out);
  for (unsigned j = 0; j < g_diag.count; j++) {
    unsigned i = (g_diag.next + DIAG_SAMPLES - g_diag.count + j) % DIAG_SAMPLES;
    const diag_sample_t *s = &g_diag.samples[i];
    fprintf(out, "%s[%llu,%.9f,%d", j ? "," : "",
        (unsigned long long)s->id, s->start, s->active);
    for (int channel = 0; channel < DIAG_CHANNELS; channel++) {
      fputc(',', out);
      if (s->mask & (1u << channel)) fprintf(out, "%.9g", s->value[channel]);
      else fputs("null", out);
    }
    fputc(']', out);
  }
  fputs("],\"gpu_seconds\":[", out);
  for (unsigned j = 0; j < g_diag.gpu_count; j++) {
    unsigned i = (g_diag.gpu_next + DIAG_SAMPLES - g_diag.gpu_count + j) % DIAG_SAMPLES;
    const diag_gpu_sample_t *s = &g_diag.gpu[i];
    fprintf(out, "%s[%llu", j ? "," : "", (unsigned long long)s->frame);
    for (int pass = 0; pass < RF_PASSES; pass++) {
      fputc(',', out);
      if (s->mask & (1u << pass)) fprintf(out, "%.9g", s->value[pass]);
      else fputs("null", out);
    }
    fputc(']', out);
  }
  fprintf(out, "],\"new_arena\":{\"accepted\":%llu,\"sample_drops\":%u,"
      "\"present_failed\":%u,\"present_pending\":%d,\"render_ready_ms\":[",
      (unsigned long long)g_sp_serial, g_sp_start_drops, g_sp_present_failed,
      g_sp_present_index >= 0);
  for (unsigned i = 0; i < g_sp_start_count; i++) {
    if (i) fputc(',', out);
    if (g_sp_start[i].mask & 1) fprintf(out, "%.6f", g_sp_start[i].render_ms);
    else fputs("null", out);
  }
  fputs("],\"present_ready_ms\":[", out);
  for (unsigned i = 0; i < g_sp_start_count; i++) {
    if (i) fputc(',', out);
    if (g_sp_start[i].mask & 2) fprintf(out, "%.6f", g_sp_start[i].present_ms);
    else fputs("null", out);
  }
  fputs("]}}\n", out);
}

// FPS follows real input-to-input time; simulation retains its bounded hitch step.
static double app_frame_delta(app_t *a, double raw) {
  if (!diag_seconds_valid(raw) || raw > 86400.0) {
    if (g_diag.enabled) g_diag.timer_errors++;
    return 0.0;
  }
  diag_add(DIAG_DELTA, raw);
  diag_add(DIAG_DISCARDED, raw > 0.1 ? raw - 0.1 : 0.0);
  a->frames++;
  a->fps_window += raw;
  if (a->fps_window >= 0.25) {
    a->fps = (int)((double)a->frames / a->fps_window + 0.5);
    a->frames = 0;
    a->fps_window = 0;
  }
  return raw > 0.1 ? 0.1 : raw;
}

// Everything a playable session needs once a GL context is current.
static void app_start(player_t *p, int w, int h, uint32_t seed) {
  sp_rules_from_saved();
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
    in->physical_held[i] = held;
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
    float fov_ads = fminf(fov, g_cfg.wp[p->wp.cur][WP_ADS_FOV]);
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
  audio_acoustic_update();
  for (int e = 0; e < g_num_events; e++) {
    const event_t *event = &g_events[e];
    int personal = event_local_outcome(event) ||
      ((unsigned)g_local_ent < MAX_ENTS && event->dst == g_local_ent &&
       (event->type == EV_HIT || event->type == EV_HIT_HEAD || event->type == EV_KILL));
    if (!g_event_feedback[e] && personal) continue;
    g_audio_event_cursor = e;
    double elapsed = ((double)g_tick - (double)g_event_tick[e]) / TICK_HZ;
    g_audio_event_age = elapsed > 0 ? (float)fmin(elapsed, 1.0) : 0;
    audio_event(&g_events[e]);
  }
  g_audio_event_age = 0;
  g_audio_event_cursor = -1;
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
    // A MIRROR'S EYE IS DERIVED, NOT RECEIVED. Nothing but the respawn edge ever
    // writes p->eye for a foreign slot (player_spawn never runs for one here), so
    // the wall predicate below used to measure from ankle height and tucked every
    // remote rifle behind knee-high cover. Same target and ease as the sim's own
    // (weapon.inc's eye settle); our own body predicted its eye already.
    if (ent != g_local_ent) {
      float eye_tgt = (p->crouched || p->sliding ? EYE_CROUCH : EYE_STAND) -
                      p->tuck_y;
      p->eye += (eye_tgt - p->eye) * f_clamp(EYE_LERP_RATE * TICK_DT, 0, 1);
    }
    // ...AND THE FLASH COUNTS DOWN HERE for the same reason, on the same line
    // player_present_tick runs: nothing else on a client ticks a body's presentation,
    // and the dead branch above already took it through player_present_tick.
    if (p->flash > 0) p->flash--;
    int idle_ok = gun_pose_state(&p->wp, &g_player_anim[h], p->vel,
                                   p->grounded, p->sliding);
    weapon_anim_tick(&g_player_anim[h], p->pos, player_eye_pos(p), p->vel,
                     p->yaw, p->pitch,
                     p->grounded, p->crouched && !p->sliding, p->tuck_y,
                     p->sliding, p->wp.ads_s, p->lean, p->wp.ready_want,
                     ent_of_human(h), &p->wp, idle_ok);
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
      anim_reset(&bt->anim, bt->pos, bt->yaw, bt->wp.cur);
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
    int idle_ok = gun_pose_state(&bt->wp, &bt->anim, bt->vel,
                                   bt->grounded, bt->sliding);
    weapon_anim_tick(&bt->anim, bt->pos,
                     bot_eye_pos(bt),
                     bt->vel, bt->yaw, bt->pitch,
                     bt->grounded, bt->crouch, 0.0f, bt->sliding, bt->wp.ads_s,
                     bt->lean, bt->wp.ready_want, b + 1, &bt->wp, idle_ok);
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

// Network identity and foreign roster cleanup have one owner. The destination
// chooses HOME or direct play; neither path can generate a second arena.
static void local_session_cleanup(void) {
  net_cl_disconnect();
  g_boot_connect = NULL;
  g_num_events = 0; // old session events cannot acquire the new audio epoch
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
}
static void leave_match(void) {
  local_session_cleanup();
  sp_rules_from_saved();
  match_scores_clear();
  match_restart();
  audio_session_reset();
  ui_open_menu();
  home_enter(local_player());
}

static int g_sp_pending;
static int g_sp_eat[IN_COUNT], g_sp_eat_l, g_sp_eat_r;
// Arena activation consumes a held menu gesture. Rest detection is independent
// of a player's fine aiming deadzone: low/deadzone-off settings still release.
static int sp_stick_held(float x, float y, float deadzone) {
  float rest = fmaxf(deadzone, 0.15f);
  return x * x + y * y > rest * rest;
}
static void sp_request_new_arena(void) {
  if (!g_sp_pending) g_sp_requested_at = diag_now();
  g_sp_pending = 1;
}

// One accepted request owns generation at the next frame boundary, before any
// handshake can publish another identity. The UI only queues intent.
static void sp_start_arena(app_t *a, const frame_input_t *in) {
  g_sp_pending = 0;
  local_session_cleanup();
  g_home = 0;
  g_home_t0 = 0;
  g_home_orb = g_vmorb.on = 0;
  sp_rules_from_saved();
  memset(g_players, 0, sizeof g_players);
  memset(g_player_anim, 0, sizeof g_player_anim);
  memset(g_human_held, 0, sizeof g_human_held);
  memset(g_human_wish, 0, sizeof g_human_wish);
  memset(g_bots, 0, sizeof g_bots);
  memset(&g_match, 0, sizeof g_match);
  g_match.nbots = sp_bot_count();
  g_bots_frozen = 0;
  audio_session_reset();
  g_last_wpn = 0;
  g_multi_t = -100000;
  match_restart();
  a->acc = 0;
  for (int i = 0; i < IN_COUNT; i++) g_sp_eat[i] = in->physical_held[i];
  g_sp_eat_l = g_pad.connected && sp_stick_held(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L]);
  g_sp_eat_r = g_pad.connected && sp_stick_held(g_pad.rx, g_pad.ry, g_cfg.pad[PAD_DZ_R]);
  memset(g_pressed, 0, sizeof g_pressed);
  g_nav_n = 0;
  g_nav_act = -1;
  g_ui_mdown = 0;
  int levels[KEY_COUNT];
  memcpy(levels, g_keys, sizeof levels);
  ui_close_menu();
  memcpy(g_keys, levels, sizeof levels);
  g_sp_serial++;
}

// Activation belongs to the menu until its physical release. Director input
// never owns this latch, so a genuinely new first-ready-frame press still works.
static void sp_filter_input(frame_input_t *in) {
  for (int i = 0; i < IN_COUNT; i++) {
    if (!in->physical_held[i]) g_sp_eat[i] = 0;
    if (g_sp_eat[i]) in->held[i] = in->held_first[i] = 0;
  }
  if (g_sp_eat_l) {
    if (!sp_stick_held(g_pad.lx, g_pad.ly, g_cfg.pad[PAD_DZ_L])) g_sp_eat_l = 0;
    g_pad.lx = g_pad.ly = 0;
  }
  if (g_sp_eat_r) {
    if (!sp_stick_held(g_pad.rx, g_pad.ry, g_cfg.pad[PAD_DZ_R])) g_sp_eat_r = 0;
    g_pad.rx = g_pad.ry = 0;
  }
}

static int app_frame(app_t *a, player_t *p, double dt, const frame_input_t *input) {
  uint64_t start_serial = g_sp_serial;
  // The result overlay's fire action uses the same new-arena owner as HOME.
  if (!g_online && !g_home && !g_menu_open && g_match.over &&
      input->held_first[IN_FIRE] && !local_player()->prev_held[IN_FIRE])
    sp_request_new_arena();
  frame_input_t clean = *input;
  pad_state_t physical_pad = g_pad;
  if (g_sp_pending) {
    sp_start_arena(a, input);
    p = local_player();
    memset(&clean, 0, sizeof clean);
    memcpy(clean.physical_held, input->physical_held, sizeof clean.physical_held);
  }
  sp_filter_input(&clean);
  const frame_input_t *in = &clean;
  if (!g_native_options.fixture) tele_tick();
  if (g_diag.enabled) diag_profile_observe();
  dt = app_frame_delta(a, dt);
  double diag_sim = 0.0;
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
      p = local_player();
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
      if (!g_menu_open && !g_native_options.fixture) { player_look(p, in->mouse_dx, in->mouse_dy, g_cfg.sense); pad_look(p, (float)dt); }
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
      if (g_diag.enabled) diag_sim = diag_now();
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
      if (g_diag.enabled) {
        diag_add(DIAG_SIM, diag_now() - diag_sim);
        diag_add(DIAG_BACKLOG, a->acc);
      }
      audio_frame_drain(p);
      render_frame(p, (float)(a->acc / TICK_DT), a->fps, NULL);
      g_pad = physical_pad;
      return nt;
    }
  }

  // Mouse look is applied per RENDER frame (lowest latency); the fixed-step
  // sim below then uses the current yaw for its wish direction.
  if (!g_menu_open && !g_native_options.fixture) {
    player_look(p, in->mouse_dx, in->mouse_dy, g_cfg.sense);
    pad_look(p, (float)dt);
  }

  // In local play the menu pauses simulation: while it is open the
  // accumulator FREEZES — no ticks run, and the frozen sub-tick remainder holds the
  // render alpha, so the world stands as a stable freeze-frame (all prev/cur
  // interpolation twins and every tick-side VFX timer hold together).
  if (!session_pauses())
    a->acc += dt * (g_hitstop > 0 && !g_online ? (double)HITSTOP_SCALE
                  : g_home ? (double)home_timescale() : 1.0);
  if (g_hitstop > 0 && !session_pauses()) g_hitstop--;
  int ticks = 0;
  if (g_diag.enabled) diag_sim = diag_now();
  while (a->acc >= TICK_DT) {
    if (g_native_options.fixture) {
      int fixture_held[IN_COUNT];
      fixture_tick_input(p, fixture_held);
      player_tick(p, fixture_held);
      fixture_tick_done();
    } else player_tick(p, ticks ? in->held : in->held_first);
    a->acc -= TICK_DT;
    ticks++;
  }

  if (g_diag.enabled) {
    diag_add(DIAG_SIM, diag_now() - diag_sim);
    diag_add(DIAG_BACKLOG, a->acc);
  }
  audio_frame_drain(p);

  render_frame(p, (float)(a->acc / TICK_DT), a->fps, NULL);
  g_pad = physical_pad;
  if (g_sp_serial != start_serial) {
    g_sp_ready_ms = -1;
    int valid = sp_elapsed_ms(g_sp_requested_at, diag_now(), &g_sp_ready_ms);
    if (g_diag.enabled) {
      if (!valid) g_diag.timer_errors++;
      if (g_sp_start_count < SP_START_SAMPLES) {
        unsigned i = g_sp_start_count++;
        g_sp_start[i].start = g_sp_requested_at;
        g_sp_start[i].render_ms = g_sp_ready_ms;
        g_sp_start[i].mask = valid ? 1u : 0u;
        g_sp_present_index = (int)i;
      } else g_sp_start_drops++;
    }
  }
  return ticks;
}

// One frame of the shared input glue plus the frame itself: pad frame step, bind
// resolution, the held/held_first snapshot, app_frame, and the latch-clear contract —
// clear g_pressed ONLY when a tick consumed it, or a tap shorter than a frame is
// dropped (the exact trap the app_frame comment documents).
static int app_pump(app_t *a, player_t *p, double dt, float mdx, float mdy) {
  // The updater's per-frame half: apply what the boot screen armed.
  if (!g_native_options.fixture) upd_auto_tick();
  pad_state_t physical_pad;
  if (g_native_options.fixture) {
    physical_pad = g_pad;
    memset(&g_pad, 0, sizeof g_pad);
  }
  int binds[IN_COUNT], pbinds[IN_COUNT];
  pad_sync((float)dt);
  resolve_binds(binds, pbinds);
  frame_input_t in = {.mouse_dx = mdx, .mouse_dy = mdy};
  frame_input_keys(&in, binds, pbinds);
  if (app_frame(a, p, dt, &in))
    memset(g_pressed, 0, sizeof g_pressed);  // a tick consumed the latches
  if (g_native_options.fixture) g_pad = physical_pad;
  return g_ui_quit;
}

// Cap the driver's render-ahead queue at one frame: block until the PREVIOUS frame's
// GPU work is done, then fence this one.
static void app_sync(app_t *a) {
  double started = g_diag.enabled ? diag_now() : 0.0;
  if (a->fence) {
    GLenum status = glClientWaitSync(a->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 100000000ull);
    if (g_diag.enabled) {
      g_diag.fence_timeouts += status == GL_TIMEOUT_EXPIRED;
      g_diag.fence_errors += status == GL_WAIT_FAILED;
    }
    glDeleteSync(a->fence);
  }
  a->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (g_diag.enabled) {
    g_diag.fence_errors += a->fence == NULL;
    diag_add(DIAG_FENCE, diag_now() - started);
  }
}
