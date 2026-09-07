// =============================================================================
// harness/analysis.inc — audio analysis, usage(), script tokens and the
// real-tick filming controller with pose, event and mixer timing witnesses.
// Proof: --help. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: `./build/game --help` IS the command reference — never copy it
// into CLAUDE.md.
// =============================================================================
// ---------------------------------------------------------------------------
// Audio measurements describe level, spectrum, stereo width and envelope decay.
// They complement listening, which remains necessary for perceptual acceptance.
// Real mixer proofs cover admission, lifecycle and measured cue timing; these
// file/sample witnesses do not measure device or loudspeaker latency.
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
enum { WAV_MAX_FRAMES = (UINT32_MAX - 36u) / (2u * SFX_CH) };
static_assert(WAV_MAX_FRAMES <= INT_MAX / SFX_CH, "WAV sample index fits int");
static int wav_frames_valid(int frames) { return frames >= 0 && frames <= WAV_MAX_FRAMES; }
static int write_wav(const char *path, const float *buf, int frames) {
  if (!wav_frames_valid(frames) || (frames && !buf)) {
    fputs("invalid WAV frame count or buffer\n", stderr); return 0;
  }
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
    if (f_is_nonfinite(buf[i])) { fclose(f); fputs("nonfinite WAV sample\n",stderr); return 0; }
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
       "                     --cap-public N (public slots 1..7; slot 0 reserved)\n"
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
       "  --windowed         play in a window for this run (overrides the config's\n"
       "                     FULLSCREEN row)\n"
       "  --w N --h N        window size when playing / framebuffer size for the\n"
       "                     harness (default 1280x720; fullscreen uses the display)\n"
       "  --seed N           map seed (harness default 1337; a played session is\n"
       "                     time-seeded unless you pass this)\n"
       "  --config PATH      config file (default: config.cfg next to binary)\n"
       "  --print-config     dump effective config and exit\n"
       NATIVE_DIAG_HELP
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
       "  wpn                print ammo/barrel recoil/ads/reload + cosmetic VM state\n"
       "  bot                print every bot: position/hp/state/target + belief\n"
       "  aimbot             snap yaw/pitch onto the nearest bot's head\n"
       "  filmtrack E Y P    continuous pose-face tracking; breathing amplitudes in degrees\n"
       "  filmtrack off|stat|log 0|1  filming lifecycle, bounds and shot/frame witnesses\n"
       "  filmtrackproof     actual motion, face hit, identity/loss and snap controls\n"
       "  filmcueproof       actual reload milestone cursor/rate/delay/reset checks\n"
       "  botfreeze [on|off] toggle or set ALL bot AI (stable capture target)\n"
       "  rag N [BOT]      ragdoll per tick + summary, across the corpse handover\n"
       "  ragx N [BOT]     the RENDERED corpse against the ground and the solids:\n"
       "                   float/hang (in the air) sink/pen (in geometry),\n"
       "                   span/hgt (sprawled or balled). figv adds the joints\n"
       "  ragsoak N        ragx over EVERY corpse and dead entity in a live\n"
       "                   match for N ticks — the multi-body soak\n"
       "  figmotion          dense emitted FP/3P reload, cancel and brace hand transitions\n"
       "  fighit             20mm visible head/body ray census versus actual hits/history\n"
       "  flinch           6 impact locations on bot 0 (stages itself): peak\n"
       "                   joint travel per case + how far apart the 6 films are\n"
       "  hud on|off         show/hide overlays; menus and weapon optics remain\n"
       "  dbgview ao|bloom|alpha|off  show a screen-space buffer as the picture\n"
       "  grade C K S        display look: curve (0 ACES, 1 AgX), contrast,\n"
       "                     saturation (defaults 0 1.03 1.10; separate from CLEAR/FILMIC)\n"
       "  showfps on|off     show/hide the FPS counter idempotently\n"
       "  puppet on|off      drive bot 0 by script instead of AI (anim testbed)\n"
       "  puppet pitch DEG   set the puppet's vertical aim for deterministic filming\n"
       "  puppet move DX DZ | speed S | face DEG | crouch 0|1 | lean -1..1 |\n"
       "                     ads 0|1 | ready 0|1 | jump | slide | warp X Z\n"
       "  naturalcheck       real jumps, tuck/landing, calm carry and action gates\n"
       "  gaitcheck          seven production gaits: toe release, contacts, IK and restaging\n"
       "  animbench N        seven-figure animation CPU timing over N ticks (240..120000)\n"
       "  skel N             advance N ticks printing bot 0 skeleton state\n"
       "  kine N             advance N ticks printing solved foot/hand kinematics\n"
       "  bots N             set bot count %d-%d (applies next tick)\n"
       "  skill easy|normal|hard   set bot difficulty\n"
       "  fraglimit N        set %d-%d frags; 0, off or unlimited disables it\n"
       "  match              print match state + one score line per entity\n"
       "  restart            queue a match restart + fresh procedural map (next tick)\n"
       "  audiosteps PATH sr|cancel  record real repeated/cancel input edges\n"
       "  audiotrace begin|write PATH|end  observe contiguous mixer cue times\n"
       "  audiotail PATH.wav finish current voices and wet tail without sim ticks\n"
       "  audiocheck [mix|catalogue] hard catalogue quality and stepped mixer lifecycle\n"
       "  acousticcheck      geometry, mirrored passes, bounded wet tails and reset proof\n"
       "  sfx DIR            dump all synthesized sounds as WAV + level stats\n"
       "  sfxlog N           print offered audio triggers for N ticks (bounded log)\n"
       "  mixdown P.wav N    isolated take: N ticks, then quiet tail drain (10 s bound)\n"
       "  map                theme/wind/surface, collision solids + fracture edges\n"
       "  weatherproof       seed selection, roofs, RNG neutrality and delayed thunder\n"
       "  sun [ELEV AZI]     the arena's light: elevation/azimuth in DEGREES,\n"
       "                     the extinguished sun colour, the exposure and the\n"
       "                     shadow box. With two arguments it MOVES the sun —\n"
       "                     the whole 6.5..54 range is reviewable without\n"
       "                     hunting seeds for it\n"
       "  biome [NAME|off]   quarry/dunes/marsh/forest/frost/night/aurora;\n"
       "                     regenerate with that treatment, or restore seed draw\n"
       "  mood [P|R G S|off] the mood: print, or FORCE it and regenerate the\n"
       "                     arena. One number = PRESET index 0..2 within the\n"
       "                     arena's theme (exact latents, reviewable); three\n"
       "                     = raw ruin/growth/strew latents in 0..1; off =\n"
       "                     back to the seed's own draw\n"
       "  scat               the scatter plan's census: per-family site counts\n"
       "                     and what planning cost in ms\n"
       "  trace N            advance N ticks, printing pos each tick\n"
       "  shot PATH.png      render + write screenshot\n"
       "  bench N            render the current view N times: ms/frame\n"
       "  benchpass N        the same, per pass (GPU timers) + the wall clock\n"
       "  capture V.rgb A.wav FPS N  RGB24+WAV capture at FPS=60|120 for N frames\n"
       "  cam X Y Z YAW PIT  override camera for later shots (cam off: back to player)\n"
       "  cambot DX DY DZ    camera at bot+offset looking at the bot (cambot off)\n"
       "  camorbit R H D L   orbit bot 0: radius, height, deg/s, look height\n"
       "                     (camorbit off returns to the previous player camera)\n"
       "  vmhand             FP Hip/ADS/reload/recoil wrist/bore/palm matrix; hard\n"
       "                     firing bend <45, support bore 35..65 deg, live clamp=0\n"
       "  vmorbit YAW PIT D W  viewmodel turntable, deg+m, W=0 trigger wrist\n"
       "                     1 support wrist  2 both  3 the trigger blade\n"
       "  vmbore YAW PIT D BORE RISE FOV  the same turntable pivoted ON THE\n"
       "                     BORE (gun-local metres along it and off it) with a\n"
       "                     real lens: the macro instrument for weapon detail\n"
       "  camaim Y           height on the bot that cambot looks at (default 0.9)\n"
       "machine-checked proofs (each must read 0 / ok):\n"
       "  feedbackproof      direction, truthful confirmation and life-boundary proof\n"
       "  framepace          wall timing, sample bounds and native fixture determinism\n"
       "  figcheck N [DIST] [ar|sr]  bot 0 figure over N ticks: open/flip/dup/zfight/\n"
       "                     near/cross/degen. cross = two surfaces passing\n"
       "                     through each other at a shallow angle (a seam the\n"
       "                     tessellation decides, not the geometry)\n"
       "                     (DIST = camera distance, to sweep the profile tiers)\n"
       "  slidecheck         eight-direction slide clearance, knee/foot contact roles,\n"
       "                     bend-plane continuity and bone-length residual\n"
       "  armcheck           AR/SR run starts and slow aim to the pitch limit: bounded\n"
       "                     elbows, weapon frame and arm-to-torso clearance\n"
       "  figv               toggle printing each figcheck offender's location\n"
       "  leanstep           hard Lean-foot fixtures: open/wall/corner/slot, sign\n"
       "                     flip, gait/wire/event neutrality and snapshot cadence\n"
       "  figm               every figure part's measured extent in mm + bone lengths\n"
       "  figbury [head]     third-person weapon vs the BODY: deepest vertex\n"
       "                     inside the trunk, %% of the weapon hidden in it,\n"
       "                     and how far the BUTT PAD is buried. `head` asks\n"
       "                     the same of the SKULL against the trunk instead\n"
       "  vmcheck            both weapons x 11 ADS steps x 6 base poses, plus real\n"
       "                     Hip/ADS back/lift/side+/-/overshoot recoil frames\n"
       "  vmsight            hand/forearm verts inside the eye's sight cone (must be 0)\n"
       "  vmscope            secured ocular/axis, dense aperture/crop, real shader\n"
       "                     bounds, recoil, closed passes and target/state recovery\n"
       "  vmframe            signed FP pitch/yaw/roll plus stock/wrist distances;\n"
       "                     SR idle/action gate; Fire T0/T1 proves the exact VM-to-\n"
       "                     world FOV remap, prediction pair and pose/EV T0 anchor;\n"
       "                     pose-only re-solves must preserve all anim state\n"
       "  gunwall            the wall gate: three standoffs x both weapons — a\n"
       "                     walled muzzle refuses the trigger silently and\n"
       "                     tucks the carriage; a clear one fires (hard gate)\n"
       "  boresight          the reticle contract: AR/SR x hip/ADS x level+tilted aim\n"
       "                     x 4.5/10.5/18.5 m against a synthetic face — impact minus\n"
       "                     the analytic eye-ray intersection on it must be inside 1 cm\n"
       "                     for an aimed round (cone 0) and inside HIP SPREAD's own\n"
       "                     angle over the path from the hip (hard gate)\n"
       "  recoil             compact gun contract: first-shot axis, fixed up + alternating\n"
       "                     side steps, held-trigger freeze, linear exact recenter, and\n"
       "                     identical jump/slide spread for AR/SR (hard gate)\n"
       "  vmtrig             hard hand contract over FP/3P Hip/ADS, reload\n"
       "                     15/35/55/75/90, SR bolt, recoil peaks, Low-Ready\n"
       "                     and death: guard/blade, actual allowed bite and\n"
       "                     bidirectional exact/proxy forbidden crossings. Gates:\n"
       "                     guard >=0, blade >=-4.5 mm, forbidden <=0.5 mm,\n"
       "                     proxy/actual bite 2..4 mm, allowed wall <=4.5 mm,\n"
       "                     live clamp=0 (death is reported separately)\n"
       "  decorcheck         emitted crown coverage, authored supports and capacity\n"
       "  mapcheck N         arenas: coplanar/capacity + through-brick-opening rays\n"
       "  parity             24 player-vs-bot gameplay rows, ok/MISMATCH each\n"
       "  botsuppress        authoritative near-pass/impact and bounded response proof\n"
       "  bottactics         routes, tactical decisions and physical/network lean proof\n"
       "  netloss            lost/reordered input recovery and wraparound proof\n"
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
       "  appframe N [T]     N real frames, T ticks/frame (default 1, max 12);\n"
       "                     with the menu open this runs 0 ticks — the pause\n"
       "                     proof. Schedules audio voices; avoid in mixdown\n"
       "  homeui             current --w/--h HOME gate: masked hover/click pixel\n"
       "                     diff, root/focus/uistat stability, fit and ui_peak\n"
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
       "  netanim N [S]      plants plus a quantized full-rate/S-strided Lean ramp:\n"
       "                     ankle, active-side, afoot/phase and event hard gates\n"
       "  netfill            fill/cap invariants over a join/leave timeline\n"
       "  netfigure          a foreign figure's IMPULSES: the shot puts the\n"
       "                     rifle on the aim axis and kicks it, the reload\n"
       "                     runs and ends, the round flashes and flinches\n"
       "                     the body, and our own predicted reload survives\n"
       "                     the server's echo of it\n"
       "  defaultsproof      canonical startup/RESET/config roundtrip and server tuning authority\n"
       "  netrecoil          >5 cm rollback with AR single+burst shots in replay:\n"
       "                     VM history/end-state, weapon/RNG, no replayed body-carry\n"
       "                     latch, local echo-once and foreign EV_FIRE response\n"
       "  netcarry           the carriage round trip: aim intent, carry state\n"
       "                     and the mid-air tuck through from_live -> wire ->\n"
       "                     apply -> the client's own ramp -> anim_tick\n"
       "  netloop N          N ticks through sim_advance vs the server loop's\n"
       "                     tick body from one saved state; prints MISMATCH\n"
       "                     with the first divergent tick, or ok\n"
       "  nethandshake      UDP version rejection, source and live-session guards\n"
       "  netclient H P N [ACTS]  headless client: connect to a --server on\n"
       "                     H:P, drive N input ticks holding ACTS (comma-\n"
       "                     separated action names, default forward), print\n"
       "                     handshake + reconcile + event-channel stats\n"
       "                     (localhost end-to-end, no GL). `fire` in ACTS must\n"
       "                     move ev_applied: that is the server's shot coming\n"
       "                     back as an event\n"
       "  netdeath           online death -> recoil pose -> ragdoll -> corpse, plus\n"
       "                     respawn VM reset (client half)\n"
       "  spstart           direct local arena UI/frame, input and audio lifecycle proof\n"
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
       "                     high entity ids, see the three-range rule)\n"
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
       (int)SP_FRAG_DEF.lo, SP_FRAG_MAX,
       (int)NET_TELE_DT_MAX);   // same reason as the slider ranges above: a
                                // hardcoded "0..90" in the help is the copy
                                // nobody greps when the boundary moves
}

