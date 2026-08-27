// =============================================================================
// harness/analysis.inc  —  WAV analysis, usage(), the script token reader,
// sim_ctx_t, sim_advance.
// Proof: --help. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: `./build/game --help` IS the command reference — never copy it
// into CLAUDE.md.
// =============================================================================
// ---------------------------------------------------------------------------
// Audio analysis. Nobody can listen in the container this game is developed in,
// so a sound IS its descriptors. peak/RMS/crest/L-R correlation cover level and width
// and were enough while the work was "make these buffers not be mono" — they say
// nothing about the two qualities this round is about. "Fat" is a question about the
// SPECTRUM (how much of the energy is weight and how much is information) and
// "reverberant" is a question about the ENVELOPE DECAY (how long the room keeps
// answering, and whether it darkens as it goes).
// ---------------------------------------------------------------------------

#define AN_N 4096                 // Welch block: 11.7 Hz bins at 48 kHz
static float g_an_re[AN_N], g_an_im[AN_N], g_an_pw[AN_N / 2 + 1];

// Iterative radix-2 FFT, in place. Twiddles are recomputed per butterfly, which is
// wasteful and completely irrelevant: this runs on 15 buffers, once, in a test command.
static void an_fft(float *re, float *im, int n) {
  for (int i = 1, j = 0; i < n; i++) {              // bit-reversal permutation
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    float ang = -F_TAU / (float)len;
    for (int i = 0; i < n; i += len)
      for (int k = 0; k < len / 2; k++) {
        float c = cosf(ang * (float)k), s = sinf(ang * (float)k);
        int a = i + k, b = a + len / 2;
        float xr = re[b] * c - im[b] * s, xi = re[b] * s + im[b] * c;
        re[b] = re[a] - xr; im[b] = im[a] - xi;
        re[a] += xr;        im[a] += xi;
      }
  }
}

// Averaged power spectrum of the mono sum (Hann, 50% overlap).
static void an_spec(const float *b, int frames, int ch) {
  for (int k = 0; k <= AN_N / 2; k++) g_an_pw[k] = 0;
  int blocks = 0;
  for (int off = 0; off + AN_N <= frames || blocks == 0; off += AN_N / 2) {
    for (int i = 0; i < AN_N; i++) {
      float m = 0;
      if (off + i < frames)
        for (int c = 0; c < ch; c++) m += b[(off + i) * ch + c];
      float w = 0.5f - 0.5f * cosf(F_TAU * (float)i / (float)AN_N);
      g_an_re[i] = m * w;
      g_an_im[i] = 0;
    }
    an_fft(g_an_re, g_an_im, AN_N);
    for (int k = 0; k <= AN_N / 2; k++)
      g_an_pw[k] += g_an_re[k] * g_an_re[k] + g_an_im[k] * g_an_im[k];
    blocks++;
    if (off + AN_N >= frames) break;
  }
}

// Share of the energy in each of four bands plus the energy-weighted centroid.
static void an_bands(float *band, float *centroid, float *a_gain) {
  double e[4] = {0, 0, 0, 0}, num = 0, den = 0, aw = 0;
  for (int k = 1; k <= AN_N / 2; k++) {
    float hz = (float)k * (float)SFX_HZ / (float)AN_N;
    double p = g_an_pw[k];
    e[hz < 120.0f ? 0 : hz < 500.0f ? 1 : hz < 2000.0f ? 2 : 3] += p;
    num += p * (double)hz;
    den += p;
    // A-weighting.
    double f2 = (double)hz * (double)hz;
    double r = 12194.0 * 12194.0 * f2 * f2
             / ((f2 + 20.6 * 20.6)
                * sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
                * (f2 + 12194.0 * 12194.0));
    aw += p * r * r * 1.5849;          // +2.0 dB curve normalisation at 1 kHz
  }
  double tot = e[0] + e[1] + e[2] + e[3] + 1e-30;
  for (int i = 0; i < 4; i++) band[i] = (float)(100.0 * e[i] / tot);
  *centroid = (float)(num / (den + 1e-30));
  *a_gain = (float)(aw / tot);
}

