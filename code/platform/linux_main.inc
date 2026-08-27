// =============================================================================
// platform/linux_main.inc  —  server_main (dedicated server) + main().
// Proof: netclient against --server. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: main()'s argument chain is the sole authority on the three
// mutually exclusive runtime modes; g_server is declared above the platform
// split so app_start's server gate compiles on MinGW too.
// =============================================================================
// ---------------------------------------------------------------------------
// Dedicated server: ONLY sim + UDP. No GL, no
// audio, no window — app_start's g_server gate skips both, and C's declaration order
// already proves no sim path can need a GL context (the first gl* call of the whole TU
// is far below the sim).
// ---------------------------------------------------------------------------
#include <signal.h>
static volatile sig_atomic_t g_srv_stop;
static void srv_sig(int sig) { (void)sig; g_srv_stop = 1; }

static int server_main(uint32_t seed) {
  // The cap chain — checked, because it holds nowhere by itself: a
  // cap_public above MAX_HUMANS promises a slot net_srv_admit can never hand
  // out, which is what the old cap_hard=10 did.
  if (g_srv.cap_public < 1 || g_srv.cap_public > MAX_HUMANS) {
    fprintf(stderr, "server: need 1 <= --cap-public (%d) <= %d\n",
            g_srv.cap_public, MAX_HUMANS);
    return 1;
  }
  if (g_srv.lobbies < 1 || g_srv.lobbies > SRV_LOBBIES) {
    fprintf(stderr, "server: --lobbies out of range 1..%d\n", SRV_LOBBIES);
    return 1;
  }
  if (g_srv.cap_bots < 0 || g_srv.cap_bots > MAX_BOTS) {
    fprintf(stderr, "server: --cap-bots out of range 0..%d\n", MAX_BOTS);
    return 1;
  }
  if (g_srv.bot_fill < 0 || g_srv.bot_fill > g_srv.cap_public) {
    fprintf(stderr, "server: need 0 <= --bot-fill (%d) <= --cap-public (%d)\n",
            g_srv.bot_fill, g_srv.cap_public);
    return 1;
  }
  if (g_srv.idle_s < 1 || g_srv.idle_s > 120) {
    fprintf(stderr, "server: --idle-timeout out of range 1..120\n");
    return 1;
  }
  // No humans until a join; there is no "me" on a server. g_headless stays 0:
  // the event contract is a HARNESS contract, not a log format — at 1,
  // ev_push would write one line per event into server.log forever, and that
  // file has no rotation because its volume is bounded by design (decision 9).
  g_local_ent = -1;
  // Match parameters from argv, not from config.
  // An empty booting server holds no bots (net_bot_target returns 0 for 0 humans);
  // the per-tick rebalance takes over on the first join.
  g_cfg.sp_bots = net_bot_target(0, g_srv.bot_fill, g_srv.cap_bots);
  g_cfg.sp_diff = g_srv.skill < 0 ? 0 : g_srv.skill > 2 ? 2 : g_srv.skill;
  g_cfg.sp_frag = g_srv.fraglimit;
  signal(SIGINT, srv_sig);
  signal(SIGTERM, srv_sig);
  // EVERY ARENA IS BROUGHT UP THE SAME WAY, through the one shared entry point.
  // app_start skips the renderer and the SFX bake when g_server is set, so on a server
  // it is exactly map + spawn + match_init — all per-lobby state.
  uint32_t lseed = seed;
  for (int l = 0; l < g_srv.lobbies; l++) {
    g_lobby = l;
    app_start(&g_players[0], 16, 16, lseed);
    g_humans_on[0] = 0;   // no humans until a join; there is no "me" here
    lseed = map_seed_next(lseed);
  }
  g_lobby = 0;
  g_srv_started = time(NULL);
  net_secret_init();
  g_srv_fd = net_udp_open(g_srv.port);
  if (g_srv_fd < 0) {
    fprintf(stderr, "server: cannot bind UDP port %d (in use?)\n", g_srv.port);
    return 1;
  }
  slog("server port=%d lobbies=%d cap=%d bots=%d skill=%d fraglimit=%d "
       "bot_fill=%d idle_s=%d v=%s",
       g_srv.port, g_srv.lobbies, g_srv.cap_public, g_srv.cap_bots,
       g_cfg.sp_diff, g_cfg.sp_frag, g_srv.bot_fill, g_srv.idle_s, BUILD_VERSION);
  // Fixed 120 Hz on the absolute clock: sleep-until beats sleep-for (no
  // drift), and the backlog is MEASURED (worst `behind` goes into the
  // digest) instead of silently eaten. ~13 µs of sim per tick, 0.16% of a
  // core — the VPS bottleneck is bandwidth, never CPU.
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  long ticks = 0, next_digest = TICK_HZ * 10;
  while (!g_srv_stop) {
    // Catch-up detection BEFORE the consume: waking from a stall inside the
    // nanosleep must not let the first tick's backlog drop eat the queued
    // inputs the following catch-up ticks are about to replay.
    {
      struct timespec nw;
      clock_gettime(CLOCK_MONOTONIC, &nw);
      long late0 = (nw.tv_sec - next.tv_sec) * 1000000000L +
                   (nw.tv_nsec - next.tv_nsec);
      g_srv_catchup = late0 > (long)(1e9 / TICK_HZ);
      // THE TELEMETRY BUDGETS RESET OFF THIS CLOCK, NOT OFF THE TICK COUNTER. `ticks %
      // TICK_HZ == 0` reads like "once per second" and is not: a catch-up burst runs
      // ticks faster than wall time, so the reset would fire more often than once per
      // second — the cap would RISE exactly when the server is already behind, which is
      // when it is needed most.
      static long tele_sec = -1, tele_day = -1, in_sec = -1;
      static int tele_cap_logged = 0;
      if (nw.tv_sec != tele_sec) { tele_sec = nw.tv_sec; g_tele_budget = 0; }
      if (nw.tv_sec / 86400 != tele_day) {
        tele_day = nw.tv_sec / 86400;
        g_tele_day_budget = 0;
        tele_cap_logged = 0;
      }
      // ...AND SAY SO WHEN THE DAY CAP BINDS. TELE_DAY_MAX is the only bound on the
      // log's growth and stays, but it is shared and unauthenticated: a flood spends
      // it in minutes and every real beat for the rest of the day is then dropped
      // silently, so server-report reads a quiet day rather than a suppressed one.
      // Once per day, so the diagnostic cannot itself be flooded. `server` keyword:
      // server-report.awk parses only `tele` rows, so its grammar is untouched.
      if (!tele_cap_logged && g_tele_day_budget >= TELE_DAY_MAX) {
        tele_cap_logged = 1;
        slog("server tele_day_cap=%d note=telemetry-suppressed", TELE_DAY_MAX);
      }
      // The INPUT budget resets off this clock for the same reason: it is the
      // anti-lag-switch cap, and a catch-up burst must not raise it. One memset over
      // every arena at once — 512 bytes, at most once a second.
      if (nw.tv_sec != in_sec) {
        in_sec = nw.tv_sec;
        memset(g_srv_in_budget_all, 0, sizeof g_srv_in_budget_all);
      }
    }
    net_srv_poll();                    // ONE socket, routed into the arenas
    // EVERY ARENA TICKS, IN ORDER. The body is exactly the single-arena loop that was
    // here before; only the g_lobby assignment and the rebalance are new.
    for (int l = 0; l < g_srv.lobbies; l++) {
      g_lobby = l;
      // g_cfg.sp_bots is global while match_t is not migrated, so the
      // arena's own bot target has to be re-established before its tick reads
      // it — without this, lobby 15's rebalance decides lobby 0's bot count.
      net_srv_rebalance();
      server_tick_once(NULL);
      if (ticks % 2 == 0) net_srv_broadcast();        // snapshots at 60 Hz
      if (ticks % 30 == 0) net_srv_scores_broadcast(); // score rows at 4 Hz
    }
    g_lobby = 0;
    ticks++;
    if (g_srv.ticks && ticks >= g_srv.ticks) break;
    if (ticks >= next_digest) {
      // ONE LINE PER OCCUPIED ARENA — diagnostics, not statistics.
      for (int l = 0; l < g_srv.lobbies; l++) {
        g_lobby = l;
        int humans = 0;
        for (int h = 0; h < MAX_HUMANS; h++) if (g_humans_on[h]) humans++;
        if (!humans) continue;
        char dg[160];
        net_digest(dg, sizeof dg);
        slog("lobby %d %s", l, dg);
      }
      g_lobby = 0;
      next_digest += TICK_HZ * 10;
    }
    next.tv_nsec += (long)(1e9 / TICK_HZ);
    while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long late_ns = (now.tv_sec - next.tv_sec) * 1000000000L +
                   (now.tv_nsec - next.tv_nsec);
    if (late_ns > 0) {
      long behind = late_ns / (long)(1e9 / TICK_HZ);
      if (behind > g_srv_behind) g_srv_behind = behind;
      // More than a second behind (a laptop lid, a VM pause): jump the clock
      // instead of fast-forwarding 120+ silent catch-up ticks per second.
      if (behind > TICK_HZ) next = now;
    } else {
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
  }
  char dg[160];
  net_digest(dg, sizeof dg);
  slog("%s", dg);
  return 0;
}

int main(int argc, char **argv) {
  g_main_argv = argv;   // execv after a self-update keeps the launch arguments
  int w = 1280, h = 720;
  uint32_t seed = DEFAULT_SEED;
  char *script = NULL;
  int print_cfg = 0, have_seed = 0;

  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    const char *arg = i + 1 < argc ? argv[i + 1] : NULL;
    if (!strcmp(a, "--help")) { usage(); return 0; }
    else if (!strcmp(a, "--print-config")) print_cfg = 1;
    else if (!strcmp(a, "--server")) g_server = 1;
    else if (arg && !strcmp(a, "--port"))       g_srv.port = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--cap-bots"))    g_srv.cap_bots = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--cap-public"))  g_srv.cap_public = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--bot-fill"))    g_srv.bot_fill = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--idle-timeout")) g_srv.idle_s = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--lobbies"))    g_srv.lobbies = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--skill"))      g_srv.skill = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--fraglimit"))  g_srv.fraglimit = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--ticks"))      g_srv.ticks = atol(argv[++i]);
    else if (!strcmp(a, "--net-debug"))         g_net_debug = 1;  // server.log: one
                                                // line per trim/skip, see decl
    else if (arg && !strcmp(a, "--connect"))    g_connect_host = argv[++i];  // HOST[:PORT]
    else if (arg && !strcmp(a, "--w")) w = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--h")) h = atoi(argv[++i]);
    else if (arg && !strcmp(a, "--seed")) { seed = (uint32_t)strtoul(argv[++i], NULL, 10); have_seed = 1; }
    else if (!strcmp(a, "--windowed")) g_fullscreen = 0;
    else if (arg && !strcmp(a, "--config")) snprintf(g_cfg_path, sizeof g_cfg_path, "%s", argv[++i]);
    else if (arg && !strcmp(a, "--do")) script = strdup(argv[++i]);
    else if (arg && !strcmp(a, "--script")) {
      FILE *f = fopen(argv[++i], "rb");
      if (!f) { fprintf(stderr, "cannot read %s\n", argv[i]); return 1; }
      fseek(f, 0, SEEK_END);
      long len = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (len <= 0) { fprintf(stderr, "cannot size %s (pipe? empty?)\n", argv[i]); fclose(f); return 1; }
      script = malloc((size_t)len + 1);
      if (!script || fread(script, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "cannot read %s (short read)\n", argv[i]);
        fclose(f);
        return 1;
      }
      script[len] = '\0';
      fclose(f);
    } else { fprintf(stderr, "unknown arg '%s' (--help)\n", a); return 1; }
  }

  // Clamp the FBO size: >=1 for GL, <=16384 keeps every allocation sane and
  // the screenshot's single IDAT chunk under the PNG 2^31-1 length cap.
  w = w < 1 ? 1 : w > 16384 ? 16384 : w;
  h = h < 1 ? 1 : h > 16384 ? 16384 : h;

  if (!g_cfg_path[0]) {
    char dir[480];
    exe_dir(dir, sizeof dir);
    snprintf(g_cfg_path, sizeof g_cfg_path, "%sconfig.cfg", dir);
  }
  config_load_or_create(g_cfg_path, &g_cfg);
  if (print_cfg) {
    printf("config %s\n", g_cfg_path);
    config_emit(stdout, &g_cfg);   // the SAME emitter config.cfg is written with
    return 0;
  }

  // THE SERVER FORK: before the play/harness split — a server is neither.
  // Random seed unless pinned, same contract as play mode.
  if (g_server) {
    if (script) { fprintf(stderr, "--server and --do/--script are exclusive\n"); return 1; }
    if (!have_seed) seed = seed_from_clock();
    return server_main(seed);
  }

  // THE FORK. No script means PLAY: this binary is the game, and the harness is
  // something you have to ask for.
  if (!script) {
    // Singleplayer contract: every launch gets a fresh procedural arena, unless
    // --seed asked for a specific one (which is worth keeping in native mode —
    // "play the arena that test reproduced" is a real thing to want).
    if (!have_seed) seed = seed_from_clock();
    g_boot_connect = g_connect_host;   // native_main kicks the join after start
    return native_main(w, h, seed);
  }

  g_headless = 1;
  egl_init();
  fbo_init(w, h);
  player_t *player_s = &g_players[0];   // human 0 IS the local player
  app_start(player_s, w, h, seed);
  sim_ctx_t sim = {.p = player_s};

  run_script(script, &sim);
  free(script);
  return 0;
}