// HOW FAR OFF THE CALM AIM THE BARREL ITSELF IS, in degrees: the magnitude of exactly
// the two recoil components gun_bore_angles adds. It is a
// measurement and lives here because only measurements ask it — the sim never needs
// the magnitude, only the angles — and one copy of it keeps `wpn`'s column and the
// recoil proof from developing separate opinions about where the rifle is pointing.
// Deliberately NOT gun_spread: that is the cone the STANCE opens around this axis.
static float gun_bore_offset(const weapon_t *w) {
  float p = w->rec_p;
  float y = w->rec_y;
  return sqrtf(p * p + y * y);
}

// Filming observes one completed pose and one life. It only changes the harness
// player's base look; authored weapon recoil and spread keep their usual owner.
#define FILM_SPEED_DEG 180.0f
#define FILM_ACCEL_DEG 1200.0f
#define FILM_FACE_HALF_WIDTH 0.050f
#define FILM_FACE_HALF_HEIGHT 0.035f
#define FILM_TRACK_GAIN 24.0f

typedef struct {
  int enabled, target, life, seen, valid, lost, log, violations, shots, face_shots, head_hits;
  long ticks, shot_tick;
  int shot_index, shot_kind, shot_hit;
  v3 shot_origin, shot_at, shot_direction;
  float velocity[2], observed[2], previous_target[2], previous_look[2];
  float amp_yaw, amp_pitch, max_speed, max_accel, max_error;
  v3 face, right, up, forward, bore;
} film_track_t;

static int film_face_anchor(int entity, v3 *point, v3 *right, v3 *up, v3 *forward) {
  if (entity < 0 || entity >= MAX_ENTS) return 0;
  const anim_t *anim = ent_anim(entity);
  const pose_t *pose = &anim->pose;
  pose_head_frame(pose, anim_head_cant(anim), NULL, right, up, forward);
  // The upper brow station lies in the visible visor band and the independently
  // checked head hitbox. Filming does not change the collision anatomy.
  const fig_head_band_t *a = &FIG_FACE_PROFILE[FIG_FACE_BROW].band;
  const fig_head_band_t *b = &FIG_FACE_PROFILE[FIG_FACE_TOP].band;
  float vertical = a->up + 0.15f * (b->up - a->up);
  float front = a->forward + a->depth +
      0.15f * (b->forward + b->depth - a->forward - a->depth);
  *point = v3_add(pose->head, v3_add(v3_scale(*up, vertical), v3_scale(*forward, front)));
  for (int axis = 0; axis < 3; axis++)
    if (f_is_nonfinite(point->e[axis]) || f_is_nonfinite(right->e[axis]) ||
        f_is_nonfinite(up->e[axis]) || f_is_nonfinite(forward->e[axis])) return 0;
  return v3_dot(*right, *right) > 0.5f && v3_dot(*up, *up) > 0.5f;
}

static void film_limit_pair(float pair[2], float limit) {
  float magnitude = sqrtf(pair[0] * pair[0] + pair[1] * pair[1]);
  if (magnitude > limit) {
    pair[0] *= limit / magnitude;
    pair[1] *= limit / magnitude;
  }
}

static void film_track_begin(film_track_t *track, player_t *player,
                             int target, float yaw_amp, float pitch_amp) {
  int log = track->log;
  *track = (film_track_t){.enabled = 1, .target = target,
      .life = g_spawn_seq[target], .amp_yaw = yaw_amp, .amp_pitch = pitch_amp, .log = log};
  track->previous_look[0] = player->yaw;
  track->previous_look[1] = player->pitch;
}

static int film_face_error(v3 eye, v3 direction, const film_track_t *track,
                           float *horizontal, float *vertical);

static void film_track_tick(film_track_t *track, player_t *player) {
  if (!track->enabled) return;
  float desired[2] = {0, 0};
  v3 eye = player_eye_pos(player);
  int alive = track->target >= 0 && track->target < MAX_ENTS &&
      g_spawn_seq[track->target] == track->life && ent_alive(track->target);
  track->valid = alive && film_face_anchor(track->target, &track->face,
                                          &track->right, &track->up, &track->forward);
  if (track->valid) {
    v3 delta = v3_sub(track->face, eye);
    float length = v3_len(delta);
    track->valid = length > 0.05f &&
      !los_blocked(eye, v3_scale(delta, 1.0f / length), length - 0.01f);
    if (track->valid) {
      desired[0] = atan2f(delta.x, -delta.z);
      desired[1] = f_clamp(atan2f(delta.y, hypotf(delta.x, delta.z)), -PITCH_MAX, PITCH_MAX);
      float time = (float)track->ticks * TICK_DT;
      desired[0] += track->amp_yaw * DEG2RAD * sinf(time * 1.55f);
      desired[1] += track->amp_pitch * DEG2RAD * sinf(time * 1.1f + 0.4f);
    }
  }
  float next_velocity[2] = {0, 0};
  const float current[2] = {player->yaw, player->pitch};
  if (track->valid) {
    for (int axis = 0; axis < 2; axis++) {
      float difference = wrap_pi(desired[axis] - track->previous_target[axis]);
      float measured = track->seen ? difference / TICK_DT : 0;
      track->observed[axis] += 0.25f * (measured - track->observed[axis]);
      float error = wrap_pi(desired[axis] - current[axis]);
      next_velocity[axis] = track->observed[axis] + FILM_TRACK_GAIN * error;
      track->previous_target[axis] = desired[axis];
    }
    film_limit_pair(next_velocity, FILM_SPEED_DEG * DEG2RAD);
    track->seen = 1;
  } else {
    track->seen = 0;
    track->observed[0] = track->observed[1] = 0;
    track->lost++;
  }
  float change[2] = {next_velocity[0] - track->velocity[0],
                     next_velocity[1] - track->velocity[1]};
  film_limit_pair(change, FILM_ACCEL_DEG * DEG2RAD * TICK_DT);
  float old_velocity[2] = {track->velocity[0], track->velocity[1]};
  for (int axis = 0; axis < 2; axis++) track->velocity[axis] += change[axis];
  player->yaw = wrap_pi(player->yaw + track->velocity[0] * TICK_DT);
  player->pitch = f_clamp(player->pitch + track->velocity[1] * TICK_DT, -PITCH_MAX, PITCH_MAX);
  float actual[2] = {wrap_pi(player->yaw - track->previous_look[0]) / TICK_DT,
                     (player->pitch - track->previous_look[1]) / TICK_DT};
  float accel[2] = {(actual[0] - old_velocity[0]) / TICK_DT,
                    (actual[1] - old_velocity[1]) / TICK_DT};
  float speed = hypotf(actual[0], actual[1]) / DEG2RAD;
  float acceleration = hypotf(accel[0], accel[1]) / DEG2RAD;
  track->max_speed = fmaxf(track->max_speed, speed);
  track->max_accel = fmaxf(track->max_accel, acceleration);
  track->violations += speed > FILM_SPEED_DEG + 0.05f || acceleration > FILM_ACCEL_DEG + 1.0f;
  track->previous_look[0] = player->yaw;
  track->previous_look[1] = player->pitch;
  track->bore = gun_bore_dir(&player->wp, player->yaw, player->pitch);
  float face_x = 0, face_y = 0;
  int measured = track->valid && film_face_error(eye, bore_axis(player->yaw, player->pitch),
                                                track, &face_x, &face_y);
  if (measured && track->ticks >= 120)
    track->max_error = fmaxf(track->max_error, hypotf(face_x, face_y));
  track->ticks++;
  if (!alive && hypotf(track->velocity[0], track->velocity[1]) < 1e-6f) track->enabled = 0;
  if (track->log)
    printf("filmtrack tick=%ld entity=%d life=%d visible=%d yaw=%.7f pitch=%.7f "
           "speed=%.5f accel=%.5f recoil_y=%.7f recoil_p=%.7f measured=%d error_x=%.7f error_y=%.7f violations=%d\n",
           g_tick + 1, track->target, track->life, track->valid,
           (double)player->yaw, (double)player->pitch, (double)speed, (double)acceleration,
           (double)player->wp.rec_y, (double)player->wp.rec_p, measured,
           (double)face_x, (double)face_y, track->violations);
}

static int film_face_error(v3 eye, v3 direction, const film_track_t *track,
                           float *horizontal, float *vertical) {
  float denominator = v3_dot(direction, track->forward);
  if (fabsf(denominator) < 1e-5f) return 0;
  float distance = v3_dot(v3_sub(track->face, eye), track->forward) / denominator;
  if (f_is_nonfinite(distance) || distance <= 0) return 0;
  v3 offset = v3_sub(v3_add(eye, v3_scale(direction, distance)), track->face);
  *horizontal = v3_dot(offset, track->right);
  *vertical = v3_dot(offset, track->up);
  return !f_is_nonfinite(*horizontal) && !f_is_nonfinite(*vertical);
}