// Envelope decay, optionally band-limited. Two "when does it arrive" numbers,
// and the gap between them is the whole transient-versus-body story:
//   t_atk  time to the loudest SAMPLE — proves the sub-millisecond attack
//          exists at all. Measured on a smoothed envelope it cannot be seen:
//          a 30 Hz follower integrates over 5 ms and reported 7.6 ms for a
//          shot whose transient is 0.5 ms long.
//   t_eng  time to the loudest smoothed ENVELOPE — where the sound's WEIGHT
//          actually lands. This is the one that catches a confirmation whose
//          peak has drifted 70 ms past the event it confirms.
// t20/t60 run from t_eng to the LAST time the envelope is still within
// 20/60 dB of it, which is exactly what "reverberant" asks: not how long the
// buffer is, but how long it is still audible. Running the filters twice
// (once for the peak, once for the crossings) beats storing an envelope for a
// 2.5 s buffer and is bit for bit identical — the filters are deterministic.
static void an_decay(const float *b, int frames, int ch, float lo_hz, float hi_hz,
                     float *t_atk, float *t_eng, float *t20, float *t60) {
  float pk = 1e-12f, raw = 1e-12f;
  int i_pk = 0, i_raw = 0, i20 = 0, i60 = 0;
  for (int pass = 0; pass < 2; pass++) {
    op_t h1 = {0}, h2 = {0}, l1 = {0}, l2 = {0}, sm = {0};
    for (int i = 0; i < frames; i++) {
      float m = 0;
      for (int c = 0; c < ch; c++) m += b[i * ch + c];
      if (lo_hz > 0) { m = op_hp(&h1, m, lo_hz); m = op_hp(&h2, m, lo_hz); }
      if (hi_hz > 0) { m = op_lp(&l1, m, hi_hz); m = op_lp(&l2, m, hi_hz); }
      float a = m < 0 ? -m : m;
      float env = op_lp(&sm, a, 30.0f);                // ~5 ms integration
      if (pass == 0) {
        if (a > raw)   { raw = a; i_raw = i; }
        if (env > pk)  { pk = env; i_pk = i; }
      } else if (i >= i_pk) {
        if (env > pk * 0.1f)   i20 = i;                // -20 dB
        if (env > pk * 0.001f) i60 = i;                // -60 dB
      }
    }
  }
  *t_atk = (float)i_raw / (float)SFX_HZ;
  *t_eng = (float)i_pk / (float)SFX_HZ;
  *t20   = (float)(i20 - i_pk) / (float)SFX_HZ;
  *t60   = (float)(i60 - i_pk) / (float)SFX_HZ;
}

// One line of descriptors for a stereo buffer, in the format both `sfx` and `mixdown`
// print. `win_s` (0 = whole buffer) is the window the LOUDNESS and the SPECTRUM are
// measured over, and it has to be a window for the same reason sfx_finish takes one: a
// shot with a 1.4 s hall on it is 80% tail by frame count, so its whole-buffer band
// split describes the room and says nothing about whether the gun has a crack.
static void an_report(const char *tag, const float *b, int frames, float win_s) {
  float peak = 1e-9f;
  double sq = 0;
  int ns = frames * SFX_CH;
  for (int k = 0; k < ns; k++) {
    float a = b[k] < 0 ? -b[k] : b[k];
    if (a > peak) peak = a;
    sq += (double)b[k] * (double)b[k];
  }
  // Stereo width, printed rather than assumed: the L/R correlation of a
  // duplicated mono buffer is exactly 1.0, and a soundfield made of
  // 1.0-correlated sources has no size however good the synthesis is.
  double sl = 0, sr = 0, slr = 0;
  for (int k = 0; k < frames; k++) {
    double l = b[k * SFX_CH], r = b[k * SFX_CH + 1];
    sl += l * l; sr += r * r; slr += l * r;
  }
  double den = sqrt(sl * sr);
  double corr = den > 1e-12 ? slr / den : 1.0;
  double rms = 10.0 * log10(sq / (double)ns + 1e-30);
  // ONE loudness ruler for the whole set.
  int nw = win_s > 0 ? (int)(win_s * SFX_HZ) : frames;
  if (nw > frames || nw <= 0) nw = frames;
  double sw = 0;
  for (int k = 0; k < nw * SFX_CH; k++) sw += (double)b[k] * (double)b[k];
  double lw = 10.0 * log10(sw / (double)(nw * SFX_CH) + 1e-30);

  float band[4], cent, awg, atk, eng, t20, t60, d0, d1, d2, lo60, hi60;
  an_spec(b, nw, SFX_CH);
  an_bands(band, &cent, &awg);
  // dBA is the number "fatter / harder-hitting" is actually about.
  printf("sfx %-9s len=%4ldms peak=%5.1fdB rms=%6.1fdB L%03ld=%6.1f/%6.1fdBA "
         "crest=%4.1fdB LRcorr=%+.2f\n", tag, (long)frames * 1000 / SFX_HZ,
         20.0 * log10((double)peak), rms, (long)nw * 1000 / SFX_HZ, lw,
         lw + 10.0 * log10((double)awg + 1e-30),
         20.0 * log10((double)peak) - rms, corr);

  an_decay(b, frames, SFX_CH, 0, 0, &atk, &eng, &t20, &t60);
  // Per-band T60 is what separates "the tail is longer" from "the tail is a room": in a
  // real space the high band dies first, so a sound whose decay IS its room must read
  // T60lo > T60hi, and a tail built from one noise burst has them equal by construction
  // (the old sniper: 878 against 910, i.e. it got BRIGHTER as it died).
  an_decay(b, frames, SFX_CH, 0, 300.0f, &d0, &d1, &d2, &lo60);
  an_decay(b, frames, SFX_CH, 2000.0f, 0, &d0, &d1, &d2, &hi60);
  printf("sfx+          sub/low/mid/hi=%2.0f/%2.0f/%2.0f/%2.0f%% cent=%5.0fHz "
         "atk=%4.1fms eng=%5.1fms T20=%4dms T60=%4dms lo=%4dms hi=%4dms\n",
         (double)band[0], (double)band[1], (double)band[2], (double)band[3],
         (double)cent, (double)(atk * 1000.0f), (double)(eng * 1000.0f),
         (int)(t20 * 1000.0f), (int)(t60 * 1000.0f),
         (int)(lo60 * 1000.0f), (int)(hi60 * 1000.0f));
}

