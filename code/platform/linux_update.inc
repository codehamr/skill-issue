// =============================================================================
// platform/linux_update.inc  —  update fetch over curl (upd_fetch/
// upd_thread/upd_apply/upd_relaunch), telemetry send (tele_thread), and
// native_main — the X11 play-mode entry point (opens the display).
// Proof: Xvfb rig (CLAUDE.md). One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: a fetch that half-read is a FAILED fetch — read() errors and
// EOF are distinguished, both in the body loop and the over-cap drain.
// =============================================================================
// ---------------------------------------------------------------------------
// Auto-update, POSIX half: fetch = /usr/bin/curl as a
// child process with a pinned argv, swap = write-verify-rename, restart = execv. The
// check runs on its own thread and publishes atomically — the start is never blocked.
// ---------------------------------------------------------------------------
#include <sys/wait.h>

static char **g_main_argv;   // set in main(); execv keeps the launch arguments

// The exact fetch contract, argv-pinned. -q MUST be the first argument: without it curl
// reads ~/.curlrc, and a line `insecure` or `proxy = ...` there disables TLS or
// reroutes the download — writing that file needs no rights in the game directory.
static int upd_fetch(const char *url, char *dst, size_t cap, size_t *out_len) {
  int fds[2];
  *out_len = 0;
  if (pipe(fds) != 0) { snprintf(g_upd_why, sizeof g_upd_why, "PIPE"); return 0; }
  pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]); close(fds[1]);
    snprintf(g_upd_why, sizeof g_upd_why, "FORK");
    return 0;
  }
  if (pid == 0) {
    dup2(fds[1], 1);
    close(fds[0]); close(fds[1]);
    char *const cargv[] = {
      "curl", "-q", "-fsS", "--proto", "=https", "--proto-redir", "=https",
      // THE URL NEVER CHANGES, SO IT MUST BE ASKED FRESH. Both the sums and
      // the asset live under releases/latest/download/, one fixed path per
      // platform for the life of the project, and the DATE inside is equal
      // across a same-day re-cut — so a proxy or CDN that caches by URL serves
      // a body that nothing in it can be told is stale. curl keeps no cache of
      // its own; this is for everything between here and GitHub.
      "-H", "Cache-Control: no-cache", "-H", "Pragma: no-cache",
      "--location", "--max-redirs", "5", "--connect-timeout", "5",
      "--max-time", "30", "--max-filesize", UPD_STR(UPD_FETCH_MAX), "--no-progress-meter",
      "-o", "-", (char *)url, NULL};
    execv("/usr/bin/curl", cargv);
    _exit(127);
  }
  close(fds[1]);
  // A READ ERROR IS NOT END OF BODY. curl's exit status says the transfer went through;
  // it says nothing about how much of curl's output THIS side got.
  size_t got = 0;
  int io_err = 0, over = 0;
  for (;;) {
    if (got == cap) break;
    ssize_t r = read(fds[0], dst + got, cap - got);
    if (r > 0) { got += (size_t)r; continue; }
    if (r == 0) break;
    if (errno == EINTR) continue;
    io_err = 1; break;
  }
  if (!io_err && got == cap) {   // response larger than our cap: drain + reject
    char sink[256];
    for (;;) {
      ssize_t r = read(fds[0], sink, sizeof sink);
      if (r > 0) { over = 1; continue; }
      if (r == 0) break;
      if (errno == EINTR) continue;
      io_err = 1; break;
    }
  }
  close(fds[0]);
  int st = 0;
  waitpid(pid, &st, 0);
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    snprintf(g_upd_why, sizeof g_upd_why,
             WIFEXITED(st) && WEXITSTATUS(st) == 127 ? "CURL MISSING" : "NET");
    return 0;
  }
  if (io_err) { snprintf(g_upd_why, sizeof g_upd_why, "NET"); return 0; }
  if (over) { snprintf(g_upd_why, sizeof g_upd_why, "TOO LARGE"); return 0; }
  *out_len = got;
  return 1;
}