static void film_track_events(film_track_t *track, long script_tick) {
  if (!track->enabled || !track->ticks) return;
  for (int i = 0; i < g_num_events; i++) {
    const event_t *event = &g_events[i];
    if (event->type != EV_FIRE || event->src != g_local_ent) continue;
    v3 origin = v3_sub(event->a, v3_scale(event->b, event->f));
    float x = 0, y = 0;
    int measurable = track->valid && film_face_error(origin, event->b, track, &x, &y);
    int face = measurable && fabsf(x) <= FILM_FACE_HALF_WIDTH && fabsf(y) <= FILM_FACE_HALF_HEIGHT;
    track->shot_tick = g_tick; track->shot_index = event->i;
    track->shot_kind = event->k; track->shot_hit = event->dst;
    track->shot_origin = origin; track->shot_at = event->a; track->shot_direction = event->b;
    track->shots++;
    track->face_shots += face;
    track->head_hits += event->dst == track->target && event->k == 3;
    if (track->log)
      printf("filmshot tick=%ld script_tick=%ld source=%d target=%d life=%d index=%d weapon=%d "
             "hit=%d kind=%d measured=%d face=%d x=%.7f y=%.7f "
             "bore_x=%.7f bore_y=%.7f bore_z=%.7f actual_x=%.7f actual_y=%.7f actual_z=%.7f\n",
             g_tick, script_tick, event->src, track->target, track->life, event->i, event->wpn,
             event->dst, event->k, measurable, face, (double)x, (double)y,
             (double)track->bore.x, (double)track->bore.y, (double)track->bore.z,
             (double)event->b.x, (double)event->b.y, (double)event->b.z);
  }
}

typedef struct {
  player_t *p;
  int held[IN_COUNT];
  long tick;
  cam_override_t cam;
  film_track_t film;
} sim_ctx_t;

// Project the actual rendered matrix. Pixel coordinates use top-left origin.
static int film_project(mat4 vp, v3 point, float cx, float cy, float rx, float ry,
                         float *x, float *y) {
  const float *m = vp.m;
  float w = m[3] * point.x + m[7] * point.y + m[11] * point.z + m[15];
  if (f_is_nonfinite(w) || w <= 1e-6f) return 0;
  *x = cx + rx * (m[0] * point.x + m[4] * point.y + m[8] * point.z + m[12]) / w;
  *y = cy - ry * (m[1] * point.x + m[5] * point.y + m[9] * point.z + m[13]) / w;
  return !f_is_nonfinite(*x) && !f_is_nonfinite(*y);
}

static void film_print_pixel(const char *key, mat4 matrix, v3 point,
                              float cx, float cy, float rx, float ry) {
  float x, y;
  printf(",\"%s\":", key);
  if (film_project(matrix, point, cx, cy, rx, ry, &x, &y))
    printf("[%.5f,%.5f]", (double)x, (double)y);
  else fputs("null", stdout);
}

static void film_frame(const sim_ctx_t *sim, uint64_t scope_before) {
  const film_track_t *track = &sim->film;
  if (!track->log) return;
  const weapon_t *weapon = &sim->p->wp;
  if (weapon->reload_t > 0) {
    int duration = wpn_reload_ticks(weapon->cur, weapon->reload_tac);
    gunanim_t pose = gun_anim(weapon);
    printf("filmreload {\"source_tick\":%ld,\"script_tick\":%ld,\"weapon\":%d,"
      "\"tactical\":%d,\"remaining\":%d,\"duration\":%d,\"mag\":%.7f,\"bolt\":%.7f,"
      "\"punch\":%.7f}\n", g_tick, sim->tick - 1, weapon->cur, weapon->reload_tac,
      weapon->reload_t, duration, (double)pose.mag, (double)pose.bolt, (double)pose.punch);
  }
  if (!track->ticks) return;
  frame_cam_t cam = rf_camera(sim->p, 1.0f, &sim->cam);
  int scope = g_scope_scene_serial != scope_before;
  mat4 projection = scope ? rf_scope_matrix(&cam) : cam.vp;
  float sx = (float)R.width / g_ui_vw, sy = (float)R.height / g_ui_vh;
  float cx = scope ? g_scope_cx * sx : R.width * 0.5f;
  float cy = scope ? g_scope_cy * sy : R.height * 0.5f;
  float rx = scope ? g_scope_r * sx : R.width * 0.5f;
  float ry = scope ? g_scope_r * sy : R.height * 0.5f;
  printf("filmframe {\"source_tick\":%ld,\"script_tick\":%ld,\"target\":%d,"
    "\"life\":%d,\"scope\":%d,\"reticle_kind\":%d,\"reticle_pixel\":",
    g_tick, sim->tick - 1, track->target, track->life, scope, g_rf_sight_witness.kind);
  if (g_rf_sight_witness.kind)
    printf("[%.5f,%.5f]", (double)g_rf_sight_witness.x, (double)g_rf_sight_witness.y);
  else fputs("null", stdout);
  printf(",\"projection_center_pixel\":[%.5f,%.5f],\"shot\":%s",
    (double)cx, (double)cy, track->shot_tick == g_tick ? "true" : "false");
  film_print_pixel("tracked_face_pixel", projection, track->face, cx, cy, rx, ry);
  v3 face, right, up, forward;
  if (track->target >= 0 && track->target < MAX_ENTS &&
      g_spawn_seq[track->target] == track->life &&
      film_face_anchor(track->target, &face, &right, &up, &forward))
    film_print_pixel("rendered_face_pixel", projection, face, cx, cy, rx, ry);
  else fputs(",\"rendered_face_pixel\":null", stdout);
  if (track->shot_tick == g_tick) {
    printf(",\"shot_index\":%d,\"hit\":%d,\"kind\":%d", track->shot_index,
      track->shot_hit, track->shot_kind);
    film_print_pixel("intended_bore_pixel", projection,
      v3_add(track->shot_origin, v3_scale(track->bore, 100)), cx, cy, rx, ry);
    film_print_pixel("actual_hit_pixel", projection, track->shot_at, cx, cy, rx, ry);
  }
  puts("}");
}

static void film_event_census(const sim_ctx_t *sim) {
  if (!sim->film.log) return;
  for (int i = 0; i < g_num_events; i++) {
    const event_t *event = &g_events[i];
    const char *name = event->type == EV_FIRE ? "fire" :
      event->type == EV_HIT ? "hit" : event->type == EV_HIT_HEAD ? "head" :
      event->type == EV_KILL ? "kill" : event->type == EV_RELOAD ? "reload" :
      event->type == EV_RELOAD_CANCEL ? "reload_cancel" : NULL;
    if (!name) continue;
    printf("filmevent {\"source_tick\":%ld,\"script_tick\":%ld,\"type\":%d,"
      "\"name\":\"%s\",\"src\":%d,\"dst\":%d,\"index\":%d,\"weapon\":%d,"
      "\"kind\":%d,\"local\":%s}\n", g_tick, sim->tick, event->type, name,
      event->src, event->dst, event->i, event->wpn, event->k,
      event->src == g_local_ent ? "true" : "false");
  }
}