// 16-bit stereo WAV, hand-rolled 44-byte header — the audio counterpart of the
// PNG writer: lets sound design iterate headless (dump, inspect, listen).
static int write_wav(const char *path, const float *buf, int frames) {
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return 0; }
  int n = frames * SFX_CH;
  uint32_t data = (uint32_t)n * 2, riff = 36 + data, rate = SFX_HZ;
  uint32_t brate = (uint32_t)(SFX_HZ * 2 * SFX_CH);
  uint8_t h[44] = {'R','I','F','F', riff, riff >> 8, riff >> 16, riff >> 24,
                   'W','A','V','E','f','m','t',' ', 16,0,0,0, 1,0, SFX_CH,0,
                   rate, rate >> 8, rate >> 16, rate >> 24,
                   brate, brate >> 8, brate >> 16, brate >> 24,
                   2 * SFX_CH,0, 16,0,
                   'd','a','t','a', data, data >> 8, data >> 16, data >> 24};
  fwrite(h, 1, 44, f);
  for (int i = 0; i < n; i++) {
    int16_t s = (int16_t)(f_clamp(buf[i], -1.0f, 1.0f) * 32767.0f);
    uint8_t b[2] = {(uint8_t)(s & 0xFF), (uint8_t)((uint16_t)s >> 8)};
    fwrite(b, 1, 2, f);
  }
  int err = ferror(f);
  if (fclose(f) != 0) err = 1;
  if (err) fprintf(stderr, "cannot write %s\n", path);
  return !err;
}