// Writability is probed BEFORE any offer: an unwritable directory
// (AppImage mount, /usr/local/bin) must show the manual line, not download
// 800 KB per start that can go nowhere.
static void upd_probe_fs(void) {
  char exe[512], p[600];
  if (!exe_path(exe, sizeof exe)) { g_upd_writable = 0; return; }
  snprintf(p, sizeof p, "%s.new.probe.%d", exe, (int)getpid());
  int fd = open(p, O_CREAT | O_EXCL | O_WRONLY, 0600);
  if (fd >= 0) { close(fd); unlink(p); g_upd_writable = 1; }
  else g_upd_writable = 0;
}

static void *upd_thread(void *arg) {
  (void)arg;
  upd_thread_body();
  return NULL;
}

static void upd_check_start(void) {
  if (!upd_check_gate()) return;
  pthread_t th;
  if (pthread_create(&th, NULL, upd_thread, NULL) == 0) pthread_detach(th);
  else upd_spawn_failed();
}

// The telemetry resolver's Linux half — same shape as upd_check_start above,
// deliberately: getaddrinfo blocks and the caller is the frame loop.
static void *tele_thread(void *arg) {
  (void)arg;
  tele_resolve_body();
  return NULL;
}

static void tele_resolve_start(void) {
  pthread_t th;
  if (pthread_create(&th, NULL, tele_thread, NULL) == 0) pthread_detach(th);
  else atomic_store(&g_tele_ip, 0xFFFFFFFFu);   // no thread, no telemetry
}

// Download to MEMORY, hash THERE, only then touch the filesystem, and never re-read the
// written file by path (the TOCTOU rule).

// The "metadata done, become the new binary" tail. execv replaces the
// image with no CRT teardown, so no thread join is needed here (the Windows
// twin, win_relaunch, must join WASAPI).
static int upd_relaunch(const char *exe) {
  config_write(g_cfg_path, &g_cfg);
  execv(exe, g_main_argv);
  // execv returned = failed. The swapped file is already in place — the next
  // manual start runs it; nothing is lost.
  snprintf(g_upd_why, sizeof g_upd_why, "EXEC");
  return 0;
}

// Download, verify, swap, re-exec. THERE IS NO ROLLBACK: the <exe>.old copy below is
// kept because it costs a link() and is the only recovery a bad build has, but nothing
// in the program reads it and there is no UI row for it — it is a file on disk for a
// human to rename.
static int upd_apply(void) {
  size_t len = 0;
  char *buf = upd_download_verified(&len);
  if (!buf) return 0;
  char exe[512], newp[600], old[600];
  if (!exe_path(exe, sizeof exe)) { free(buf); return 0; }
  snprintf(newp, sizeof newp, "%s.new.%d", exe, (int)getpid());
  snprintf(old, sizeof old, "%s.old", exe);
  int fd = open(newp, O_WRONLY | O_CREAT | O_EXCL, 0700);
  if (fd < 0) {
    free(buf);
    snprintf(g_upd_why, sizeof g_upd_why, "WRITE");
    return 0;
  }
  size_t off = 0;
  while (off < len) {
    ssize_t w = write(fd, buf + off, len - off);
    if (w <= 0) { close(fd); unlink(newp); free(buf);
                  snprintf(g_upd_why, sizeof g_upd_why, "WRITE"); return 0; }
    off += (size_t)w;
  }
  free(buf);
  int bad = fchmod(fd, 0755) != 0 || fsync(fd) != 0;
  if (close(fd) != 0) bad = 1;
  if (bad) { unlink(newp); snprintf(g_upd_why, sizeof g_upd_why, "WRITE"); return 0; }
  unlink(old);
  if (link(exe, old) != 0) { /* best effort: a failed link loses the copy, not the swap */ }
  if (rename(newp, exe) != 0) {
    unlink(newp);
    snprintf(g_upd_why, sizeof g_upd_why, "RENAME");
    return 0;
  }
  return upd_relaunch(exe);
}