static void sim_advance(sim_ctx_t *s) {
  // Tick-driven ambient voices can be offered inside player_tick, before
  // the event drain. Both producers observe the current script clock.
  g_sfx_log_tick = s->tick;
  film_track_tick(&s->film, s->p);
  player_tick(s->p, s->held);
  film_track_events(&s->film, s->tick);
  film_event_census(s);
  net_hist_record();   // write-only lag-comp history; nothing reads it in SP,
                       // so the byte gate is untouched (the net* proofs read it).
  // Drive the AUDIO ROUTING from the headless sim too, so `sfxlog` and `mixdown`
  // exercise exactly the code the played frame loop runs (the same audio_frame_drain).
  if (g_audio_route) {
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

  const size_t tick_samples = SFX_HZ / TICK_HZ;
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
    uint64_t scope_before = g_scope_scene_serial;
    render_frame(s->p, 1.0f, fps, &s->cam);
    film_frame(s, scope_before);
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
  long          event_tick[MAX_EVENTS];
  unsigned char event_feedback[MAX_EVENTS];
  unsigned char humans_on[MAX_HUMANS];
  int      num_solids, num_events, frozen, predicting, prediction_replay, ev_peak;
  long     ev_drops, tick;
  uint64_t rng_bot;
} proof_world_t;

static void proof_world_save(proof_world_t *w) {
  memcpy(w->bots, g_bots, sizeof w->bots);
  memcpy(w->players, g_players, sizeof w->players);
  w->match = g_match;
  memcpy(w->solids, g_solids, sizeof w->solids);
  memcpy(w->events, g_events, sizeof w->events);
  memcpy(w->event_tick, g_event_tick, sizeof w->event_tick);
  memcpy(w->event_feedback, g_event_feedback, sizeof w->event_feedback);
  memcpy(w->humans_on, g_humans_on, sizeof w->humans_on);
  w->num_solids = g_num_solids;
  w->num_events = g_num_events;
  w->frozen     = g_bots_frozen;
  w->predicting = g_predicting;
  w->prediction_replay = g_prediction_replay;
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
  memcpy(g_event_tick, w->event_tick, sizeof w->event_tick);
  memcpy(g_event_feedback, w->event_feedback, sizeof w->event_feedback);
  memcpy(g_humans_on, w->humans_on, sizeof w->humans_on);
  g_num_solids  = w->num_solids;
  g_num_events  = w->num_events;
  g_bots_frozen = w->frozen;
  g_predicting  = w->predicting;
  g_prediction_replay = w->prediction_replay;
  g_ev_peak     = w->ev_peak;
  g_ev_drops    = w->ev_drops;
  g_tick        = w->tick;
  g_rng_bot     = w->rng_bot;
}

static void bothear_emit_shot(int src, v3 pos) {
  weapon_t w = {.cur = WPN_AR};
  wpn_fill_mags(&w);
  gun_shoot(src, &w, pos, 0.0f, 0.0f, 1, 0, &g_rng_bot);
}

// Float-buffer quality gates are fixed before palette edits. The 300ms level
// window excludes a weapon's long tail; reloads use their complete timeline.
enum {
  AQ_LENGTH = 1, AQ_FINITE = 2, AQ_PEAK = 4, AQ_LEVEL = 8, AQ_DC = 16,
  AQ_EDGE = 32, AQ_TAIL = 64, AQ_CORR = 128, AQ_TIMING = 256
};
typedef struct {
  float len_lo, len_hi, rms_lo, rms_hi, corr_lo, corr_hi, attack_hi;
} audio_quality_bound_t;
static const audio_quality_bound_t AUDIO_QUALITY[SFX_COUNT] = {
  [SFX_FIRE_AR] = {.18f,.80f,.05f,.45f,-.10f,1.001f,.025f},
  [SFX_FIRE_SR] = {.25f,2.50f,.08f,.48f,-.10f,1.001f,.025f},
  [SFX_FAR_AR] = {.18f,1.40f,.025f,.35f,-.10f,.90f,.090f},
  [SFX_FAR_SR] = {.20f,1.40f,.025f,.35f,-.10f,.90f,.090f},
  [SFX_HIT] = {.10f,.40f,.06f,.48f,.10f,1.001f,.025f},
  [SFX_HIT_HEAD] = {.10f,.40f,.06f,.48f,.10f,1.001f,.025f},
  [SFX_KILL] = {.20f,.80f,.06f,.50f,.10f,1.001f,.025f},
  [SFX_KILL_HEAD] = {.20f,.80f,.06f,.50f,.10f,1.001f,.025f},
  [SFX_HURT] = {.10f,.40f,.025f,.30f,.10f,1.001f,.030f},
  [SFX_IMPACT] = {.08f,.35f,.015f,.25f,-.10f,1.001f,.025f},
  [SFX_WHIZZ] = {.08f,.35f,.020f,.30f,-.10f,.95f,.025f},
  [SFX_STEP] = {.08f,.22f,.015f,.20f,-.10f,.95f,.025f},
  [SFX_DRY] = {.08f,.18f,.020f,.20f,.10f,1.001f,.025f},
  [SFX_RELOAD_AR] = {1.0f,2.50f,.03f,.25f,.10f,1.001f,0},
  [SFX_RELOAD_SR] = {1.0f,2.50f,.03f,.25f,.10f,1.001f,0},
  [SFX_FLESH] = {.08f,.30f,.015f,.25f,.10f,1.001f,.025f},
  [SFX_RELOAD_AR_TAC] = {.80f,2.50f,.03f,.25f,.10f,1.001f,0},
  [SFX_THUNDER] = {4.7f,4.9f,.015f,.25f,-.10f,.20f,.60f},
  [SFX_RAIN] = {3.1f,3.3f,.008f,.12f,-.10f,.20f,3.2f},
  [SFX_WIND] = {4.1f,4.3f,.008f,.18f,-.10f,.20f,4.2f}
};
typedef struct {
  double peak, rms, dc, edge, tail, corr;
  float attack, energy;
} audio_quality_t;
static unsigned audio_quality_measure(int id, const float *buf, int frames,
                                        audio_quality_t *out) {
  *out = (audio_quality_t){0};
  if ((unsigned)id >= SFX_COUNT || !buf || frames < 1 || frames > SFX_HZ * 5)
    return AQ_LENGTH;
  const audio_quality_bound_t *bound = &AUDIO_QUALITY[id];
  double seconds = (double)frames / SFX_HZ;
  unsigned fail = seconds < bound->len_lo || seconds > bound->len_hi ||
                  bound->len_hi <= 0 ? AQ_LENGTH : 0;
  int reload = id == SFX_RELOAD_AR || id == SFX_RELOAD_AR_TAC || id == SFX_RELOAD_SR;
  int window = reload ? frames : frames < SFX_HZ * 3 / 10 ? frames : SFX_HZ * 3 / 10;
  double sum[2] = {0}, square[2] = {0}, product = 0, level = 0, tail = 0;
  int tail_start = frames > SFX_HZ / 100 ? frames - SFX_HZ / 100 : 0;
  for (int f = 0; f < frames; f++) {
    for (int c = 0; c < SFX_CH; c++) {
      float sample = buf[f * SFX_CH + c];
      if (f_is_nonfinite(sample)) return fail | AQ_FINITE;
      double value = sample, absolute = fabs(value);
      if (absolute > out->peak) out->peak = absolute;
      sum[c] += value; square[c] += value * value;
      if (f < window) level += value * value;
      if (f >= tail_start) tail += value * value;
      if (f > 0) {
        double delta = fabs(value - buf[(f - 1) * SFX_CH + c]);
        if (delta > out->edge) out->edge = delta;
      }
      if ((f == 0 || f == frames - 1) && absolute > .0001) fail |= AQ_EDGE;
    }
    product += (double)buf[f * SFX_CH] * buf[f * SFX_CH + 1];
  }
  out->rms = sqrt(level / ((double)window * SFX_CH));
  out->dc = fmax(fabs(sum[0]), fabs(sum[1])) / frames;
  out->tail = sqrt(tail / ((double)(frames - tail_start) * SFX_CH));
  double den = sqrt(square[0] * square[1]);
  out->corr = den > 1e-12 ? product / den : 0;
  if (out->peak < .15 || out->peak > .94) fail |= AQ_PEAK;
  if (out->rms < bound->rms_lo || out->rms > bound->rms_hi) fail |= AQ_LEVEL;
  if (out->dc > .006) fail |= AQ_DC;
  if (out->edge > 1.90) fail |= AQ_EDGE;
  if (out->tail > .0002) fail |= AQ_TAIL;
  if (out->corr < bound->corr_lo || out->corr > bound->corr_hi) fail |= AQ_CORR;
  float t20, t60;
  an_decay(buf, frames, SFX_CH, 0, 0, &out->attack, &out->energy, &t20, &t60);
  if (reload) {
    int weapon = id == SFX_RELOAD_SR ? WPN_SR : WPN_AR;
    int tactical = id == SFX_RELOAD_AR_TAC;
    float duration = (float)wpn_reload_ticks(weapon, tactical) / TICK_HZ;
    float beat = (tactical ? RELOAD_SEAT_TAC : RELOAD_BEAT[weapon][1]) * duration;
    int expected = (int)((duration + .22f) * SFX_HZ);
    if (abs(frames - expected) > 1 || fabsf(out->energy - beat) > .040f ||
        fabsf(out->attack - beat) > .035f) fail |= AQ_TIMING;
  } else if (out->attack > bound->attack_hi) fail |= AQ_TIMING;
  return fail;
}
static int audiocheck_catalogue(void) {
  int fail = 0, controls = 0;
  for (int id = 0; id < SFX_COUNT; id++) {
    audio_quality_t measured;
    unsigned errors = audio_quality_measure(id, g_sfx_buf[id], g_sfx_len[id], &measured);
    printf("audiocheck sound=%s frames=%d peak=%.5f rms=%.5f dc=%.6f edge=%.5f "
           "tail=%.7f corr=%.4f attack_ms=%.3f energy_ms=%.3f errors=%u\n",
           SFX_NAME[id], g_sfx_len[id], measured.peak, measured.rms, measured.dc,
           measured.edge, measured.tail, measured.corr,
           (double)(measured.attack * 1000), (double)(measured.energy * 1000), errors);
    fail |= errors != 0;
  }
  int frames = g_sfx_len[SFX_FIRE_AR];
  size_t bytes = (size_t)frames * SFX_CH * sizeof(float);
  float *control = malloc(bytes);
  if (!control) fatal("audio proof out of memory");
  audio_quality_t result;
  memset(control, 0, bytes);
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_PEAK) != 0;
  memcpy(control, g_sfx_buf[SFX_FIRE_AR], bytes); control[48] = 1.0f;
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_PEAK) != 0;
  memcpy(control, g_sfx_buf[SFX_FIRE_AR], bytes);
  uint32_t nan = UINT32_C(0x7fc00001); memcpy(control + 48, &nan, sizeof nan);
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_FINITE) != 0;
  memcpy(control, g_sfx_buf[SFX_FIRE_AR], bytes);
  for (int f = 0; f < frames * SFX_CH; f++) control[f] += .020f;
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_DC) != 0;
  memset(control, 0, bytes);
  int offset = SFX_HZ * 12 / 100;
  memcpy(control + offset * SFX_CH, g_sfx_buf[SFX_FIRE_AR],
         (size_t)(frames - offset) * SFX_CH * sizeof(float));
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_TIMING) != 0;
  memcpy(control, g_sfx_buf[SFX_FIRE_AR], bytes);
  for (int f = frames - SFX_HZ / 100; f < frames; f++)
    for (int c = 0; c < SFX_CH; c++) control[f * SFX_CH + c] = .01f;
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_TAIL) != 0;
  memcpy(control, g_sfx_buf[SFX_FIRE_AR], bytes);
  for (int f = 0; f < frames; f++) control[f * SFX_CH + 1] *= -1;
  controls += (audio_quality_measure(SFX_FIRE_AR, control, frames, &result) & AQ_CORR) != 0;
  controls += (audio_quality_measure(SFX_FIRE_AR, control, 0, &result) & AQ_LENGTH) != 0;
  free(control);
  fail |= controls != 8;
  printf("audiocheck catalogue rows=%d controls=%d/8 %s\n", SFX_COUNT, controls,
         fail ? "FAIL" : "ok");
  return fail;
}

static int audiocheck_routes(void) {
  int local = g_local_ent, fail = 0;
  v3 listener = g_listen, right = g_listen_r;
  uint64_t rng = g_rng_audio;
  g_local_ent = 0; g_listen = (v3){{0, 0, 0}}; g_listen_r = (v3){{1, 0, 0}};
  // Two remote shooters in one source tick retain distinct event identities.
  audio_session_reset(); audio_mix_discard_pending(); audio_mix_sync_epoch();
  fail |= !audio_trace_begin();
  for (int i = 0; i < 2; i++) {
    event_t e = {.type = EV_FIRE, .wpn = WPN_AR, .src = 2 + i, .dst = 7,
                 .i = 17 + i, .k = 1, .a = {{10, 0, 0}}, .b = {{1, 0, 0}}, .f = 1};
    audio_event(&e);
  }
  audio_play(SFX_DRY, .7f, 1, 0, 0);
  fail |= g_audio_trace_n != 3;
  if (g_audio_trace_n == 3) {
    for (int i = 0; i < 2; i++) {
      const audio_trace_event_t *e = &g_audio_trace[i].event;
      fail |= !e->serial || e->type != EV_FIRE || e->src != 2 + i || e->dst != 7 ||
        e->index != 17 + i || e->weapon != WPN_AR || e->kind != 1;
    }
    fail |= g_audio_trace[0].event.serial == g_audio_trace[1].event.serial ||
      g_audio_trace[2].event.serial != 0;
  }
  audio_trace_end();
  const int ids[WPN_COUNT][2] = {{SFX_RELOAD_AR, SFX_RELOAD_AR_TAC}, {SFX_RELOAD_SR, SFX_RELOAD_SR}};
  for (int weapon = 0; weapon < WPN_COUNT; weapon++)
    for (int tac = 0; tac < 2; tac++)
      for (int remote = 0; remote < 2; remote++) {
        audio_session_reset(); audio_mix_discard_pending(); audio_mix_sync_epoch();
        g_dev_rate = 1;
        event_t e = {.type = EV_RELOAD, .wpn = weapon, .k = tac, .src = remote ? 2 : 0,
                     .a = {{.5f, 0, 0}}};
        audio_on_event(&e); audio_mix_admit();
        int id = ids[weapon][tac], tag = remote ? 4 : 1, found = 0;
        for (int i = 0; i < MAX_VOICES; i++) if (g_voices[i].active) {
          const voice_t *v = &g_voices[i]; found++;
          fail |= v->id != id || v->tag != tag || v->rate != 1 ||
              v->delay != (remote ? (int)(.5f / 343 * SFX_HZ) : 0);
        }
        fail |= found != 1;
        e.type = EV_RELOAD_CANCEL; audio_on_event(&e); audio_mix_admit();
        found = 0;
        for (int i = 0; i < MAX_VOICES; i++) if (g_voices[i].active) {
          found++; fail |= !g_voices[i].dying || g_voices[i].tag != tag;
        }
        fail |= found != (remote ? 0 : 1);
      }
  g_local_ent = local; g_listen = listener; g_listen_r = right; g_rng_audio = rng;
  printf("audiocheck routing reload_variants=8 local_remote_cancel=8 fixed_rate=1 event_identity=2 %s\n", fail ? "FAIL" : "ok");
  return fail;
}