static void usage(void) {
  // printf rather than puts so the two match-slider ranges come from the ONE
  // place that defines them — a hardcoded "1-20" here is the copy nobody greps.
  printf("skill-issue — NO ARGUMENTS PLAYS THE GAME (X11 window).\n"
       "             --do/--script run the headless harness instead.\n"
       "  --connect H[:P]    join a server on host H (port P, else mp_port)\n"
       "                     while the local match runs until the first snapshot\n"
       "  --server           dedicated server: sim only, no GL/audio/window.\n"
       "                     with --port N --cap-bots N (bots in the arena)\n"
       "                     --cap-public N (arena size, public door)\n"
       "                     --lobbies N (independent arenas in this ONE\n"
       "                     process on this ONE port; a new one fills only\n"
       "                     once the previous is full)\n"
       "                     --bot-fill N (bots top a populated arena up to N\n"
       "                     humans+bots; an empty arena holds none; default 2)\n"
       "                     --idle-timeout N (drop a silent client after N\n"
       "                     seconds, 1..120, default 4)\n"
       "                     --skill 0|1|2 --fraglimit N and\n"
       "                     --ticks N (exit after N ticks; 0 = run forever)\n"
       "                     --net-debug (verbose: trims/skips, one line each)\n"
       "  --windowed         play in a window rather than borderless fullscreen\n"
       "  --w N --h N        window size when playing / framebuffer size for the\n"
       "                     harness (default 1280x720; fullscreen uses the display)\n"
       "  --seed N           map seed (harness default 1337; a played session is\n"
       "                     time-seeded unless you pass this)\n"
       "  --config PATH      config file (default: config.cfg next to binary)\n"
       "  --print-config     dump effective config and exit\n"
       "  --do \"CMDS\"        run script commands, headless (also: --script FILE)\n"
       "script commands (whitespace/;-separated):\n"
       "  wait N             advance N ticks (120 Hz)\n"
       "  +A / -A            press/release action: forward back left right jump crouch\n"
       "                     lean_left lean_right fire ads reload swap slot1 slot2\n"
       "                     score view (view toggles 3rd person)\n"
       "  weapon ar|sr       switch weapon instantly (no raise animation)\n"
       "  tap A              press action for exactly one tick\n"
       "  look DX DY         inject mouse counts (scaled by sense)\n"
       "  aim X Y Z          snap yaw/pitch to look at a world point\n"
       "  warp X Y Z         teleport the player (peek/camera framing tests)\n"
       "  pos                print tick/position/velocity/eye/grounded/lean/shots\n"
       "  lean               print lean command/clearance/eye+head offset/exposure\n"
       "  wpn                print ammo/spread/recoil/bloom/ads/reload\n"
       "  bot                print every bot: position/hp/state/target + belief\n"
       "  aimbot             snap yaw/pitch onto the nearest bot's head\n"
       "  botfreeze [on|off] toggle or set ALL bot AI (stable capture target)\n"
       "  rag N [BOT]      ragdoll per tick + summary, across the corpse handover\n"
       "  ragx N [BOT]     the RENDERED corpse against the ground and the solids:\n"
       "                   float/hang (in the air) sink/pen (in geometry),\n"
       "                   span/hgt (sprawled or balled). figv adds the joints\n"
       "  ragsoak N        ragx over EVERY corpse and dead entity in a live\n"
       "                   match for N ticks — the multi-body soak\n"
       "  flinch           6 impact locations on bot 0 (stages itself): peak\n"
       "                   joint travel per case + how far apart the 6 films are\n"
       "  hud on|off         show/hide gameplay HUD (menus remain visible)\n"
       "  showfps on|off     show/hide the FPS counter idempotently\n"
       "  puppet on|off      drive bot 0 by script instead of AI (anim testbed)\n"
       "  puppet move DX DZ | speed S | face DEG | crouch 0|1 | lean -1..1 |\n"
       "                     ads 0|1 | jump | slide | warp X Z\n"
       "  skel N             advance N ticks printing bot 0 skeleton state\n"
       "  kine N             advance N ticks printing solved foot/hand kinematics\n"
       "  bots N             set bot count %d-%d (applies next tick)\n"
       "  skill easy|normal|hard   set bot difficulty\n"
       "  fraglimit N        set the frag limit %d-%d\n"
       "  match              print match state + one score line per entity\n"
       "  restart            queue a match restart + fresh procedural map (next tick)\n"
       "  sfx DIR            dump all synthesized sounds as WAV + level stats\n"
       "  sfxlog N           print every audio routing decision for N ticks\n"
       "  mixdown P.wav N    render the real voice mixer + limiter for N ticks\n"
       "  map                theme/wind/surface treatment + all collision solids\n"
       "  sun [ELEV AZI]     the arena's light: elevation/azimuth in DEGREES,\n"
       "                     the extinguished sun colour, the exposure and the\n"
       "                     shadow box. With two arguments it MOVES the sun —\n"
       "                     the whole 6.5..54 range is reviewable without\n"
       "                     hunting seeds for it\n"
       "  mood [P|R G S|off] the mood: print, or FORCE it and regenerate the\n"
       "                     arena. One number = PRESET index 0..2 within the\n"
       "                     arena's theme (exact latents, reviewable); three\n"
       "                     = raw ruin/growth/strew latents in 0..1; off =\n"
       "                     back to the seed's own draw\n"
       "  scat               the scatter plan's census: per-family site counts\n"
       "                     and what planning cost in ms\n"
       "  trace N            advance N ticks, printing pos each tick\n"
       "  shot PATH.png      render + write screenshot\n"
       "  capture V.rgb A.wav FPS N  RGB24+WAV capture at FPS=60|120 for N frames\n"
       "  cam X Y Z YAW PIT  override camera for later shots (cam off: back to player)\n"
       "  cambot DX DY DZ    camera at bot+offset looking at the bot (cambot off)\n"
       "  camorbit R H D L   orbit bot 0: radius, height, deg/s, look height\n"
       "                     (camorbit off returns to the previous player camera)\n"
       "  vmhand             wrist bend / forearm-to-bore / palm length, per hand\n"
       "  vmorbit YAW PIT D W  viewmodel turntable, deg+m, W=0 trigger wrist\n"
       "                     1 support wrist  2 both  3 the trigger blade\n"
       "  vmbore YAW PIT D BORE RISE FOV  the same turntable pivoted ON THE\n"
       "                     BORE (gun-local metres along it and off it) with a\n"
       "                     real lens: the macro instrument for weapon detail\n"
       "  camaim Y           height on the bot that cambot looks at (default 0.9)\n"
       "machine-checked proofs (each must read 0 / ok):\n"
       "  figcheck N [DIST]  bot 0 figure over N ticks: open/flip/dup/zfight/\n"
       "                     near/cross/degen. cross = two surfaces passing\n"
       "                     through each other at a shallow angle (a seam the\n"
       "                     tessellation decides, not the geometry)\n"
       "                     (DIST = camera distance, to sweep the profile tiers)\n"
       "  figv               toggle printing each figcheck offender's location\n"
       "  figm               every figure part's measured extent in mm + bone lengths\n"
       "  figbury [head]     third-person weapon vs the BODY: deepest vertex\n"
       "                     inside the trunk, %% of the weapon hidden in it,\n"
       "                     and how far the BUTT PAD is buried. `head` asks\n"
       "                     the same of the SKULL against the trunk instead\n"
       "  vmcheck            same analysis, both weapons x 11 ADS steps x 7 poses\n"
       "  vmsight            hand/forearm verts inside the eye's sight cone (must be 0)\n"
       "  vmtrig             trigger guard + blade against the HAND'S MESH, per\n"
       "                     weapon x reload station x first/third person\n"
       "  mapcheck N         regenerate N arenas; count coplanar same-facing faces\n"
       "  parity             24 player-vs-bot gameplay rows, ok/MISMATCH each\n"
       "  bothear            authoritative shot/step hearing and investigation proof\n"
       "  botweapon          bot weapon-choice/swap/cooldown hysteresis proof\n"
       "  botmemory          bot line-of-sight memory proof\n"
       "  botmemoryobserve   a reacquisition may not difference velocity across\n"
       "                     the occlusion gap (proof)\n"
       "  botflank           bot flank anchor/commitment/abort proof\n"
       "  padbackend         normalized gamepad mapper proof (std/XInput/HID)\n"
       "  tacstat N          bot tactics census incl. belief memory/search/reacq\n"
       "  budget             scene/peak/max verts + drops (render a frame FIRST —\n"
       "                     it only accumulates while the scene is being built)\n"
       "gamepad commands (raw state exactly as a platform pump writes it):\n"
       "  pad AXIS V         set raw axis: lx ly rx ry (-1..1, +y=up) lt rt (0..1)\n"
       "  pad btn B 0|1      set button level: a b x y lb rb lt rt select start\n"
       "                     l3 r3 up down left right p1 p2 p3 p4\n"
       "  pad on|off         mark a pad connected / clear all pad state\n"
       "  padsync            one shared pad frame step (edges, nav, trig hysteresis)\n"
       "  padstat            processed pad state + actions held through the binds\n"
       "  padscan [N]        the REAL /dev/input scan for N frames (default\n"
       "                     600): the node table, which one is ACTIVE, and\n"
       "                     every hand-over. The on-device pad proof\n"
       "  padcurve           f(0..1) table for linear/standard/dynamic (proof)\n"
       "  padlook N          N ticks of right-stick look, yaw/pitch delta in deg\n"
       "menu/UI commands (uiframe and shot both run one UI logic+draw pass):\n"
       "  menu               toggle the pause menu\n"
       "  home on|off|T      the boot screen (tiles + live showcase); T jumps\n"
       "                     the director's clock to T seconds into the loop\n"
       "  cursor X Y         set UI cursor (framebuffer px)\n"
       "  mdown / mup        press / release the UI mouse button\n"
       "  wheel up|down      one mouse-wheel notch (cycles the weapon on the\n"
       "                     next appframe; ignored while a menu is open)\n"
       "  key TOKEN          feed a key token to an active rebind capture\n"
       "  nav DIR            queue pad navigation for the next uiframe/shot:\n"
       "                     up down left right ok back; tabl/tabr are ignored\n"
       "  dev on|off         show/hide the DEV TOOLS root action (config key\n"
       "                     devmode, default off; required before a DEV-page rig)\n"
       "  uistat             print menu page/control/focus/modal state + pad cfg\n"
       "  uiframe            run one UI pass without a screenshot\n"
       "  appframe N         N frames through the real frame step (app_frame);\n"
       "                     with the menu open this runs 0 ticks — the pause\n"
       "                     proof. Schedules audio voices; avoid in mixdown\n"
       "  cfg                print live config values\n"
       "  netpack N          encode/decode round-trip over N random states;\n"
       "                     prints the worst quantization error per field\n"
       "  netfuzz N          N random/garbage packets into every decoder; no\n"
       "                     crash, every accepted record in bounds (run asan)\n"
       "  netpredict N       advance N, build+encode+decode a snapshot, verify\n"
       "                     reconstruction (mm) and the resend dedup property\n"
       "  netlagcomp [R]     shot vs a target rewound R ticks: HIT with\n"
       "                     compensation, miss without, residual in mm\n"
       "  netstall [G]       the live input queue: dup-free redundancy, gap\n"
       "                     fill rule, rate cap, and a G-tick far-ahead jump\n"
       "  netanim N [S]      plants at 120 Hz vs from an S-strided snapshot\n"
       "  netfill            fill/cap invariants over a join/leave timeline\n"
       "  netfigure          a foreign figure's IMPULSES: the shot puts the\n"
       "                     rifle on the aim axis and kicks it, the reload\n"
       "                     runs and ends, the round flashes and flinches\n"
       "                     the body, and our own predicted reload survives\n"
       "                     the server's echo of it\n"
       "  netcarry           the carriage round trip: aim intent, carry state\n"
       "                     and the mid-air tuck through from_live -> wire ->\n"
       "                     apply -> the client's own ramp -> anim_tick\n"
       "  netloop N          N ticks through sim_advance vs the server loop's\n"
       "                     tick body from one saved state; prints MISMATCH\n"
       "                     with the first divergent tick, or ok\n"
       "  netclient H P N [ACTS]  headless client: connect to a --server on\n"
       "                     H:P, drive N input ticks holding ACTS (comma-\n"
       "                     separated action names, default forward), print\n"
       "                     handshake + reconcile + event-channel stats\n"
       "                     (localhost end-to-end, no GL). `fire` in ACTS must\n"
       "                     move ev_applied: that is the server's shot coming\n"
       "                     back as an event\n"
       "  netdeath           online death -> ragdoll -> corpse (client half)\n"
       "  netleave [drop]    online->local teardown: every count must be 0 and\n"
       "                     the session must land on the entry screen. `drop`\n"
       "                     runs the same teardown from a dead server instead\n"
       "  tele H P KIND [DT] one telemetry packet (NP_TELE) at a --server on\n"
       "                     H:P through the live encoder. KIND is boot|ui|sp|\n"
       "                     mp; DT (default 60, clamped 0..%d) is the beat's\n"
       "                     seconds. Fire-and-forget: grep the server log.\n"
       "                     Sends a FIXED install id — point it at a local\n"
       "                     --server, not the public one, or it adds a\n"
       "                     permanent phantom player to that append-only log\n"
       "  netstat            print the server digest line (the SAME function\n"
       "                     the --server mode prints every 10 s)\n"
       "  netdebug           toggle per-event net diagnostics: one line per\n"
       "                     reconcile correction, backlog trim and far-ahead\n"
       "                     skip (--net-debug is the --server equivalent)\n"
       "  humans N           set the human roster to N (slots 1+ spawn idle;\n"
       "                     entity ids 21+, see the three-range rule)\n"
       "  localent N         force g_local_ent (identity-sweep instrument;\n"
       "                     diff sfxlog/HUD against the id-0 run)\n"
       "  updinfo            version, own asset name, the FULL asset table\n"
       "                     (the naming contract), the update status line and\n"
       "                     the menu-open re-check policy as never/just-inside/\n"
       "                     just-outside the throttle, per update state\n"
       "  updcheck P [DATE]  run the update comparison against a local\n"
       "                     SHA256SUMS file P; DATE overrides the local\n"
       "                     version for UPDATE/DOWNGRADE cases\n\n",
       (int)SP_BOTS_DEF.lo, (int)SP_BOTS_DEF.hi,
       (int)SP_FRAG_DEF.lo, (int)SP_FRAG_DEF.hi,
       (int)NET_TELE_DT_MAX);   // same reason as the slider ranges above: a
                                // hardcoded "0..90" in the help is the copy
                                // nobody greps when the boundary moves
}