// Time-seeded arena when --seed was not given — the server fork and the play
// fork share the one formula (the comment "same contract as play mode" was
// enforced by hand while this lived twice inside main).
static uint32_t seed_from_clock(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint32_t)ts.tv_nsec * 2654435761u ^ (uint32_t)ts.tv_sec ^
         ((uint32_t)getpid() << 16);
}

static int native_main(int w, int h, uint32_t seed) {
  XSetErrorHandler(nat_xerr);
  g_xdpy = XOpenDisplay(NULL);
  if (!g_xdpy) {
    fprintf(stderr,
            "skill-issue: no X display (DISPLAY=%s).\n"
            "This binary is the game; run it from a desktop or gaming session.\n"
            "For the headless test harness use --do \"CMDS\" (see --help).\n",
            getenv("DISPLAY") ? getenv("DISPLAY") : "unset");
    return 1;
  }
  int scr = DefaultScreen(g_xdpy);
  g_xroot = RootWindow(g_xdpy, scr);
  if (g_fullscreen) {
    w = DisplayWidth(g_xdpy, scr);
    h = DisplayHeight(g_xdpy, scr);
  }
  // Auto-repeat otherwise sends a KeyRelease+KeyPress PAIR at the repeat rate, so a
  // held key flickers 1->0->1 and the sim sees a second press EDGE: a phantom extra
  // jump, reload or weapon swap.
  Bool detectable = False;
  XkbSetDetectableAutoRepeat(g_xdpy, True, &detectable);
  g_detect_repeat = detectable != False;

  A_WM_PROTOCOLS       = XInternAtom(g_xdpy, "WM_PROTOCOLS", False);
  A_WM_DELETE          = XInternAtom(g_xdpy, "WM_DELETE_WINDOW", False);
  A_NET_WM_STATE       = XInternAtom(g_xdpy, "_NET_WM_STATE", False);
  A_NET_WM_FULLSCREEN  = XInternAtom(g_xdpy, "_NET_WM_STATE_FULLSCREEN", False);
  A_NET_WM_BYPASS_COMP = XInternAtom(g_xdpy, "_NET_WM_BYPASS_COMPOSITOR", False);

  nat_window(w, h);
  g_xi_on = nat_xi2();
  g_refresh_hz = nat_refresh_hz();

  player_t *player_s = &g_players[0];   // human 0 IS the local player
  app_start(player_s, w, h, seed);
  audio_start();
  // Async, never blocking the start; harness and server sessions never
  // reach this line, which IS the "server and harness never update" rule.
  upd_check_start();
  // Boot into the menu over the LIVE arena: the match is already running behind it, so
  // SINGLEPLAYER -> START MATCH is two presses with the focus starting on the first of
  // them.
  ui_open_menu();
  // ...and the boot menu is the BOOT SCREEN: tiles over a live camera rather than a
  // panel over a freeze-frame.
  home_enter(local_player());
  // --connect HOST[:PORT]: the direct-join entry point; the ONLINE menu
  // drives the same start_connect.
  boot_connect_kick();

  // First frame before the window appears, exactly as on Windows: no black or
  // white startup flash while the shaders and the arena come up.
  render_frame(player_s, 1.0f, 0, NULL);
  eglSwapBuffers(g_egl_dpy, g_egl_surf);
  XMapRaised(g_xdpy, g_xwin);
  XFlush(g_xdpy);
  eglSwapInterval(g_egl_dpy, g_cfg.vsync ? 1 : 0);

  app_t app = {0};
  double last = nat_now();

  // Installed HERE, after every start step that may legitimately die loudly: from now
  // on ^C/`kill` must walk the full teardown below, not the default fatal handler.
  signal(SIGINT, nat_sig);
  signal(SIGTERM, nat_sig);

  while (g_running) {
    if (g_nat_quit) break;   // signal quit: same exit as WM_DELETE, teardown below
    nat_pump();
    // Pump the pad even unfocused: the kernel's evdev buffer is a ring, and a
    // paused reader overflows it (SYN_DROPPED). State just isn't ACTED on.
    nat_pad_pump(nat_now());
    if (!g_active) {   // unfocused: no sim, no rendering, no GPU burn
      struct timespec ts = {0, 5000000L};
      nanosleep(&ts, NULL);
      last = nat_now();
      app.acc = 0;
      continue;
    }

    // A pointer grab can be refused because someone else holds one (a menu, a drag, a
    // screenshot tool), and unlike a Windows cursor clip it does not come back on its
    // own.
    if (!g_grabbed && !g_menu_open) nat_grab(1);

    double now = nat_now();
    double dt = now - last;
    last = now;

    if (app_pump(&app, player_s, dt, g_mouse_dx, g_mouse_dy)) g_running = 0;
    g_mouse_dx = g_mouse_dy = 0;

    eglSwapBuffers(g_egl_dpy, g_egl_surf);
    app_sync(&app);

    // Frame limiter.
    double cap = g_cfg.fps_cap > 0 ? (double)g_cfg.fps_cap
               : g_cfg.vsync      ? g_refresh_hz
                                  : 0.0;
    if (cap > 0) {
      // Sleep the coarse part, spin the last 0.3 ms.
      double target = 1.0 / cap;
      for (;;) {
        double rem = target - (nat_now() - last);
        if (rem <= 0) break;
        if (rem > 0.0003) {
          double s = rem - 0.0003;
          struct timespec ts = {(time_t)s, (long)(fmod(s, 1.0) * 1e9)};
          nanosleep(&ts, NULL);
        }
      }
    }
  }

  // GOODBYE FIRST: every quit path through this teardown (window close, signal, in-game
  // QUIT) hands a joined slot back to the server now instead of parking it for the idle
  // timeout. Gated like leave_match's call — at boot g_cl is zeroed and its fd of 0
  // would otherwise be "closed".
  if (g_cl.state != NC_OFF) net_cl_disconnect();
  // Same ordering constraint the Windows teardown documents: signal the audio
  // thread first so it winds down while the window comes apart, and give the
  // cursor back before anything else — a crash from here on must not leave the
  // user with an invisible pointer.
  atomic_store(&g_audio_run, 0);
  nat_grab(0);
  nat_cursor(1);

  // GL objects and the EGL surface go while the window they belong to still
  // exists, and the context stays current until the last GL call is made.
  if (app.fence) glDeleteSync(app.fence);
  eglMakeCurrent(g_egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(g_egl_dpy, g_egl_surf);
  eglDestroyContext(g_egl_dpy, g_egl_ctx);
  XDestroyWindow(g_xdpy, g_xwin);
  XFlush(g_xdpy);

  // BOUNDED, like the Windows side's 800 ms WaitForSingleObject rather than an open-
  // ended join: the audio thread's blocking point is inside libasound, and a wedged
  // sound server must not be able to hold the process open forever.
  for (int i = 0; g_audio_started && i < 80; i++) {
    if (atomic_load(&g_audio_done)) break;
    struct timespec ts = {0, 10000000L};
    nanosleep(&ts, NULL);
  }
  eglTerminate(g_egl_dpy);
  XCloseDisplay(g_xdpy);
  // ...AND THE BOUND ABOVE MEANS WE LEAVE WHATEVER THE SOUND SERVER IS DOING. Returning
  // into exit() would then walk _dl_fini through libasound — dlopen'd in audio_start,
  // never dlclosed — with a thread still inside it. Two more detached threads have no
  // wait at all (upd_thread forks curl, tele_thread sits in getaddrinfo, which dlopens
  // the NSS modules). Nothing in this TU registers atexit and config_write has already
  // run on every quit path, so all exit() would still do for us is flush stdio.
#if defined(__SANITIZE_ADDRESS__)
  // _exit skips ASan's atexit hook, so a native ASan run would silently report no
  // leaks at all. LSan stops the world, which is what exit() did here before.
  void __lsan_do_leak_check(void);
  __lsan_do_leak_check();
#endif
  fflush(NULL);
  _exit(0);
}