static void audiocheck_reset(void) {
  audio_session_reset();
  audio_mix_discard_pending();
  audio_mix_sync_epoch();
  atomic_store(&g_audio_pause, 0);
  g_pause_env = 1; g_dev_rate = 1; g_cfg.vol = VOL_REF;
  g_trig_flooded = g_trig_protected_flooded = g_audio_invalid = 0;
  g_trig_stale = g_trig_cancelled = g_protected_dropped = g_audio_bad_snapshot = 0;
  g_trig_dropped = g_trig_capped = 0;
}
static int audiocheck_live(int id, int tag, int dying) {
  int n = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    const voice_t *v = &g_voices[i];
    n += v->active && (id < 0 || v->id == id) && (tag < 0 || v->tag == tag) &&
         (dying < 0 || v->dying == dying);
  }
  return n;
}
static double audiocheck_render(int frames, int channels, int *fail) {
  float pcm[1028];
  for (int i = 0; i < 1028; i++) pcm[i] = 12345;
  if (frames < 0 || frames > 512 || (channels != 1 && channels != 2)) {
    *fail = 1; return 0;
  }
  audio_mix(pcm + 2, (uint32_t)frames, channels);
  *fail |= pcm[0] != 12345 || pcm[1] != 12345 ||
      pcm[2 + frames * channels] != 12345 || pcm[3 + frames * channels] != 12345;
  double energy = 0;
  for (int i = 0; i < frames * channels; i++) {
    float value = pcm[2 + i];
    *fail |= f_is_nonfinite(value) || fabsf(value) >= 1;
    energy += (double)value * value;
  }
  return energy;
}
static int audiocheck_trace(void) {
  int fail = 0;
  audiocheck_reset();
  fail |= !audio_trace_begin() || audio_trace_begin();
  audio_play(SFX_DRY, .01f, 1, 0, 0);
  float invalid; uint32_t bits = UINT32_C(0x7fc00001); memcpy(&invalid, &bits, sizeof invalid);
  audio_play(SFX_HIT, .5f, invalid, 0, 0);
  audio_play_tag(SFX_DRY, .7f, 1, 0, .01f, 7);
  double quiet = audiocheck_render(200, 2, &fail);
  quiet += audiocheck_render(280, 2, &fail);
  audiocheck_render(16, 2, &fail);
  int first = 0;
  while (first < 16 && g_sfx_buf[SFX_DRY][first * SFX_CH] == 0 &&
         g_sfx_buf[SFX_DRY][first * SFX_CH + 1] == 0) first++;
  fail |= quiet != 0 || first >= 16 || g_audio_trace_n != 3 || g_audio_trace_frames != 496 ||
      g_audio_trace[0].status != ATR_QUIET || g_audio_trace[1].status != ATR_INVALID ||
      g_audio_trace[2].first_source != 480 || g_audio_trace[2].first_nonzero != (uint64_t)(480 + first) ||
      g_audio_trace[2].source_delay != 480 || g_audio_trace[2].device_delay != 480;
  audio_session_reset();
  audio_play(SFX_HIT_HEAD, .7f, 1, 0, 0); audiocheck_render(16, 2, &fail);
  fail |= g_audio_trace[2].status != ATR_RESET || g_audio_trace[3].first_source != 496;
  fail |= !audio_mix_device_reset(SFX_HZ, SFX_CH);
  audio_play(SFX_HURT, .7f, 1, 0, 0); audiocheck_render(16, 2, &fail);
  fail |= g_audio_trace[3].status != ATR_RESET || g_audio_trace[4].first_source != 512 ||
      g_audio_trace_frames != 528;
  audio_play_tag(SFX_DRY, .7f, 1, 0, 1, 5); atomic_store(&g_audio_pause, 1);
  for (int i = 0; i < 8; i++) audiocheck_render(512, 2, &fail);
  fail |= g_audio_trace[5].first_source != AUDIO_TRACE_UNKNOWN;
  audio_kill(SFX_DRY, 5); audiocheck_render(1, 2, &fail);
  fail |= g_audio_trace[5].status != ATR_CANCELLED || g_audio_trace[5].first_source != AUDIO_TRACE_UNKNOWN;
  FILE *out = tmpfile();
  if (!out) fail = 1;
  else { fail |= !audio_trace_write(out); if (fclose(out)) fail = 1; }
  audio_trace_end();
  audiocheck_reset(); fail |= !audio_trace_begin();
  for (int i = 0; i < AUDIO_TRACE_MAX + 1; i++) audio_play(SFX_DRY, .01f, 1, 0, 0);
  fail |= g_audio_trace_n != AUDIO_TRACE_MAX || g_audio_trace_overflow != 1;
  out = tmpfile();
  if (!out) fail = 1;
  else { fail |= audio_trace_write(out); if (fclose(out)) fail = 1; }
  audio_trace_end();
  audiocheck_reset(); fail |= !audio_trace_begin();
  g_dev_rate = (float)SFX_HZ / 44100;
  audiocheck_render(16, 2, &fail);
  fail |= g_audio_trace_bad_rate != 1;
  out = tmpfile();
  if (!out) fail = 1;
  else { fail |= audio_trace_write(out); if (fclose(out)) fail = 1; }
  audio_trace_end();
  printf("audiocheck trace source_sample=480 nonzero_sample=%d segments=3 resets=2 overflow=1 rate_change=1 %s\n",
         480 + first, fail ? "FAIL" : "ok");
  return fail;
}