typedef struct {
  player_t *p;
  int held[IN_COUNT];
  long tick;
  cam_override_t cam;
} sim_ctx_t;

static void sim_advance(sim_ctx_t *s) {
  player_tick(s->p, s->held);
  net_hist_record();   // write-only lag-comp history; nothing reads it in SP,
                       // so the byte gate is untouched (the net* proofs read it).
  // Drive the AUDIO ROUTING from the headless sim too, so `sfxlog` and `mixdown`
  // exercise exactly the code the played frame loop runs (the same audio_frame_drain).
  if (g_audio_route) {
    g_sfx_log_tick = s->tick;
    audio_frame_drain(s->p);
  }
  s->tick++;
  g_num_events = 0;  // event lines were printed at push time; no-op after a drain
}

static void capture_orbit_advance(cam_override_t *cam, int fps) {
  if (!cam->orbit) return;
  cam->orbit_phase += cam->orbit_rate * DEG2RAD / (float)fps;
  v3 bp = g_bots[0].pos;
  cam->eye = (v3){{bp.x + sinf(cam->orbit_phase) * cam->orbit_radius,
                   bp.y + cam->orbit_height,
                   bp.z + cosf(cam->orbit_phase) * cam->orbit_radius}};
  v3 d = v3_sub(v3_add(bp, (v3){{0, cam->orbit_look, 0}}), cam->eye);
  cam->yaw = atan2f(d.x, -d.z);
  cam->pitch = f_clamp(atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)),
                       -PITCH_MAX, PITCH_MAX);
}