static int audiocheck_lifecycle(int (*routing)(void)) {
  if (g_audio_trace_on) { puts("audiocheck cannot run during an active take trace"); return 1; }
  // The harness has no device thread. Save both SPSC lanes and their publications
  // so this destructive stress fixture can be chained with other real proofs.
  audio_wet_t *wet = malloc(sizeof *wet);
  if (!wet) fatal("out of memory");
  memcpy(wet, &g_audio_wet, sizeof *wet);
  uint64_t acoustic_word = atomic_load(&g_acoustic_word);
  long acoustic_state[] = {g_acoustic_next, g_acoustic_updates, g_acoustic_queries, g_whizz_t, g_acoustic_whizz_rays, g_acoustic_whizz_solids};
  int acoustic_last = g_acoustic_last_queries; float whizz_gain = g_whizz_g;
  static voice_t voices[MAX_VOICES];
  static sfx_trig_t rings[2][TRIG_RING];
  static uint32_t cancels[SFX_COUNT][AUDIO_TAGS];
  memcpy(voices, g_voices, sizeof voices);
  memcpy(rings[0], g_trig, sizeof g_trig); memcpy(rings[1], g_protected_trig, sizeof g_protected_trig);
  audio_snapshot_t read = {atomic_load(&g_trig_r), atomic_load(&g_protected_r)};
  audio_snapshot_t write = audio_mix_snapshot();
  uint32_t epoch = atomic_load(&g_audio_epoch), mix_epoch = g_audio_mix_epoch;
  for (int id = 0; id < SFX_COUNT; id++)
    for (int tag = 0; tag < AUDIO_TAGS; tag++) cancels[id][tag] = atomic_load(&g_audio_cancel[id][tag]);
  float state[] = {g_dev_rate, g_cfg.vol, g_lim_g, g_lim_min, g_duck_env, g_duck_min, g_pause_env};
  int pause = atomic_load(&g_audio_pause), log_on = g_sfx_log_on, voice_peak = g_voice_peak;
  long device_counts[] = {g_audio_device_errors, g_audio_underruns, g_audio_stream_errors, g_audio_restarts};
  long counts[] = {g_trig_flooded, g_trig_protected_flooded, g_audio_invalid, g_trig_stale,
      g_trig_cancelled, g_protected_dropped, g_audio_bad_snapshot, g_trig_dropped, g_trig_capped};
  g_sfx_log_on = 0;
  int fail = 0, overall = 0, cases = 0;

  audiocheck_reset();
  audio_play_tag(SFX_RELOAD_AR, .8f, 1, 0, 0, 1);
  audio_play_tag(SFX_RELOAD_AR, .8f, 1, 0, 1, 1);
  double old_energy = 0;
  for (int i = 0; i < 10; i++) old_energy += audiocheck_render(512, 2, &fail);
  fail |= old_energy <= 1e-6 || audiocheck_live(SFX_RELOAD_AR, 1, 0) != 2;
  for (int i = 0; i < TRIG_RING; i++) audio_play_tag(SFX_RELOAD_AR, .8f, 1, 0, 1, 1);
  audio_play(SFX_STEP, .8f, 1, 0, 0); // ordinary pressure is measured separately
  audio_play(SFX_HIT, .8f, 1, 0, 0);
  audio_play(SFX_HURT, .8f, 1, 0, 0);
  audio_kill(SFX_RELOAD_AR, 1);
  double protected_energy = audiocheck_render(512, 2, &fail);
  fail |= g_trig_flooded != 1 || g_trig_protected_flooded || g_protected_dropped ||
          audiocheck_live(SFX_HIT, 0, 0) != 1 || audiocheck_live(SFX_HURT, 0, 0) != 1 ||
          audiocheck_live(SFX_RELOAD_AR, 1, 0) != 0 || protected_energy <= 1e-6;
  audio_play_tag(SFX_RELOAD_AR, .8f, 1, 0, 0, 1);
  audiocheck_render(512, 2, &fail);
  fail |= audiocheck_live(SFX_RELOAD_AR, 1, 0) != 1;
  for (int i = 0; i < 8; i++) audiocheck_render(512, 2, &fail);
  fail |= audiocheck_live(SFX_RELOAD_AR, 1, 1) != 0;
  printf("audiocheck admission old_energy=%.6f protected_energy=%.6f normal_flood=%ld "
         "protected_flood=%ld cancelled=%ld\n", old_energy, protected_energy,
         g_trig_flooded, g_trig_protected_flooded, g_trig_cancelled);
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  audiocheck_reset();
  audio_play_tag(SFX_RELOAD_SR, .8f, 1, 0, 1, 2);
  audio_mix_admit();
  for (int i = 0; i < TRIG_RING; i++) audio_play(SFX_HIT, .5f, 1, 0, .2f);
  audio_play(SFX_HURT, .8f, 1, 0, 0); // even protected storage remains bounded
  audio_kill(SFX_RELOAD_SR, 2);
  audiocheck_render(1, 2, &fail);
  fail |= g_trig_protected_flooded != 1 || audiocheck_live(SFX_RELOAD_SR, 2, -1) ||
          audiocheck_live(SFX_HIT, 0, 0) != TRIG_RING || g_protected_dropped;
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  for (int race = 0; race < 2; race++) {
    audiocheck_reset();
    audio_play(SFX_FIRE_AR, .5f, 1, 0, 1);
    audio_play(SFX_HURT, .5f, 1, 0, 1);
    audio_snapshot_t end = audio_mix_snapshot();
    audio_mix_sync_epoch();
    audio_session_reset();
    audio_play(SFX_DRY, .5f, 1, 0, 0);
    audio_play(SFX_HIT_HEAD, .5f, 1, 0, 0);
    if (!race) end = audio_mix_snapshot();
    audio_mix_admit_until(end);
    fail |= atomic_load(&g_trig_r) != end.normal || atomic_load(&g_protected_r) != end.protected;
    if (race) fail |= audiocheck_live(-1, -1, -1) != 0 ||
        atomic_load(&g_trig_w) != end.normal + 1 || atomic_load(&g_protected_w) != end.protected + 1;
    audiocheck_render(256, 2, &fail);
    fail |= audiocheck_live(-1, -1, -1) != 2 || audiocheck_live(SFX_DRY, 0, 0) != 1 ||
            audiocheck_live(SFX_HIT_HEAD, 0, 0) != 1;
    printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;
  }

  audiocheck_reset();
  uint32_t near_wrap = UINT32_MAX - 4;
  atomic_store(&g_trig_r, near_wrap); atomic_store(&g_trig_w, near_wrap);
  atomic_store(&g_protected_r, near_wrap); atomic_store(&g_protected_w, near_wrap);
  for (int i = 0; i < 8; i++) {
    audio_play(SFX_FIRE_AR, .4f, 1, 0, 0);
    audio_play(SFX_HIT, .4f, 1, 0, 0);
  }
  audio_mix_admit();
  fail |= atomic_load(&g_trig_r) != near_wrap + 8 || atomic_load(&g_trig_w) != near_wrap + 8 ||
      atomic_load(&g_protected_r) != near_wrap + 8 || atomic_load(&g_protected_w) != near_wrap + 8 ||
      audiocheck_live(-1, -1, -1) != 16 || g_audio_bad_snapshot;
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  audiocheck_reset();
  for (int i = 0; i < MAX_VOICES; i++) {
    audio_play(SFX_FIRE_AR, .7f, 1, 0, 0);
    if ((i + 1) % 32 == 0) audio_mix_admit();
  }
  fail |= audiocheck_live(SFX_FIRE_AR, 0, 0) != MAX_VOICES;
  audio_play(SFX_HURT, .05f, 1, 0, 0); audio_mix_admit();
  audio_play(SFX_HIT, .1f, 1, 0, 0); audio_mix_admit();
  fail |= audiocheck_live(SFX_HURT, 0, 0) != 1 || audiocheck_live(SFX_HIT, 0, 0) != 1 ||
      audiocheck_live(SFX_FIRE_AR, 0, 0) != MAX_VOICES - 2 || g_protected_dropped;
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  audiocheck_reset();
  for (int i = 0; i < MAX_VOICES; i++) {
    audio_play(SFX_HIT, .5f, 1, 0, 0);
    if ((i + 1) % 32 == 0) audio_mix_admit();
  }
  audio_play(SFX_FIRE_AR, 1, 1, 0, 0); audio_mix_admit();
  fail |= audiocheck_live(SFX_HIT, 0, 0) != MAX_VOICES || g_trig_dropped != 1 || g_protected_dropped;
  audio_play(SFX_HURT, 1, 1, 0, 0); audio_mix_admit();
  fail |= g_protected_dropped != 1 || g_trig_dropped != 2;
  audiocheck_render(512, 2, &fail);
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  audiocheck_reset();
  float invalid; uint32_t nan = UINT32_C(0x7fc00001); memcpy(&invalid, &nan, sizeof invalid);
  audio_snapshot_t before = audio_mix_snapshot();
  audio_play(-1, 1, 1, 0, 0); audio_play(SFX_COUNT, 1, 1, 0, 0);
  audio_play(SFX_HIT, invalid, 1, 0, 0); audio_play(SFX_HIT, 1, 0, 0, 0);
  audio_play(SFX_HIT, 1, 1, invalid, 0); audio_play(SFX_HIT, 1, 1, 0, 61);
  audio_play_tag(SFX_HIT, 1, 1, 0, 0, AUDIO_TAGS); audio_kill(SFX_HIT, -1);
  audio_snapshot_t after = audio_mix_snapshot();
  fail |= before.normal != after.normal || before.protected != after.protected || g_audio_invalid != 8;
  after.normal -= 1; audio_mix_admit_until(after);
  fail |= g_audio_bad_snapshot != 1 || atomic_load(&g_trig_r) != before.normal;
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;

  for (int rate_index = 0; rate_index < 2; rate_index++)
    for (int channels = 1; channels <= 2; channels++) {
      audiocheck_reset();
      int hz = rate_index ? 48000 : 44100;
      g_dev_rate = (float)SFX_HZ / hz;
      audio_play(SFX_IMPACT, .8f, 1, .6f, .012f);
      audio_mix_admit();
      int delay = (int)((float)(int)(.012f * SFX_HZ) / g_dev_rate);
      double silence = 0;
      for (int left = delay; left > 0;) {
        int block = left > 512 ? 512 : left;
        silence += audiocheck_render(block, channels, &fail); left -= block;
      }
      fail |= silence != 0 || g_voices[0].pos != 0 || g_voices[0].delay != 0;
      double energy = audiocheck_render(256, channels, &fail);
      fail |= energy <= 1e-6 || fabsf(g_voices[0].pos - 256 * g_dev_rate) > .01f;
      atomic_store(&g_audio_pause, 1);
      for (int block = 0; block < 8; block++) audiocheck_render(512, channels, &fail);
      float frozen = g_voices[0].pos;
      double paused = audiocheck_render(512, channels, &fail);
      fail |= paused != 0 || g_voices[0].pos != frozen;
      atomic_store(&g_audio_pause, 0);
      audiocheck_render(128, channels, &fail);
      fail |= g_voices[0].pos <= frozen;
      // Equal milliseconds use the same pause and cancellation envelopes at
      // both rates. A fixed per-device-frame fade fails the 44.1kHz witness.
      audiocheck_reset();
      fail |= !audio_mix_device_reset((unsigned)hz, channels);
      audio_play_tag(SFX_RELOAD_SR, .8f, 1, 0, 0, 3);
      audio_mix_admit();
      float initial = g_voices[0].gl + g_voices[0].gr;
      audio_kill(SFX_RELOAD_SR, 3); atomic_store(&g_audio_pause, 1);
      audiocheck_render(hz / 100, channels, &fail);
      float envelope = powf(.9945f, (float)SFX_HZ / 100);
      float faded = (g_voices[0].gl + g_voices[0].gr) / initial;
      fail |= fabsf(g_pause_env - envelope) > .0002f || fabsf(faded - envelope) > .0002f;
      // A device switch clears audible, scheduled and queued timelines through
      // the same consumer owner while a fresh post-reset cue remains audible.
      audio_play_tag(SFX_RELOAD_AR, .8f, 1, 0, 1, 1);
      audio_play(SFX_HURT, .8f, 1, 0, 1);
      fail |= !audio_mix_device_reset(hz == 48000 ? 44100u : 48000u, channels);
      fail |= audiocheck_live(-1, -1, -1) || g_duck_env != 0 || g_lim_g != 1 || g_pause_env != 1 ||
          atomic_load(&g_trig_r) != atomic_load(&g_trig_w) ||
          atomic_load(&g_protected_r) != atomic_load(&g_protected_w);
      atomic_store(&g_audio_pause, 0);
      audio_play(SFX_HIT_HEAD, .8f, 1, 0, 0);
      fail |= audiocheck_render(128, channels, &fail) <= 1e-6 || audiocheck_live(SFX_HIT_HEAD, 0, 0) != 1;
      long errors = g_audio_device_errors;
      fail |= audio_mix_device_reset(0, channels) || audio_mix_device_reset(48000, 0) ||
          audio_mix_device_reset(192001, channels) || audio_mix_device_reset(48000, 9) ||
          g_audio_device_errors != errors + 4 || audiocheck_live(SFX_HIT_HEAD, 0, 0) != 1;
      printf("audiocheck device hz=%d channels=%d delay_frames=%d energy=%.6f "
             "paused=%.6f resumed=1\n", hz, channels, delay, energy, paused);
      printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;
    }

  fail |= audiocheck_trace();
  printf("audiocheck lifecycle_case=%d %s\n", cases + 1, fail ? "FAIL" : "ok");
  overall |= fail; fail = 0; cases++;
  if (routing) overall |= routing();

  memcpy(&g_audio_wet, wet, sizeof *wet); free(wet);
  atomic_store(&g_acoustic_word, acoustic_word);
  g_acoustic_next = acoustic_state[0]; g_acoustic_updates = acoustic_state[1];
  g_acoustic_queries = acoustic_state[2]; g_whizz_t = acoustic_state[3];
  g_acoustic_whizz_rays = acoustic_state[4]; g_acoustic_whizz_solids = acoustic_state[5];
  g_acoustic_last_queries = acoustic_last; g_whizz_g = whizz_gain;
  memcpy(g_voices, voices, sizeof voices);
  memcpy(g_trig, rings[0], sizeof g_trig); memcpy(g_protected_trig, rings[1], sizeof g_protected_trig);
  atomic_store(&g_trig_r, read.normal); atomic_store(&g_trig_w, write.normal);
  atomic_store(&g_protected_r, read.protected); atomic_store(&g_protected_w, write.protected);
  atomic_store(&g_audio_epoch, epoch); g_audio_mix_epoch = mix_epoch;
  for (int id = 0; id < SFX_COUNT; id++)
    for (int tag = 0; tag < AUDIO_TAGS; tag++) atomic_store(&g_audio_cancel[id][tag], cancels[id][tag]);
  g_dev_rate = state[0]; g_cfg.vol = state[1]; g_lim_g = state[2]; g_lim_min = state[3];
  g_duck_env = state[4]; g_duck_min = state[5]; g_pause_env = state[6];
  atomic_store(&g_audio_pause, pause); g_sfx_log_on = log_on; g_voice_peak = voice_peak;
  g_audio_device_errors = device_counts[0]; g_audio_underruns = device_counts[1];
  g_audio_stream_errors = device_counts[2]; g_audio_restarts = device_counts[3];
  g_trig_flooded = counts[0]; g_trig_protected_flooded = counts[1]; g_audio_invalid = counts[2];
  g_trig_stale = counts[3]; g_trig_cancelled = counts[4]; g_protected_dropped = counts[5];
  g_audio_bad_snapshot = counts[6]; g_trig_dropped = counts[7]; g_trig_capped = counts[8];
  fail |= cases != 13 || overall;
  printf("audiocheck lifecycle cases=%d snapshot_races=2 rate_channels=4 %s\n", cases,
         fail ? "FAIL" : "ok");
  return fail;
}

// These fixtures drive actual geometry, routing, admission and callback owners.
// Scalar analytic witnesses do not reimplement the production descriptor or DSP.
static int acousticcheck_geometry(void) {
  int fail = 0;
  uint64_t rng = g_rng, bots = g_rng_bot;
  g_listen = (v3){{0,2,0}}; g_listen_r = (v3){{1,0,0}};
  acoustic_params_t contexts[4];
  for (int kind = 0; kind < 4; kind++) {
    g_num_solids = 0;
    if (kind >= 1) map_add((v3){{2,0,-20}}, (v3){{2.2f,5,20}}, (v3){{1,1,1}});
    if (kind >= 2) map_add((v3){{-2.2f,0,-20}}, (v3){{-2,5,20}}, (v3){{1,1,1}});
    if (kind >= 3) map_add((v3){{-20,3.5f,-20}}, (v3){{20,3.7f,20}}, (v3){{1,1,1}});
    audio_acoustic_invalidate(); g_tick = 100;
    long queries = g_acoustic_queries, updates = g_acoustic_updates;
    audio_acoustic_update();
    contexts[kind] = acoustic_unpack(atomic_load(&g_acoustic_word));
    fail |= contexts[kind].kind != kind || g_acoustic_last_queries != 9;
    for (int tick = 1; tick < 24; tick++) {
      g_tick = 100 + tick; audio_acoustic_update(); fail |= g_acoustic_last_queries != 0;
    }
    fail |= g_acoustic_queries != queries + 9 || g_acoustic_updates != updates + 1;
    g_tick = 124; audio_acoustic_update();
    fail |= g_acoustic_last_queries != 9 || g_acoustic_queries != queries + 18;
    printf("acousticcheck context=%d actual=%d early=(%.4f %.4f) delay_ms=(%.3f %.3f) late=%.4f queries=18 %s\n",
      kind, contexts[kind].kind, (double)contexts[kind].early[0], (double)contexts[kind].early[1],
      contexts[kind].delay[0] * 1000.0, contexts[kind].delay[1] * 1000.0,
      (double)contexts[kind].late, fail ? "FAIL" : "ok");
  }
  fail |= contexts[0].early[0] != 0 || contexts[0].early[1] != 0 || contexts[0].late != 0;
  fail |= contexts[1].early[1] <= contexts[1].early[0] * 2 || contexts[1].late <= .02f;
  fail |= fabsf(contexts[2].early[1] - contexts[2].early[0]) > .002f;
  fail |= contexts[2].late <= contexts[1].late || contexts[3].late <= contexts[2].late;
  fail |= contexts[3].damping >= contexts[1].damping;
  g_listen_r.x = -1; audio_acoustic_invalidate(); audio_acoustic_update();
  fail |= g_rng != rng || g_rng_bot != bots;
  for (int i = 0; i < 256; i++) {
    acoustic_params_t p = {.delay = {-.1f, 2}, .early = {2,-1}, .late = 4,
      .feedback = 2, .damping = 50000, .kind = i};
    acoustic_params_t got = acoustic_unpack(acoustic_pack(p));
    fail |= got.delay[0] < .001999f || got.delay[1] > .070001f || got.early[0] > .300001f ||
      got.early[1] != 0 || got.late > .220001f || got.feedback > .650001f || got.damping > 9000;
    uint64_t invalid = (uint64_t)(unsigned)i << 56;
    got = acoustic_unpack(invalid);
    if (i < 128 || i > 131) fail |= got.early[0] != 0 || got.early[1] != 0 || got.late != 0;
  }
  return fail;
}

static int acousticcheck_routes(void) {
  int fail = 0;
  float pulse[1024 * SFX_CH];
  for (int i = 0; i < 1024; i++) pulse[i*2] = pulse[i*2+1] = .1f;
  float *old_buf = g_sfx_buf[SFX_WHIZZ]; int old_len = g_sfx_len[SFX_WHIZZ];
  g_sfx_buf[SFX_WHIZZ] = pulse; g_sfx_len[SFX_WHIZZ] = 1024;
  g_local_ent = 0; g_listen = (v3){{0,2,0}}; g_listen_r = (v3){{1,0,0}};
  for (int test = 0; test < 15; test++) {
    audiocheck_reset(); g_sfx_log_on = 1; g_sfx_log_n = 0; g_num_solids = 0;
    g_tick = 100; g_rng_audio = 0xa0d10; g_listen = (v3){{0,2,0}};
    event_t e = {.type = EV_FIRE, .a = {{2,2,10}}, .b = {{0,0,1}}, .f = 20,
      .wpn = WPN_AR, .src = 1, .dst = -1};
    int expected = 1; float pan = .9f, along = 10, miss = 2;
    if (test == 1) { e.a.x = -2; pan = -.9f; }
    if (test == 2) { e.a.x = 0; e.a.y = 4; pan = 0; }
    if (test == 3) { e.a.x = 0; pan = 0; miss = 0; }
    if (test == 4) { e.a.z = -1; expected = 0; }
    if (test == 5) { e.a.z = .4f; expected = 0; }
    if (test == 6) { e.a.x = 4.3f; expected = 0; }
    if (test == 7) { e.dst = 0; expected = 0; }
    if (test == 8) { e.src = 0; expected = 0; }
    if (test == 9) {
      map_add((v3){{1,0,-1}}, (v3){{1.2f,5,1}}, (v3){{1,1,1}}); expected = 0;
    }
    if (test == 10) {
      map_add((v3){{1.5f,0,-1}}, (v3){{2.5f,5,1}}, (v3){{1,1,1}}); e.pen = 1; expected = 0;
    }
    if (test == 11) {
      map_add((v3){{-3,0,-5.2f}}, (v3){{3,5,-5}}, (v3){{1,1,1}}); e.pen = 1;
    }
    if (test == 12) { e.a.z = -5; e.f = 5; expected = 0; }
    if (test == 13) { e.dst = 2; e.k = 2; }
    if (test == 14) {
      // trace_world intentionally ignores inside-origin boxes: containment is
      // independently required when both listener and closest point lie inside.
      map_add((v3){{1.5f,0,-1}}, (v3){{2.5f,5,1}}, (v3){{1,1,1}});
      g_listen.x = 1.8f; e.pen = 1; expected = 0;
    }
    audio_shot_world(&e);
    int bad = g_sfx_log_n != expected;
    double energy[2] = {0}; float pcm[256 * 2];
    for (int chunk = 0; chunk < 24; chunk++) {
      audio_mix(pcm, 256, 2);
      for (int i = 0; i < 512; i++) { bad |= f_is_nonfinite(pcm[i]); energy[i&1] += (double)pcm[i]*pcm[i]; }
    }
    if (expected) {
      sfx_log_t t = g_sfx_log[0];
      bad |= t.id != SFX_WHIZZ || fabsf(t.pan - pan) > 1e-5f;
      float delay = along / WPN_DEF[WPN_AR].v_round + miss / 343.0f;
      bad |= fabsf(t.delay - delay) > 1e-6f || energy[0] + energy[1] <= .001;
      if (pan > 0) bad |= energy[1] < 15 * energy[0];
      if (pan < 0) bad |= energy[0] < 15 * energy[1];
      if (pan == 0) bad |= fabs(energy[0] - energy[1]) > 1e-6;
    } else bad |= energy[0] != 0 || energy[1] != 0;
    printf("acousticcheck segment=%d offered=%d expected=%d energy=(%.7f %.7f) %s\n",
      test, g_sfx_log_n, expected, energy[0], energy[1], bad ? "FAIL" : "ok");
    fail |= bad;
  }
  audiocheck_reset(); g_sfx_log_on = 1; g_sfx_log_n = 0; g_num_solids = 0;
  g_listen = (v3){{0,2,0}};
  event_t e = {.type = EV_FIRE, .a = {{1,2,10}}, .b = {{0,0,1}}, .f = 20,
    .wpn = WPN_AR, .src = 1, .dst = -1};
  audio_shot_world(&e); g_tick++; e.a.x = 2; audio_shot_world(&e);
  fail |= g_sfx_log_n != 1;
  e.a.x = .5f; audio_shot_world(&e); fail |= g_sfx_log_n != 2;
  g_tick += WHIZZ_HOLD; e.a.x = 2; audio_shot_world(&e); fail |= g_sfx_log_n != 3;
  audio_session_reset(); audio_shot_world(&e); fail |= g_sfx_log_n != 4;
  printf("acousticcheck cooldown initial=1 farther=0 closer=1 expired=1 new_epoch=1 %s\n", fail ? "FAIL" : "ok");
  g_sfx_buf[SFX_WHIZZ] = old_buf; g_sfx_len[SFX_WHIZZ] = old_len;

  float rates[3];
  for (int material = 0; material < 3; material++) {
    audiocheck_reset(); g_sfx_log_on = 1; g_sfx_log_n = 0; g_num_solids = 0;
    map_add((v3){{1,0,-1}}, (v3){{2,4,1}}, (v3){{1,1,1}});
    g_solids[0].mat = material == 1 ? SOLID_WALL : SOLID_CONCRETE;
    event_t impact = {.type = EV_IMPACT, .a = {{1,2,0}}, .i = material == 0 ? -1 : 0, .src = 1};
    event_t shot = {.type = EV_FIRE, .a = {{1,2,0}}, .b = {{1,0,0}}, .f = 1,
      .src = 0, .dst = -1, .wpn = WPN_AR};
    audio_event(&shot);
    fail |= g_sfx_log_n != 1 || g_sfx_log[0].id != SFX_FIRE_AR;
    audio_event(&impact); fail |= g_sfx_log_n != 2 || g_sfx_log[1].id != SFX_IMPACT;
    rates[material] = g_sfx_log[1].rate;
    impact.k = 1; audio_on_event(&impact); audio_on_event(&impact);
    fail |= g_sfx_log_n != 2;
  }
  fail |= !(rates[0] < rates[1] && rates[1] < rates[2]);
  printf("acousticcheck impacts floor=%.5f wall=%.5f concrete=%.5f penetration_entry_exit=0 %s\n",
    (double)rates[0], (double)rates[1], (double)rates[2], fail ? "FAIL" : "ok");
  return fail;
}

static int acousticcheck_wet(void) {
  int fail = 0;
  const int rates[] = {8000, 44100, 48000, 192000};
  acoustic_params_t full = {.delay = {.010f,.024f}, .early = {.30f,.30f},
    .late = .22f, .feedback = .65f, .damping = 3500, .kind = ACOUSTIC_SHELTER};
  for (unsigned rate = 0; rate < sizeof rates / sizeof rates[0]; rate++) {
    int hz = rates[rate], bad = 0;
    audiocheck_reset(); audio_mix_device_reset((unsigned)hz, 2);
    atomic_store(&g_acoustic_word, acoustic_pack(full)); audio_wet_prepare();
    float left, right;
    for (int i = 0; i < hz; i++) audio_wet_sample(0, 0, &left, &right);
    int first = -1; double early_energy[2] = {0}, last = 0, peak = 0;
    int expected = (hz + 50) / 100;  // authored10ms maps exactly to packed code30
    for (int i = 0; i < hz * 4; i++) {
      audio_wet_sample(i == 0 ? 1 : 0, 0, &left, &right);
      bad |= f_is_nonfinite(left) || f_is_nonfinite(right);
      double e = (double)left*left + (double)right*right;
      if (e > 0 && first < 0) first = i;
      if (fabsf(left) > peak) peak = fabsf(left);
      if (fabsf(right) > peak) peak = fabsf(right);
      if (i < hz / 40) { early_energy[0] += (double)left*left; early_energy[1] += (double)right*right; }
      if (i >= hz * 3) last += e;
    }
    bad |= first != expected || peak > 1 || early_energy[0] <= early_energy[1] * 10;
    bad |= last / (hz * 2) > 1e-10;
    // Unit-bounded continuous input and rapid descriptors cannot grow a tail.
    for (int i = 0; i < hz * 3; i++) {
      if (i % (hz / 100) == 0) {
        acoustic_params_t target = full;
        if ((i / (hz / 100)) & 1) target = (acoustic_params_t){.delay = {.070f,.002f}, .damping = 9000};
        atomic_store(&g_acoustic_word, acoustic_pack(target)); audio_wet_prepare();
      }
      audio_wet_sample(sinf(i * .017f), cosf(i * .021f), &left, &right);
      bad |= f_is_nonfinite(left) || f_is_nonfinite(right) || fabsf(left) > 1 || fabsf(right) > 1;
    }
    audio_mix_device_reset(rate & 1 ? 48000u : 44100u, 2);
    audio_wet_prepare(); audio_wet_sample(0,0,&left,&right);
    bad |= left != 0 || right != 0;
    printf("acousticcheck wet hz=%d first=%d expected=%d peak=%.7f tail_rms=%.9f rapid=300 reset=0 %s\n",
      hz, first, expected, peak, sqrt(last/(hz*2)), bad ? "FAIL" : "ok");
    fail |= bad;
  }
  // Re-entering dry geometry crossfades the running room rather than clearing it.
  audiocheck_reset(); atomic_store(&g_acoustic_word, acoustic_pack(full)); audio_wet_prepare();
  float steady_l = 0, steady_r = 0, transition_l, transition_r;
  for (int i = 0; i < SFX_HZ * 2; i++) audio_wet_sample(1,1,&steady_l,&steady_r);
  audio_acoustic_invalidate(); audio_wet_prepare();
  audio_wet_sample(1,1,&transition_l,&transition_r);
  fail |= steady_l <= .1f || fabsf(transition_l-steady_l) > .02f || fabsf(transition_r-steady_r) > .02f;
  double end_power = 0;
  for (int i = 0; i < SFX_HZ * 4; i++) {
    audio_wet_sample(0,0,&transition_l,&transition_r);
    fail |= f_is_nonfinite(transition_l) || f_is_nonfinite(transition_r);
    if (i >= SFX_HZ * 3) end_power += (double)transition_l*transition_l+(double)transition_r*transition_r;
  }
  fail |= end_power / (SFX_HZ * 2) > 1e-10;
  printf("acousticcheck boundary steady=%.6f dry_tail_rms=%.9f %s\n",
    (double)steady_l,sqrt(end_power/(SFX_HZ*2)),fail ? "FAIL" : "ok");
  // A100Hz carrier bounds sample slope independently of the tap implementation.
  // A five-millisecond tap jump would invert it; a50ms fade stays below0.006.
  audiocheck_reset(); acoustic_params_t tap = full;
  tap.delay[0] = tap.delay[1] = .010f; tap.late = tap.feedback = 0;
  atomic_store(&g_acoustic_word,acoustic_pack(tap)); audio_wet_prepare();
  float previous = 0, slope = 0;
  for (int i = 0; i < SFX_HZ + SFX_HZ / 10; i++) {
    if (i == SFX_HZ) {
      tap.delay[0] = tap.delay[1] = .015f;
      atomic_store(&g_acoustic_word,acoustic_pack(tap)); audio_wet_prepare();
    }
    float x = sinf(2 * F_PI * 100 * (float)i / SFX_HZ + 1);
    audio_wet_sample(x,x,&transition_l,&transition_r);
    if (i >= SFX_HZ && fabsf(transition_l-previous) > slope) slope = fabsf(transition_l-previous);
    previous = transition_l;
  }
  fail |= slope > .006f;
  printf("acousticcheck taps carrier_hz=100 transition_ms=50 max_slope=%.7f limit=0.0060000 %s\n",
    (double)slope,fail ? "FAIL" : "ok");
  // Both the epoch and a same-format recovery must remove a real active room.
  for (int reset = 0; reset < 2; reset++) {
    audiocheck_reset(); atomic_store(&g_acoustic_word, acoustic_pack(full));
    float pcm[512 * 2];
    audio_play(SFX_FIRE_AR,.7f,1,0,0);
    for (int block = 0; block < 16; block++) audio_mix(pcm,512,2);
    if (reset) audio_mix_device_reset(SFX_HZ,2); else audio_session_reset();
    audio_mix(pcm,512,2);
    for (int i = 0; i < 1024; i++) fail |= pcm[i] != 0;
    audio_play(SFX_HIT_HEAD,.5f,1,0,0); audio_mix(pcm,512,2);
    double cue = 0; for (int i = 0; i < 1024; i++) cue += (double)pcm[i]*pcm[i];
    fail |= cue <= 1e-6 || audiocheck_live(SFX_HIT_HEAD,0,0) != 1;
  }
  float lead_dry[8192], lead_room[8192];
  audiocheck_reset(); audio_play(SFX_HIT_HEAD,.5f,1,0,0); audio_mix(lead_dry,4096,2);
  audiocheck_reset(); atomic_store(&g_acoustic_word,acoustic_pack(full));
  for (int i = 0; i < 12; i++) audio_mix(lead_room,4096,2);
  audio_play(SFX_HIT_HEAD,.5f,1,0,0); audio_mix(lead_room,4096,2);
  fail |= memcmp(lead_dry,lead_room,sizeof lead_dry) != 0;
  // LEAD is exactly dry even with a fully warmed live room; KEEP feeds only10%.
  audiocheck_reset(); atomic_store(&g_acoustic_word, acoustic_pack(full));
  float output[512 * 2];
  for (int block = 0; block < 100; block++) audio_mix(output,512,2);
  audio_play(SFX_HIT_HEAD, .5f, 1, 0, 0);
  for (int block = 0; block < 100; block++) audio_mix(output,512,2);
  for (unsigned i = 0; i < AUDIO_WET_RING; i++)
    fail |= g_audio_wet.early[0][i] != 0 || g_audio_wet.early[1][i] != 0;
  audio_play(SFX_FIRE_AR, .7f, 1, 0, 0);
  for (int block = 0; block < 12; block++) audio_mix(output,512,2);
  atomic_store(&g_audio_pause,1);
  for (int block = 0; block < 12; block++) audio_mix(output,512,2);
  fail |= !g_audio_wet.paused_clear;
  for (int i = 0; i < 1024; i++) fail |= output[i] != 0;
  atomic_store(&g_audio_pause,0); audio_session_reset();
  audio_play(SFX_HIT_HEAD,.5f,1,0,0); audio_mix(output,512,2);
  double energy = 0; for (int i = 0; i < 1024; i++) energy += (double)output[i]*output[i];
  fail |= energy <= 1e-6 || audiocheck_live(SFX_HIT_HEAD,0,0) != 1;
  printf("acousticcheck ownership lead_dry=1 pause_clear=1 new_epoch_cue=1 bytes=%zu %s\n",
    sizeof g_audio_wet, fail ? "FAIL" : "ok");
  return fail;
}