static void capture_remove_outputs(const char *rgb_path, const char *wav_path) {
  remove(rgb_path);
  remove(wav_path);
}

static int capture_same_file(const char *a, const char *b) {
  struct stat sa, sb;
  return stat(a, &sa) == 0 && stat(b, &sb) == 0 &&
         sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int capture_is_symlink(const char *path) {
  struct stat st;
  return lstat(path, &st) == 0 && S_ISLNK(st.st_mode);
}

static void capture_run(sim_ctx_t *s, const char *rgb_path,
                        const char *wav_path, int fps, long frames) {
  if (fps != 60 && fps != 120) {
    fprintf(stderr, "capture: fps must be 60 or 120\n");
    exit(1);
  }
  if (frames <= 0) {
    fprintf(stderr, "capture: frames must be positive\n");
    exit(1);
  }

  const size_t tick_samples = 400;
  const size_t ticks_per_frame = (size_t)(TICK_HZ / fps);
  size_t nframes = (size_t)frames;
  if ((size_t)R.width > SIZE_MAX / 3 ||
      (size_t)R.height > SIZE_MAX / ((size_t)R.width * 3)) {
    fprintf(stderr, "capture: size overflow\n");
    exit(1);
  }
  size_t rgb_frame_bytes = (size_t)R.width * (size_t)R.height * 3;
  if (nframes > SIZE_MAX / rgb_frame_bytes ||
      nframes > SIZE_MAX / ticks_per_frame ||
      nframes * ticks_per_frame > SIZE_MAX / tick_samples) {
    fprintf(stderr, "capture: size overflow\n");
    exit(1);
  }
  size_t audio_frames = nframes * ticks_per_frame * tick_samples;
  if (audio_frames > (size_t)INT_MAX ||
      audio_frames > (size_t)(UINT32_MAX - 36u) / (2u * SFX_CH) ||
      audio_frames > SIZE_MAX / (SFX_CH * sizeof(float))) {
    fprintf(stderr, "capture: size overflow\n");
    exit(1);
  }
  if (capture_is_symlink(rgb_path) || capture_is_symlink(wav_path)) {
    fprintf(stderr, "capture: output paths must not be symlinks\n");
    exit(1);
  }
  if (!strcmp(rgb_path, wav_path) || capture_same_file(rgb_path, wav_path)) {
    fprintf(stderr, "capture: output paths must differ\n");
    exit(1);
  }

  struct stat video_before;
  int video_existed = stat(rgb_path, &video_before) == 0;
  FILE *video = fopen(rgb_path, "wb");
  if (!video) {
    fprintf(stderr, "capture: cannot open video\n");
    exit(1);
  }
  if (capture_same_file(rgb_path, wav_path)) {
    fclose(video);
    if (!video_existed) remove(rgb_path);
    fprintf(stderr, "capture: output paths must differ\n");
    exit(1);
  }
  FILE *audio_probe = fopen(wav_path, "wb");
  if (!audio_probe) {
    fclose(video);
    remove(rgb_path);
    fprintf(stderr, "capture: cannot open audio\n");
    exit(1);
  }
  if (fclose(audio_probe) != 0) {
    fclose(video);
    capture_remove_outputs(rgb_path, wav_path);
    fprintf(stderr, "capture: cannot open audio\n");
    exit(1);
  }

  float *audio = malloc(audio_frames * SFX_CH * sizeof *audio);
  if (!audio) {
    fclose(video);
    capture_remove_outputs(rgb_path, wav_path);
    fprintf(stderr, "capture: out of memory\n");
    exit(1);
  }

  int old_audio_route = g_audio_route;
  float old_dev_rate = g_dev_rate;
  const char *error = NULL;
  size_t audio_at = 0;
  g_audio_route = 1;
  g_dev_rate = 1.0f;
  for (long frame = 0; frame < frames && !error; frame++) {
    for (size_t tick = 0; tick < ticks_per_frame; tick++) {
      sim_advance(s);
      audio_mix(audio + audio_at * SFX_CH, (uint32_t)tick_samples, SFX_CH);
      audio_at += tick_samples;
    }
    capture_orbit_advance(&s->cam, fps);
    render_frame(s->p, 1.0f, fps, &s->cam);
    if (!write_rgb_frame(video, R.width, R.height))
      error = "capture: cannot write video";
  }
  g_audio_route = old_audio_route;
  g_dev_rate = old_dev_rate;

  if (fclose(video) != 0 && !error) error = "capture: cannot write video";
  if (!error && !write_wav(wav_path, audio, (int)audio_frames))
    error = "capture: cannot write audio";
  free(audio);

  if (error) {
    capture_remove_outputs(rgb_path, wav_path);
    fprintf(stderr, "%s\n", error);
    exit(1);
  }
  printf("capture frames=%ld fps=%d video=%s audio=%s\n",
         frames, fps, rgb_path, wav_path);
}

static void sim_print(const sim_ctx_t *s) {
  const player_t *p = s->p;
  printf("t=%ld pos=(%.3f %.3f %.3f) vel=(%.3f %.3f %.3f) eye=%.3f dip=%.3f "
         "grounded=%d crouch=%d slide=%d lean=%.3f shots=%d\n",
         s->tick, (double)p->pos.x, (double)p->pos.y, (double)p->pos.z,
         (double)p->vel.x, (double)p->vel.y, (double)p->vel.z,
         (double)p->eye, (double)p->land_dip,
         p->grounded, p->crouched, p->sliding, (double)p->lean, p->shots);
}

static int action_from_name(const char *n) {
  for (int i = 0; i < IN_COUNT; i++)
    if (!strcmp(n, ACTION_DEF[i].script)) return i;
  fprintf(stderr, "script: unknown action '%s'\n", n);
  exit(1);
}

// Point the player at a world point FROM THE EYE THE ROUND LEAVES. While leaning that
// eye is up to 0.28 m off the body's centre line, and aiming from the centre line
// instead put every scripted shot a body-width wide of the target at 10 m — the test
// would then be measuring the harness, not the peek.
static void aim_at(player_t *p, v3 tgt) {
  for (int i = 0; i < 2; i++) {
    v3 e = player_eye_pos(p);
    float dx = tgt.x - e.x, dy = tgt.y - e.y, dz = tgt.z - e.z;
    p->yaw = atan2f(dx, -dz);
    p->pitch = f_clamp(atan2f(dy, sqrtf(dx * dx + dz * dz)), -PITCH_MAX, PITCH_MAX);
  }
}

static char *g_tok_pushback;

static char *take_tok(void) {
  if (g_tok_pushback) {
    char *t = g_tok_pushback;
    g_tok_pushback = NULL;
    return t;
  }
  return strtok(NULL, " \t\r\n;");
}

static char *next_tok(void) {
  char *t = take_tok();
  if (!t) { fprintf(stderr, "script: missing argument\n"); exit(1); }
  return t;
}

// Every numeric argument in the ladder used to go through a bare atol/atoi/strtof with
// the end pointer thrown away, so a typo parsed as 0 and said nothing: `--do "wait abc;
// pos"` printed t=0 — the wait was a no-op and the test looked like it had run.
static long parse_n(const char *t) {
  char *e;
  errno = 0;
  long v = strtol(t, &e, 10);
  // ERANGE is as fatal as trailing junk: a silently LONG_MAX-clamped count is
  // a test that runs a different length than it says.
  if (e == t || *e || errno == ERANGE) {
    fprintf(stderr, "script: '%s' is not an integer\n", t);
    exit(1);
  }
  return v;
}
// An OPTIONAL trailing number.
static long opt_n(long dflt) {
  char *t = take_tok();
  if (!t) return dflt;
  char *e;
  errno = 0;
  long v = strtol(t, &e, 10);
  if (e == t || *e || errno == ERANGE) { g_tok_pushback = t; return dflt; }
  return v;
}

static float parse_f(const char *t) {
  char *e;
  float v = strtof(t, &e);
  // Reject non-finite results (literal nan/inf and ERANGE overflow, which strtof
  // reports as +/-HUGE_VALF) — a NaN-poisoned run still exits 0 and looks like a pass.
  if (e == t || *e || f_is_nonfinite(v)) {
    fprintf(stderr, "script: '%s' is not a finite number\n", t);
    exit(1);
  }
  return v;
}
static long  next_n(void) { return parse_n(next_tok()); }
static float next_f(void) { return parse_f(next_tok()); }

static int next_on_off(const char *command) {
  const char *arg = next_tok();
  if (!strcmp(arg, "on")) return 1;
  if (!strcmp(arg, "off")) return 0;
  fprintf(stderr, "script: %s expects on or off\n", command);
  exit(1);
}

// Everything a proof rig stages, saved by VALUE on the rig's own stack frame so rigs
// can nest (botmemory drives bothear mid-proof and asserts the restore).
typedef struct {
  bot_t         bots[MAX_BOTS];
  player_t      players[MAX_HUMANS];
  match_t       match;
  solid_t       solids[MAX_SOLIDS];
  event_t       events[MAX_EVENTS];
  unsigned char humans_on[MAX_HUMANS];
  int      num_solids, num_events, frozen, predicting, ev_peak;
  long     ev_drops, tick;
  uint64_t rng_bot;
} proof_world_t;

static void proof_world_save(proof_world_t *w) {
  memcpy(w->bots, g_bots, sizeof w->bots);
  memcpy(w->players, g_players, sizeof w->players);
  w->match = g_match;
  memcpy(w->solids, g_solids, sizeof w->solids);
  memcpy(w->events, g_events, sizeof w->events);
  memcpy(w->humans_on, g_humans_on, sizeof w->humans_on);
  w->num_solids = g_num_solids;
  w->num_events = g_num_events;
  w->frozen     = g_bots_frozen;
  w->predicting = g_predicting;
  w->ev_peak    = g_ev_peak;
  w->ev_drops   = g_ev_drops;
  w->tick       = g_tick;
  w->rng_bot    = g_rng_bot;
}

static void proof_world_restore(const proof_world_t *w) {
  memcpy(g_bots, w->bots, sizeof w->bots);
  memcpy(g_players, w->players, sizeof w->players);
  g_match = w->match;
  memcpy(g_solids, w->solids, sizeof w->solids);
  memcpy(g_events, w->events, sizeof w->events);
  memcpy(g_humans_on, w->humans_on, sizeof w->humans_on);
  g_num_solids  = w->num_solids;
  g_num_events  = w->num_events;
  g_bots_frozen = w->frozen;
  g_predicting  = w->predicting;
  g_ev_peak     = w->ev_peak;
  g_ev_drops    = w->ev_drops;
  g_tick        = w->tick;
  g_rng_bot     = w->rng_bot;
}

static void bothear_emit_shot(int src, v3 pos) {
  weapon_t w = {.cur = WPN_AR};
  wpn_fill_mags(&w);
  gun_shoot(src, &w, pos, 0.0f, 0.0f, (v3){{0,0,0}}, 1, &g_rng_bot);
}