// Diagnostic captures share one energy/voice completion contract at 48 kHz.
// The quiet interval exceeds the longest early tap; delayed voices keep it open.
#define AUDIO_TAIL_SECONDS 10
#define AUDIO_TAIL_QUIET_FRAMES (SFX_HZ / 10)
#define AUDIO_TAIL_QUIET_POWER 1e-10
typedef struct { unsigned quiet_frames; double power; } audio_tail_t;
static int audio_tail_step(audio_tail_t *tail, const float *pcm, int frames, int channels) {
  if (frames <= 0 || channels < 1 || channels > 8) return -1;
  double energy = 0;
  for (int i = 0; i < frames * channels; i++) {
    if (f_is_nonfinite(pcm[i])) return -1;
    energy += (double)pcm[i] * pcm[i];
  }
  tail->power = energy / (frames * channels);
  tail->quiet_frames = tail->power < AUDIO_TAIL_QUIET_POWER && !audiocheck_live(-1,-1,-1)
    ? tail->quiet_frames + (unsigned)frames : 0;
  return tail->quiet_frames >= AUDIO_TAIL_QUIET_FRAMES;
}

static int acousticcheck_tail(void) {
  int fail = 0; float pcm[400 * 2] = {0}; audio_tail_t tail = {0};
  audiocheck_reset();
  fail |= !wav_frames_valid(0) || !wav_frames_valid(WAV_MAX_FRAMES) ||
    wav_frames_valid(-1) || wav_frames_valid(WAV_MAX_FRAMES + 1) || wav_frames_valid(INT_MAX);
  for (int i = 0; i < 12; i++) fail |= audio_tail_step(&tail,pcm,400,2) != (i == 11);
  pcm[0] = .1f; fail |= audio_tail_step(&tail,pcm,400,2) != 0 || tail.quiet_frames != 0;
  pcm[0] = 0; audio_play(SFX_HIT,.5f,1,0,5); audio_mix(pcm,400,2);
  for (int i = 0; i < 24; i++) fail |= audio_tail_step(&tail,pcm,400,2) != 0;
  uint32_t bits = UINT32_C(0x7fc00001); memcpy(&pcm[0],&bits,sizeof bits);
  fail |= audio_tail_step(&tail,pcm,400,2) != -1;
  audiocheck_reset();
  printf("acousticcheck drain quiet_frames=4800 rms_limit=0.000010000 delayed_voice=held nonfinite=rejected %s\n",
    fail ? "FAIL" : "ok");
  return fail;
}

static int acousticcheck_all(void) {
  solid_t solids[MAX_SOLIDS]; memcpy(solids,g_solids,sizeof solids);
  int count = g_num_solids, local = g_local_ent, log_on = g_sfx_log_on, log_n = g_sfx_log_n;
  long tick = g_tick, log_drops = g_sfx_log_drops;
  int last_wpn = g_last_wpn;
  v3 listener = g_listen, right = g_listen_r;
  uint64_t rng = g_rng, bots = g_rng_bot, audio_rng = g_rng_audio;
  sfx_log_t logs[SFX_LOG_N]; memcpy(logs,g_sfx_log,sizeof logs);
  int fail = acousticcheck_geometry();
  fail |= acousticcheck_routes(); fail |= acousticcheck_wet(); fail |= acousticcheck_tail();
  fail |= g_rng != rng || g_rng_bot != bots;
  memcpy(g_solids,solids,sizeof solids); g_num_solids = count;
  g_tick = tick; g_local_ent = local; g_listen = listener; g_listen_r = right; g_rng_audio = audio_rng;
  g_sfx_log_on = log_on; g_sfx_log_n = log_n; g_sfx_log_drops = log_drops;
  g_last_wpn = last_wpn;
  memcpy(g_sfx_log,logs,sizeof logs);
  printf("acousticcheck summary geometry=4 segments=15 materials=3 rates=4 sim_rng=unchanged %s\n", fail ? "FAIL" : "ok");
  return fail;
}

// Actual mixer blocks must cross each authored reload offset at the expected
// device sample, including rate changes, pending delays and interrupted takes.
static int filmcue_samples(void) {
  int overall = 0, cases = 0;
  static const int ids[] = {SFX_RELOAD_AR, SFX_RELOAD_AR_TAC, SFX_RELOAD_SR};
  for (int family = 0; family < 3; family++)
    for (int rate2 = 1; rate2 <= 3; rate2++) for (int delayed = 0; delayed < 2; delayed++) {
      int fail = 0;
      audiocheck_reset(); fail |= !audio_trace_begin();
      float rate = (float)rate2 * 0.5f;
      audio_play_tag(ids[family], .8f, rate, 0, delayed ? .017f : 0, 1);
      int frames = (int)(4.5f * SFX_HZ);
      for (int at = 0; at < frames;) {
        int block = at % 2 ? 73 : 512;
        if (block > frames - at) block = frames - at;
        audiocheck_render(block, 2, &fail); at += block;
      }
      const audio_trace_t *trace = &g_audio_trace[0];
      int expected_marks = family == 1 ? 1 : 2;
      fail |= g_audio_trace_n != 1 || trace->first_source != (uint64_t)trace->device_delay;
      for (int mark = 0; mark < 2; mark++) {
        if (mark >= expected_marks) {
          fail |= trace->milestone_source[mark] != -1 || trace->milestone_frame[mark] != AUDIO_TRACE_UNKNOWN;
          continue;
        }
        int source = trace->milestone_source[mark];
        int weapon = family == 2 ? WPN_SR : WPN_AR;
        double fraction = family == 1 ? RELOAD_SEAT_TAC : RELOAD_BEAT[weapon][mark];
        double authored = fraction * wpn_reload_ticks(weapon, family == 1) / TICK_HZ;
        fail |= fabs((double)source / SFX_HZ - authored) > 1.0 / SFX_HZ;
        uint64_t expected = (uint64_t)trace->device_delay + (uint64_t)((source * 2 + rate2 - 1) / rate2);
        fail |= source <= 0 || trace->milestone_frame[mark] != expected;
      }
      FILE *out = tmpfile();
      if (!out) fail = 1;
      else { fail |= !audio_trace_write(out); if (fclose(out)) fail = 1; }
      audio_trace_end();
      printf("filmcueproof family=%s rate=%.1f delayed=%d marks=%d %s\n",
        SFX_NAME[ids[family]], (double)rate, delayed, expected_marks, fail ? "FAIL" : "ok");
      overall |= fail; cases++;
    }
  for (int interrupted = 0; interrupted < 2; interrupted++) {
    int fail = 0;
    audiocheck_reset(); fail |= !audio_trace_begin();
    audio_play_tag(SFX_RELOAD_SR, .8f, 1, 0, .050f, 1);
    audiocheck_render(512, 2, &fail);
    if (interrupted) audio_session_reset(); else audio_kill(SFX_RELOAD_SR, 1);
    audiocheck_render(512, 2, &fail);
    const audio_trace_t *trace = &g_audio_trace[0];
    fail |= trace->milestone_source[0] < 0 || trace->milestone_source[1] < 0 ||
        trace->milestone_frame[0] != AUDIO_TRACE_UNKNOWN || trace->milestone_frame[1] != AUDIO_TRACE_UNKNOWN;
    audio_trace_end(); overall |= fail; cases++;
    printf("filmcueproof interrupted=%s unknown_marks=2 %s\n",
      interrupted ? "reset" : "cancel", fail ? "FAIL" : "ok");
  }
  for (int interrupted = 0; interrupted < 2; interrupted++) {
    int fail = 0;
    audiocheck_reset(); fail |= !audio_trace_begin();
    audio_play_tag(SFX_RELOAD_SR, .8f, 1, 0, 0, 1);
    int seat = g_audio_trace[0].milestone_source[0];
    for (int at = 0; at <= seat + 512; at += 512) audiocheck_render(512, 2, &fail);
    uint64_t crossed = g_audio_trace[0].milestone_frame[0];
    if (interrupted) audio_session_reset(); else audio_kill(SFX_RELOAD_SR, 1);
    for (int block = 0; block < 16; block++) audiocheck_render(512, 2, &fail);
    fail |= crossed != (uint64_t)seat || g_audio_trace[0].milestone_frame[0] != crossed ||
        g_audio_trace[0].milestone_frame[1] != AUDIO_TRACE_UNKNOWN;
    audio_trace_end(); overall |= fail; cases++;
    printf("filmcueproof after_seat=%s preserved_seat=1 unknown_bolt=1 %s\n",
      interrupted ? "reset" : "cancel", fail ? "FAIL" : "ok");
  }
  audiocheck_reset();
  printf("filmcueproof summary cases=%d %s\n", cases, overall ? "FAIL" : "ok");
  return overall;
}
