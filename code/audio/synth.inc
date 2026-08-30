// =============================================================================
// audio/synth.inc  —  procedural synthesis, the Confirmation Matrix (hit/
// kill x body/head), sfx_build_all.
// Proof: sfx DIR. One TU: included once by game.c, in definition order —
// moving a definition out is a semantic change.
// Contract: everything is generated at startup from a seeded stream;
// sfx_alloc reseeds g_rng_sfx from the SOUND ID alone, so sounds are
// byte-identical across map seeds.
// =============================================================================
// ---------------------------------------------------------------------------
// Procedural audio — every sound is synthesized at startup into a 48 kHz
// interleaved STEREO float buffer (layered noise/sine partials through one-pole filters
// and exponential envelopes; seeded, so the WAV dump is reproducible).
// ---------------------------------------------------------------------------

[[maybe_unused]] static const char *SFX_NAME[SFX_COUNT] = {  // harness WAV dump
  "fire_ar", "fire_sr", "far_ar", "far_sr", "hit", "hit_head",
  "kill", "kill_head", "hurt", "impact", "whizz", "step", "dryfire",
  "reload_ar", "reload_sr", "flesh", "reload_ar_tac",
};

#define SFX_HZ 48000
#define SFX_CH 2            // every buffer is interleaved stereo

// Buffers are STEREO now.
static float *g_sfx_buf[SFX_COUNT];   // interleaved L,R
static int    g_sfx_len[SFX_COUNT];   // FRAMES, not samples
static uint64_t g_rng_sfx;

#define SFX_SCRATCH (SFX_HZ * 2)
static float g_scr[SFX_SCRATCH];      // one mono layer at a time

static float *sfx_alloc(int id, float seconds) {
  // One noise stream PER SOUND. g_rng_sfx used to be a single stream consumed in build
  // order, which means changing the NUMBER or the LENGTH of the noise layers in any one
  // sound re-rolls the noise in every sound built after it.
  g_rng_sfx = 0x5F31Aull + 0x9E3779B97F4A7C15ull * (uint64_t)(id + 1);
  int n = (int)(seconds * SFX_HZ);
  g_sfx_len[id] = n;
  g_sfx_buf[id] = calloc((size_t)n * SFX_CH, sizeof(float));
  if (!g_sfx_buf[id]) fatal("out of memory");
  return g_sfx_buf[id];
}

// --- filters -----------------------------------------------------------
// One-pole, for gentle shaping. `hz` is the -3 dB corner; the coefficient is
// computed per sample so a layer can sweep its own filter.
typedef struct { float z; } op_t;
static float op_lp(op_t *s, float x, float hz) {
  float k = 1.0f - expf(-F_TAU * hz / SFX_HZ);
  s->z += (x - s->z) * k;
  return s->z;
}
static float op_hp(op_t *s, float x, float hz) { return x - op_lp(s, x, hz); }

// State-variable filter.
typedef struct { float lp, bp; } svf_t;
static float svf(svf_t *s, float x, float hz, float q) {
  // The Chamberlin topology is only stable while f + 1/q < 2, which puts a hard ceiling
  // near SR/6 — not the Nyquist limit one would assume.
  float f = 2.0f * sinf(F_PI * f_clamp(hz, 20.0f, SFX_HZ / 6.5f) / SFX_HZ);
  float d = 1.0f / f_clamp(q, 0.5f, 20.0f);
  if (f + d > 1.9f) d = 1.9f - f;
  float hp = x - s->lp - s->bp * d;
  s->bp += f * hp;
  s->lp += f * s->bp;
  return s->bp;   // BANDPASS tap only — lp is state the hp line above needs
}

// --- envelopes ---------------------------------------------------------
// Attack is LINEAR and measured in seconds; decay is exponential with a shape
// exponent (>1 = faster at first, which is what a real impact does).
static float sfx_env(float t, float atk, float dec, float shape) {
  if (t < 0) return 0;
  float a = atk <= 0.0f ? 1.0f : (t < atk ? t / atk : 1.0f);
  float d = expf(-t / dec);
  return a * (shape == 1.0f ? d : powf(d, shape));
}

// --- layer placement ---------------------------------------------------
// Add `n` frames of the mono scratch into a stereo buffer at frame `at`. `pan` is
// -1..1; `width` delays one channel by a few samples, which is how a near-coincident
// pair hears a source that is not a point.
static void sfx_lay(int id, int n, int at, float pan, int width) {
  float *b = g_sfx_buf[id];
  int len = g_sfx_len[id];
  float gl = sqrtf(0.5f * (1.0f - pan)), gr = sqrtf(0.5f * (1.0f + pan));
  for (int i = 0; i < n; i++) {
    int fl = at + i, fr = at + i + width;
    if (fl >= 0 && fl < len) b[fl * SFX_CH + 0] += g_scr[i] * gl * 1.4142f;
    if (fr >= 0 && fr < len) b[fr * SFX_CH + 1] += g_scr[i] * gr * 1.4142f;
  }
}

// Normalise the scratch to a peak of `amp`.
static int scr_norm(int n, float amp) {
  float mx = 1e-9f;
  for (int i = 0; i < n; i++) { float a = g_scr[i] < 0 ? -g_scr[i] : g_scr[i];
                                if (a > mx) mx = a; }
  float g = amp / mx;
  for (int i = 0; i < n; i++) g_scr[i] *= g;
  return n;
}

// --- building blocks ---------------------------------------------------
// A band-limited noise burst into the scratch. This is the workhorse: a
// gunshot is four of these at different centres, widths and decays.
static int sfx_burst(float sec, float hz, float q, float atk, float dec,
                     float shape, float amp) {
  int n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  svf_t f1 = {0, 0}, f2 = {0, 0};
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float x = rng_rangef(&g_rng_sfx, -1, 1);
    x = svf(&f1, x, hz, q);
    x = svf(&f2, x, hz, q * 0.7f);       // two poles: a real band, not a tilt
    g_scr[i] = x * sfx_env(t, atk, dec, shape);
  }
  return scr_norm(n, amp);
}

// A pitched body: a sine that sweeps down, which is what a pressure wave
// expanding away from a muzzle actually does.
static int sfx_sweep(float sec, float f0, float f1_, float ftau,
                     float dec, float shape, float amp) {
  int n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  float ph = 0;
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float f = f1_ + (f0 - f1_) * expf(-t / ftau);
    ph += F_TAU * f / SFX_HZ;
    g_scr[i] = sinf(ph) * sfx_env(t, 0.0002f, dec, shape);
  }
  return scr_norm(n, amp);
}

// The transient.
static int sfx_click(float sec, float dec, float hp_hz, float amp) {
  int n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  op_t h = {0};
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float x = op_hp(&h, rng_rangef(&g_rng_sfx, -1, 1), hp_hz);
    g_scr[i] = x * expf(-t / dec);
  }
  return scr_norm(n, amp);
}

// Two decaying partials with a little inharmonicity — a struck metal object.
static int sfx_ring(float sec, float f1_, float f2_, float dec, float amp) {
  int n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    g_scr[i] = sinf(F_TAU * f1_ * t) * expf(-t / dec)
             + sinf(F_TAU * f2_ * t) * expf(-t / (dec * 0.62f)) * 0.55f;
  }
  return scr_norm(n, amp);
}

// Room tail: band-limited noise that RISES for a few ms before it decays, because a
// room takes time to fill.
static void sfx_tail(int id, int at, float sec, float hz, float q,
                     float rise, float dec, float amp) {
  float *b = g_sfx_buf[id];
  int len = g_sfx_len[id], n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  for (int c = 0; c < SFX_CH; c++) {
    svf_t f1 = {0, 0}, f2 = {0, 0};
    for (int i = 0; i < n; i++) {
      float t = (float)i / SFX_HZ;
      float x = rng_rangef(&g_rng_sfx, -1, 1);
      x = svf(&f1, x, hz, q);
      x = svf(&f2, x, hz * 1.35f, q * 0.8f);
      g_scr[i] = x * (1.0f - expf(-t / rise)) * expf(-t / dec);
    }
    // Normalised like every other generator, so `amp` is a level here too.
    scr_norm(n, amp);
    for (int i = 0; i < n; i++) {
      int fr = at + i;
      if (fr >= 0 && fr < len) b[fr * SFX_CH + c] += g_scr[i];
    }
  }
}

// Early reflections: a handful of delayed, filtered, alternately-panned copies of what
// has been written so far.
static void sfx_reflect(int id, int upto, const float *dly, const float *gn,
                        int nref, float lp_hz) {
  float *b = g_sfx_buf[id];
  int len = g_sfx_len[id];
  // The source region is SNAPSHOT first, and that is a fix, not a tidy-up: writing
  // `b[i+d]` while reading `b[i]` over the same range fed the generator on its own
  // output once `upto` exceeded the shortest delay, so the tap at d reappeared at 2d,
  // 3d, 4d... — an IIR comb, not a handful of delayed copies.
  int ns = upto;
  if (ns > SFX_SCRATCH / SFX_CH) ns = SFX_SCRATCH / SFX_CH;
  for (int i = 0; i < ns * SFX_CH; i++) g_scr[i] = b[i];
  for (int r = 0; r < nref; r++) {
    int d = (int)(dly[r] * SFX_HZ);
    for (int c = 0; c < SFX_CH; c++) {
      op_t lp = {0};
      float g = gn[r] * (c == (r & 1) ? 1.0f : 0.66f);   // alternate the side
      for (int i = 0; i < ns && i + d < len; i++)
        b[(i + d) * SFX_CH + c] += op_lp(&lp, g_scr[i * SFX_CH + c], lp_hz) * g;
    }
  }
}

// A-weighting, as six one-poles.
typedef struct { op_t h1, h2, h3, h4, l1, l2; } awt_t;
static float aw_run(awt_t *s, float x) {
  x = op_hp(&s->h1, x, 20.6f);
  x = op_hp(&s->h2, x, 20.6f);
  x = op_hp(&s->h3, x, 107.7f);
  x = op_hp(&s->h4, x, 737.9f);
  x = op_lp(&s->l1, x, 12194.0f);
  x = op_lp(&s->l2, x, 12194.0f);
  return x * 1.2589f;
}

// Gain-stage FIRST, then limit, then a peak safety. tanh-then-peak-normalise is the
// wrong order twice over: tanh at a fixed drive distorts quiet material as hard as
// loud, and peak-normalising alone lets two sounds sharing a peak differ by 15 dB of
// actual loudness.
static void sfx_finish(int id, float peak, float target_rms, float win_s) {
  float *b = g_sfx_buf[id];
  int n = g_sfx_len[id] * SFX_CH;
  if (n <= 0) return;
  int w = win_s > 0 ? (int)(win_s * SFX_HZ) * SFX_CH : n;
  if (w > n || w <= 0) w = n;
  // A-weighted, per channel — see aw_run. `target_rms` is therefore a LOUDNESS,
  // and the peak safety below is what charges a bass-heavy sound for the
  // headroom it eats: a layer stack that needs 12 dB of gain to reach its
  // weighted target will run into the limiter and come back squashed, which is
  // the signal that the low end is too big rather than the level too low.
  awt_t aw[SFX_CH] = {0};
  double sq = 0;
  for (int i = 0; i < w; i++) {
    float a = aw_run(&aw[i % SFX_CH], b[i]);
    sq += (double)a * (double)a;
  }
  float rms = (float)sqrt(sq / (double)w) + 1e-9f;
  float g = target_rms > 0 ? target_rms / rms : 1.0f;
  for (int i = 0; i < n; i++) {
    float x = b[i] * g, a = x < 0 ? -x : x;
    if (a > 0.55f) {                      // soft knee, then a hard-ish ceiling
      float over = a - 0.55f;
      a = 0.55f + over / (1.0f + over * 2.6f);
      x = x < 0 ? -a : a;
    }
    b[i] = x;
  }
  float mx = 1e-6f;
  for (int i = 0; i < n; i++) {
    float a = b[i] < 0 ? -b[i] : b[i];
    if (a > mx) mx = a;
  }
  // `peak` only bites BELOW 0.9346 — that is where the soft knee above asymptotes (0.55
  // + 1/2.6), so nothing leaving that loop can exceed it and a peak argument at or over
  // 0.94 is a knob with nothing on the other end.
  if (mx > peak) { float k = peak / mx; for (int i = 0; i < n; i++) b[i] *= k; }
}

// A room, rather than an impression of one. `sfx_tail` fakes reverberation with
// band-limited noise, which cannot know what the sound WAS and decays every band at
// the same rate, so the room never DARKENS. A feedback delay network fixes both: delay
// lines of mutually incommensurate length, one damping filter each, mixed through an
// orthogonal matrix so energy circulates without growing or collapsing, read out on
// two decorrelated taps. Fed the sound already written, the tail is that sound's own
// reflections.
//   upto      frames of already-written direct sound to feed in
//   predelay  seconds to the first reflection
//   size_s    base delay-line length — the room's mean reflection interval, i.e. the
//             SIZE knob, and what a "big hall" means
//   rt60      seconds to decay 60 dB (at DC; damping shortens the top)
//   damp_hz   one-pole corner in each line; the term that makes T60lo exceed T60hi
//   wet       reverb peak as a fraction of the DRY peak, comparable with `amp`
// SIX lines, not four, with the input staggered across them: four lines fed a
// broadband shot are a comb filter with four teeth, because the impulse arrives in all
// of them on the same sample and the "room" rings on a pitch.
#define VERB_LINES 6
// 250 ms of line.
#define VERB_MAX   (SFX_HZ / 4)
static float g_verb_z[VERB_LINES][VERB_MAX];

static void sfx_verb(int id, int upto, float predelay, float size_s,
                     float rt60, float damp_hz, float wet) {
  float *b = g_sfx_buf[id];
  int len = g_sfx_len[id];
  if (upto > len) upto = len;
  if (upto > SFX_SCRATCH) upto = SFX_SCRATCH;
  // The direct sound has to be SNAPSHOT before anything is added back into the
  // buffer: the wet write at frame f lands ahead of the read at f-predelay, so
  // reading b[] directly would feed the network its own output and turn a
  // decaying room into an accumulating one.
  float dry_pk = 1e-9f;
  for (int i = 0; i < upto; i++) {
    g_scr[i] = 0.5f * (b[i * SFX_CH] + b[i * SFX_CH + 1]);
    for (int c = 0; c < SFX_CH; c++) {
      float a = b[i * SFX_CH + c] < 0 ? -b[i * SFX_CH + c] : b[i * SFX_CH + c];
      if (a > dry_pk) dry_pk = a;
    }
  }
  static const float LR[VERB_LINES] = {1.00f, 1.13f, 1.31f, 1.51f, 1.73f, 1.97f};
  static const int   IN_OFF[VERB_LINES] = {0, 37, 79, 131, 191, 263};
  int dl[VERB_LINES], idx[VERB_LINES];
  float fb[VERB_LINES];
  for (int i = 0; i < VERB_LINES; i++) {
    dl[i] = (int)(size_s * LR[i] * SFX_HZ);
    if (dl[i] < 16)       dl[i] = 16;
    if (dl[i] > VERB_MAX) dl[i] = VERB_MAX;
    // 60 dB over rt60 seconds, charged once per trip around this line.
    fb[i] = powf(10.0f, -3.0f * (float)dl[i] / ((float)SFX_HZ * rt60));
  }
  int pd = (int)(predelay * SFX_HZ);
  // Pass 0 measures the wet peak so `wet` can mean a level; pass 1 writes.
  // Re-running the network is bit for bit identical — it has no RNG in it.
  float gain = 1.0f;
  for (int pass = 0; pass < 2; pass++) {
    memset(g_verb_z, 0, sizeof g_verb_z);
    for (int i = 0; i < VERB_LINES; i++) idx[i] = 0;
    op_t lp[VERB_LINES] = {{0}, {0}, {0}, {0}, {0}, {0}};
    float wet_pk = 1e-9f;
    for (int f = 0; f < len; f++) {
      float o[VERB_LINES], sum = 0;
      for (int i = 0; i < VERB_LINES; i++) { o[i] = g_verb_z[i][idx[i]]; sum += o[i]; }
      // Householder reflection y = x - (2/N)*sum(x).
      float s = sum * (2.0f / (float)VERB_LINES);
      for (int i = 0; i < VERB_LINES; i++) {
        int src = f - pd - IN_OFF[i];
        float in = (src >= 0 && src < upto) ? g_scr[src] : 0.0f;
        float v = (o[i] - s) * fb[i] + in * ((i & 1) ? -0.4f : 0.4f);
        g_verb_z[i][idx[i]] = op_lp(&lp[i], v, damp_hz);
        idx[i] = idx[i] + 1 >= dl[i] ? 0 : idx[i] + 1;
      }
      // The two taps share no delay line, so L and R are genuinely different
      // rooms heard from one head rather than one room heard twice.
      float wl = (o[0] + o[2] + o[4]) * 0.333f;
      float wr = (o[1] + o[3] + o[5]) * 0.333f;
      if (pass == 0) {
        float a = wl < 0 ? -wl : wl, c = wr < 0 ? -wr : wr;
        if (c > a) a = c;
        if (a > wet_pk) wet_pk = a;
      } else {
        b[f * SFX_CH]     += wl * gain;
        b[f * SFX_CH + 1] += wr * gain;
      }
    }
    if (pass == 0) gain = wet * dry_pk / wet_pk;
  }
}

// A short percussive "clack" — the mechanical building block. Kept as a helper because
// the gunshots' bolt detail and dry fire all want the same thing.
static void sfx_clack(int id, int at, float tone_hz, float amp) {
  int n = sfx_burst(0.055f, tone_hz, 1.1f, 0.00004f, 0.0075f, 1.3f, amp);
  sfx_lay(id, n, at, 0.0f, 0);
  n = sfx_ring(0.05f, tone_hz * 0.42f, 190.0f, 0.011f, amp * 0.55f);
  sfx_lay(id, n, at, 0.0f, 0);
}

// ONE mechanical event, with mass and a space around it — the reload's counterpart to
// `sfx_head_sig`: one authored signature that every moving metal part is an instance
// of, so a magazine seating and a bolt going home differ in two frequencies and a
// weight rather than in two invented layer stacks.
//   knack     the transient. Sub-millisecond and high, and it must stay the loudest
//             SAMPLE of the event.
//   metal     the band that says what the part is made of (`tone_hz`)
//   spring    spring, detent, gear rattle — the connective tissue that makes it a
//             mechanism rather than isolated clicks
//   mass      `mass_hz`, at +6 ms. Laid at frame 0 a slow low excursion adds to the
//             transient's peak, the composite crest forces the limiter down, and the
//             knack is what gets eaten. Metal-then-weight is what an impact IS.
//   room      two dark early taps at +11/+19 ms: a reload is heard at 30 cm, so what
//             returns per event is your own gear and the wall a metre away, not a hall.
// THE HOLES BETWEEN THE EVENTS: the reloads are above threshold for only ~30% of their
// length, with gaps of 70-276 ms. What fills a hole is NOT another struck event — a
// sixth clack would fight `eng`, which is a contract on this buffer (atk and eng must
// both land on the LAST event, or the sound announces a live weapon early). It is the
// CONTINUOUS noise a mechanism makes between its events: broadband, quiet, no
// transient. Deliberately 12-18 dB under the events it bridges, and `width` gives each
// bed its own channel offset so the bridge is wider than the clacks it sits between.
static void sfx_bed(int id, int at, float dur, float hz, float amp, int width) {
  int n = sfx_burst(dur, hz, 0.85f, 0.020f, dur * 0.55f, 1.0f, amp);
  sfx_lay(id, n, at, 0.0f, width);
}

static void sfx_mech(int id, int at, float tone_hz, float mass_hz, float amp,
                     float spring) {
  int n, d6 = (int)(0.006f * SFX_HZ);
  n = sfx_click(0.0025f, 0.00016f, tone_hz * 1.8f, 1.45f * amp);
  sfx_lay(id, n, at, 0.0f, 0);                              // knack
  n = sfx_burst(0.065f, tone_hz, 1.15f, 0.00004f, 0.0110f, 1.3f, 2.70f * amp);
  sfx_lay(id, n, at, 0.0f, 1);                              // metal
  if (spring > 0.0f) {
    n = sfx_burst(0.09f, 3100.0f, 1.6f, 0.00006f, 0.026f, 1.5f, spring * amp);
    sfx_lay(id, n, at + (int)(0.003f * SFX_HZ), 0.0f, 2);    // spring
  }
  // 0.13, not 0.62. `amp` is a PEAK, and a two-partial ring carries roughly 15 dB more
  // ENERGY than a band-passed noise burst at the same peak, so a mass ring set by eye
  // against the metal burst's number buries it.
  float lk = f_clamp(150.0f / mass_hz, 1.0f, 1.60f);
  float low = 0.17f * lk * lk;
  n = sfx_ring(0.115f, mass_hz, mass_hz * 1.52f, 0.030f * (low / 0.17f),
               low * amp);
  sfx_lay(id, n, at + d6, 0.0f, 0);                         // mass
  n = sfx_burst(0.075f, mass_hz * 1.35f, 0.95f, 0.00012f, 0.017f, 1.1f,
                0.55f * amp);
  sfx_lay(id, n, at + d6, 0.0f, 3);                         // the weight
  // THE MEAT, and it was simply not here: every AR reload event came back with 0-3%
  // below 120 Hz and 68-70% in 500 Hz-2 kHz — all tone and knack with nothing in the
  // 200-800 Hz band where weight actually lives.
  float meat = 0.95f * f_clamp(175.0f / mass_hz, 0.42f, 1.45f);
  n = sfx_burst(0.085f, mass_hz * 3.40f, 1.05f, 0.00010f, 0.021f, 1.15f,
                meat * amp);
  sfx_lay(id, n, at + (int)(0.008f * SFX_HZ), 0.0f, 2);     // 200-800 Hz
  n = sfx_burst(0.07f, tone_hz * 0.55f, 1.0f, 0.00006f, 0.014f, 1.2f,
                0.30f * amp);
  sfx_lay(id, n, at + (int)(0.011f * SFX_HZ), -0.18f, 2);    // near tap
  n = sfx_burst(0.09f, tone_hz * 0.40f, 0.9f, 0.00008f, 0.020f, 1.1f,
                0.19f * amp);
  sfx_lay(id, n, at + (int)(0.019f * SFX_HZ), 0.22f, 4);     // far tap
}

// ---------------------------------------------------------------------------
// THE CONFIRMATION MATRIX
//
// Four buffers, but NOT four compositions. What the player is being told has two
// independent axes, and each axis gets exactly ONE authored signature:
//
//   CONSEQUENCE (exclusive)  hit = the tick        kill = tick + mass + room
//   PRECISION   (additive)   body = nothing        head = + the ding
//
//                    body                       head
//     hit            sfx_hit_base               sfx_hit_base  + sfx_head_sig
//     kill           sfx_kill_base              sfx_kill_base + sfx_head_sig
//
// Consequence is a CHOICE (a shot either killed or it did not, so the two bases are
// alternatives, never summed); precision is a LAYER, added on top of whichever base
// ran. That asymmetry is the whole design — four independent recipes cannot be kept in
// a relationship, and the pair that drifted closest together was kill/kill_head.
//
// Two load-bearing consequences. The ding plays independently of lethality: the sniper
// does 150 against 100 hp, and the AUDIO ROUTER drops the shooter's own hit
// confirmation when the event's `k` (the victim's hp after the round) is 0, so the
// sniper only ever triggers kill/kill_head — as a LAYER, its one-shot headshot carries
// the ding structurally. The gate is in the router, NOT in ent_damage: ent_damage
// pushes EV_HIT unconditionally, because the wound VFX, the killfeed and `phit` all
// need a lethal round's hit event. And the ladder is bought where the ear is:
// A-weighting peaks at 2-4 kHz, exactly where the ding lives, so the head variants
// sit above their body counterparts almost for free. It is also the band neither
// weapon occupies, so the signature never fights the shot that caused it.
// ---------------------------------------------------------------------------

// The PRECISION signature: "that was the head".
#define SFX_HEAD_DUCK 0.90f
static void sfx_head_sig(int id, float g) {
  int n;
  // The one layer that reaches above 8 kHz.
  n = sfx_click(0.0015f, 0.00012f, 7000.0f, 0.80f * g);
  sfx_lay(id, n, 0, 0.0f, 0);                          // air
  n = sfx_ring(0.20f, 4500.0f, 6700.0f, 0.048f, 1.00f * g);
  sfx_lay(id, n, 0, 0.0f, 0);                          // THE DING
  n = sfx_ring(0.15f, 2950.0f, 5200.0f, 0.032f, 0.58f * g);
  sfx_lay(id, n, 2, 0.0f, 3);                          // beating = shimmer
  n = sfx_burst(0.06f, 2200.0f, 1.4f, 0.00002f, 0.011f, 1.6f, 1.75f * g);
  sfx_lay(id, n, 0, 0.0f, 1);                          // bone crack
  // 5400, not 6000: sfx_tail runs its second pole at hz*1.35 and the Chamberlin
  // SVF is only stable below SFX_HZ/6.5 = 7385 Hz, so 6000 asks for 8100 and is
  // silently clamped — the sizzle would not be the filter it says it is.
  // 5400*1.35 = 7290 leaves 95 Hz of margin.
  sfx_tail(id, (int)(0.001f * SFX_HZ), 0.18f, 5400.0f, 1.1f,
           0.003f, 0.032f, 0.16f * g);                       // sizzle
}

// CONSEQUENCE, alternative 1: a body being hit and still standing. It has to be heard
// THROUGH the shot that caused it, which is a spectrum problem and not a volume one —
// the two are simultaneous by definition, and the AR carries real energy to ~4 kHz, so
// the tick sits above it.
//   razor + tick    WHERE and WHAT (survives being under a gunshot)
//   impact snap     that it was a bullet and not a UI beep
//   meat            the mid band; left at 2%, the hole between tick and thump is why
//                   it read as thin
//   thump + weight  the part you feel
// Two rules about WHERE fat lives. (1) Not in the bottom octave: a sine carries ~10 dB
// more energy than a band-passed burst at the same peak, so a loud sub sweep needs so
// much gain that the limiter crushes the crest, and a squashed hitmarker is not a heavy
// one. The fat is 190-780 Hz, where the ear is 20 dB more sensitive. (2) The low layers
// are OFFSET 6-10 ms, or their slow excursions add to the transient's peak and force
// the limiter down on everything. Crack-then-thud is what an impact is anyway.
static void sfx_hit_attack(int id, float g) {
  int n;
  n = sfx_click(0.003f, 0.00026f, 2600.0f, 1.55f * g);
  sfx_lay(id, n, 0, 0.0f, 0);                          // razor
  n = sfx_ring(0.075f, 2650.0f, 3950.0f, 0.017f, 1.30f * g);
  sfx_lay(id, n, 0, 0.0f, 0);                          // the tick
  n = sfx_burst(0.055f, 1900.0f, 1.3f, 0.00002f, 0.010f, 1.6f, 2.60f * g);
  sfx_lay(id, n, 0, 0.0f, 1);                          // impact snap
}

// THE ROOM IS PART OF THE CONSEQUENCE AXIS, so it is authored once per consequence and
// applied to both precision variants.
static void sfx_room_hit(int id) {   // 8 ms of decay, so the ring is not a beep
  sfx_verb(id, (int)(0.09f * SFX_HZ), 0.005f, 0.013f, 0.13f, 3400.0f, 0.20f);
}
static void sfx_room_kill(int id) {  // a real room: a kill has size a hit lacks
  sfx_verb(id, (int)(0.30f * SFX_HZ), 0.011f, 0.030f, 0.42f, 2200.0f, 0.30f);
}

static void sfx_hit_base(int id, float g) {
  int n;
  sfx_hit_attack(id, g);
  n = sfx_burst(0.09f, 780.0f, 1.1f, 0.00005f, 0.020f, 1.3f, 2.80f * g);
  sfx_lay(id, n, (int)(0.001f * SFX_HZ), 0.0f, 2);     // meat
  n = sfx_ring(0.06f, 430.0f, 660.0f, 0.016f, 0.85f * g);
  sfx_lay(id, n, (int)(0.002f * SFX_HZ), 0.0f, 2);     // the thock
  n = sfx_burst(0.15f, 190.0f, 0.95f, 0.00015f, 0.042f, 1.1f, 0.90f * g);
  sfx_lay(id, n, (int)(0.006f * SFX_HZ), 0.0f, 3);     // the thump
  n = sfx_ring(0.11f, 104.0f, 158.0f, 0.028f, 0.26f * g);
  sfx_lay(id, n, (int)(0.010f * SFX_HZ), 0.0f, 0);     // the weight
}

// CONSEQUENCE, alternative 2: the target goes down. The loudest event in the game
// and the only one allowed to be. Everything the hit does, plus the two things a hit
// must NOT have: mass that keeps developing after the transient, and a real room.
//   razor + ring    the confirmation, in the band the gun does not own
//   crunch          bone — a 1.15 kHz burst with a 15 ms decay, something breaking
//                   rather than being hit
//   meat + mass     430 Hz and 105 Hz, the two loudest layers in the sound
//   body tone       a 330/500 Hz RING, because 24 ms at 330 Hz is eight cycles and a
//                   burst that short is a random-phase lottery per build
//   the drop        an 88/132 Hz ring at +11 ms. A ring, not a sweep: a sweep at the
//                   same peak carries ~10 dB more energy and the low-end budget is
//                   already spent on the mass.
//   the arrival     at +95 ms, low-passed to nothing but weight — the body reaching
//                   the floor, and being pure low end it cannot smear the tick
//   resolution      two notes, 6 dB under everything else. Level with the impact
//                   they put the kill's ENERGY peak 70 ms late, which reads as a
//                   jingle afterwards rather than as caused by the trigger. `eng`
//                   must stay inside the ~50 ms window where the ear fuses events.
//
// THE CONSEQUENCE AXIS WAS INVERTED IN THE ATTACK: the kill had re-authored a
// second, DARKER transient, so over the first 20 ms it measured 4.57 dB less
// 2-6 kHz than the HIT it replaces and only overtook it at a 150 ms integration —
// the most rewarding event in the game had the dullest onset. It now SHARES the
// hit's transient trio at bake time and its own layers are only what a kill ADDS.
// That does not violate "the two bases are alternatives, never summed", which is
// about PLAY time (one voice, one buffer), not about sharing an attack at bake time.
static void sfx_kill_base(int id, float g) {
  int n;
  sfx_hit_attack(id, g);                               // the SAME onset
  n = sfx_burst(0.085f, 1150.0f, 1.2f, 0.00003f, 0.015f, 1.5f, 2.30f * g);
  sfx_lay(id, n, 0, 0.0f, 1);                          // the crunch
  n = sfx_burst(0.13f, 430.0f, 1.0f, 0.00007f, 0.032f, 1.2f, 2.10f * g);
  sfx_lay(id, n, (int)(0.001f * SFX_HZ), 0.0f, 2);     // meat
  n = sfx_ring(0.09f, 330.0f, 500.0f, 0.024f, 0.85f * g);
  sfx_lay(id, n, (int)(0.002f * SFX_HZ), 0.0f, 2);     // the body tone
  n = sfx_burst(0.22f, 105.0f, 0.95f, 0.00020f, 0.070f, 1.0f, 1.05f * g);
  sfx_lay(id, n, (int)(0.007f * SFX_HZ), 0.0f, 3);     // the mass
  n = sfx_ring(0.16f, 88.0f, 132.0f, 0.045f, 0.42f * g);
  sfx_lay(id, n, (int)(0.011f * SFX_HZ), 0.0f, 0);     // the drop
  n = sfx_burst(0.16f, 120.0f, 0.9f, 0.00030f, 0.045f, 1.0f, 0.50f * g);
  sfx_lay(id, n, (int)(0.095f * SFX_HZ), 0.05f, 4);    // the arrival
  static const float KF[2] = {1180.0f, 1770.0f};
  for (int k = 0; k < 2; k++) {
    n = sfx_ring(0.13f, KF[k], KF[k] * 1.5f, 0.028f, 0.40f * g);
    sfx_lay(id, n, (int)((0.070f + 0.048f * (float)k) * SFX_HZ),
            (k & 1) ? 0.20f : -0.20f, k * 2);                // resolution
  }
}

// ---------------------------------------------------------------------------
// Every sound is built the same way: a sub-millisecond TRANSIENT, then a
// CRACK (the band that carries the "rifle"), then a BODY, then a SUB, then mechanical
// detail, then early REFLECTIONS, then a decorrelated TAIL. The old shots had the body
// and the sub and almost nothing else: measured, 94% of the AR's energy sat below 250
// Hz and only 2.9% above 2 kHz, which is a thud, not a report.
// ---------------------------------------------------------------------------
static void sfx_build_shots(void) {
  int n;  // sfx_alloc reseeds g_rng_sfx per sound id — no global seed here

  // ---- AR: sharp, mid-forward, short room -------------------------------
  // 0.66 s, not 0.52: the room below decays over 440 ms from a 9 ms predelay,
  // and a reverb tail that runs off the end of its buffer is a step to zero —
  // i.e. a click, at the exact moment the ear is listening to the decay.
  sfx_alloc(SFX_FIRE_AR, 0.66f);
  n = sfx_click(0.004f, 0.00055f, 1800.0f, 1.75f);
  sfx_lay(SFX_FIRE_AR, n, 0, 0.0f, 0);                 // transient
  n = sfx_burst(0.08f, 4300.0f, 1.7f, 0.00002f, 0.011f, 1.7f, 0.88f);
  sfx_lay(SFX_FIRE_AR, n, 0, 0.0f, 0);                 // snap
  n = sfx_burst(0.10f, 2500.0f, 1.4f, 0.00003f, 0.022f, 1.5f, 1.30f);
  sfx_lay(SFX_FIRE_AR, n, 0, 0.0f, 0);                 // crack
  n = sfx_burst(0.12f, 1150.0f, 1.2f, 0.00006f, 0.034f, 1.2f, 0.95f);
  sfx_lay(SFX_FIRE_AR, n, 1, -0.06f, 2);               // upper body
  n = sfx_burst(0.16f, 240.0f, 1.0f, 0.00010f, 0.045f, 1.0f, 0.58f);
  sfx_lay(SFX_FIRE_AR, n, 2, 0.05f, 3);                // body
  n = sfx_sweep(0.20f, 190.0f, 62.0f, 0.026f, 0.050f, 1.0f, 0.46f);
  sfx_lay(SFX_FIRE_AR, n, 0, 0.0f, 0);                 // sub punch
  sfx_clack(SFX_FIRE_AR, (int)(0.006f * SFX_HZ), 3400.0f, 0.20f);  // bolt
  {   // nearby cover: 1.9 / 3.6 / 5.8 m, there and back
    static const float D[3] = {0.011f, 0.021f, 0.034f};
    static const float G[3] = {0.30f, 0.20f, 0.13f};
    sfx_reflect(SFX_FIRE_AR, (int)(0.09f * SFX_HZ), D, G, 3, 3200.0f);
  }
  {   // ...and the arena. Both weapons are fired inside the same 48 m box, so
      // both get the same wall returns — the AR's are just quieter and darker
      // under a shot that is itself over in 80 ms.
    static const float D[2] = {0.140f, 0.198f};
    static const float G[2] = {0.14f, 0.09f};
    sfx_reflect(SFX_FIRE_AR, (int)(0.07f * SFX_HZ), D, G, 2, 900.0f);
  }
  // One real room instead of two noise tails.
  sfx_verb(SFX_FIRE_AR, (int)(0.24f * SFX_HZ), 0.009f, 0.027f, 0.44f, 2600.0f,
           0.30f);
  sfx_tail(SFX_FIRE_AR, (int)(0.006f * SFX_HZ), 0.22f, 2600.0f, 0.9f,
           0.005f, 0.055f, 0.13f);   // air, on top of the walls
  sfx_finish(SFX_FIRE_AR, 0.97f, 0.1748f, 0.30f);

  // ---- Sniper: a pressure wave, and then the whole arena answering --------
  // "Mightier" and "more reverberant" are two jobs solved in two places. POWER is the
  // first 60 ms.
  sfx_alloc(SFX_FIRE_SR, 2.45f);
  n = sfx_click(0.007f, 0.00090f, 1150.0f, 3.10f);
  sfx_lay(SFX_FIRE_SR, n, 0, 0.0f, 0);                 // transient
  // A ONE-SHOT WEAPON HAS TO BE FRIGHTENING, and fright lives in the crack, not in the
  // bottom octave.
  n = sfx_burst(0.055f, 5600.0f, 1.9f, 0.00002f, 0.0085f, 1.9f, 4.90f);
  sfx_lay(SFX_FIRE_SR, n, 0, 0.0f, 0);                 // whip
  n = sfx_burst(0.105f, 3600.0f, 1.7f, 0.00002f, 0.019f, 1.8f, 4.40f);
  sfx_lay(SFX_FIRE_SR, n, 0, 0.0f, 0);                 // snap
  // 2500, NOT 1750, AND THIS IS THE ONE THAT MADE THE SNIPER BLUNT: its only long-decay
  // layer had been moved DOWN out of 2-6 kHz, the band this file's own rule calls
  // fright and localisation, so it self-darkened 6.3 dB per 25 ms against the AR's 2.4.
  n = sfx_burst(0.17f, 2500.0f, 1.35f, 0.00004f, 0.048f, 1.4f, 3.20f);
  sfx_lay(SFX_FIRE_SR, n, 0, 0.0f, 0);                 // crack
  // THE SNARL. A big rifle does not just crack, it TEARS: the report falls in pitch
  // over the first tenth of a second as the muzzle blast expands, and that falling edge
  // is the most recognisable thing about a heavy bolt gun.
  n = sfx_sweep(0.125f, 2150.0f, 560.0f, 0.028f, 0.066f, 1.25f, 1.20f);
  sfx_lay(SFX_FIRE_SR, n, (int)(0.003f * SFX_HZ), 0.0f, 0);
  n = sfx_burst(0.26f, 620.0f, 1.1f, 0.00008f, 0.062f, 1.1f, 1.85f);
  sfx_lay(SFX_FIRE_SR, n, 1, -0.07f, 3);               // chest
  n = sfx_burst(0.34f, 150.0f, 1.0f, 0.00012f, 0.090f, 1.0f, 0.95f);
  sfx_lay(SFX_FIRE_SR, n, 2, 0.06f, 4);                // body
  // Bottoming out at 45 Hz bought nothing — headphones roll off below 50 and the ear is
  // 25 dB down there — while eating the headroom the crack needs.
  n = sfx_sweep(0.26f, 165.0f, 62.0f, 0.030f, 0.062f, 1.0f, 1.05f);
  sfx_lay(SFX_FIRE_SR, n, (int)(0.008f * SFX_HZ), 0.0f, 0);  // the pressure
  {   // nearby cover: 2.7 / 5 / 8 / 12 m
    static const float D[4] = {0.016f, 0.029f, 0.047f, 0.071f};
    static const float G[4] = {0.34f, 0.26f, 0.19f, 0.13f};
    sfx_reflect(SFX_FIRE_SR, (int)(0.13f * SFX_HZ), D, G, 4, 2200.0f);
  }
  {   // the arena: side walls, a corner, the far wall, and one second-order
      // return.
    static const float D[4] = {0.140f, 0.198f, 0.245f, 0.292f};
    static const float G[4] = {0.38f, 0.29f, 0.21f, 0.15f};
    sfx_reflect(SFX_FIRE_SR, (int)(0.16f * SFX_HZ), D, G, 4, 1200.0f);
  }
  sfx_clack(SFX_FIRE_SR, (int)(0.34f * SFX_HZ), 1000.0f, 0.42f);   // bolt back
  sfx_clack(SFX_FIRE_SR, (int)(0.50f * SFX_HZ), 1700.0f, 0.36f);   // bolt home
  // The hall.
  sfx_verb(SFX_FIRE_SR, (int)(0.62f * SFX_HZ), 0.014f, 0.055f, 1.95f, 2000.0f,
           0.33f);
  sfx_finish(SFX_FIRE_SR, 0.99f, 0.2680f, 0.30f);

  // ---- The same two shots heard from across the arena --------------------
  // Distance is not a volume knob. Air eats the high end, the direct crack arrives dry
  // and thin, and everything else you hear is the town: a long, dark, wide tail.
  for (int fr = 0; fr < 2; fr++) {
    int id = fr ? SFX_FAR_SR : SFX_FAR_AR;
    sfx_alloc(id, fr ? 1.30f : 0.85f);
    // No sharp click: at 40 m the wavefront has been smeared by the air and by
    // everything it reflected off on the way.
    n = sfx_burst(0.05f, 1100.0f, 0.8f, 0.0010f, 0.010f, 1.1f, 0.40f);
    sfx_lay(id, n, 0, 0.0f, 0);
    n = sfx_burst(0.10f, fr ? 900.0f : 1250.0f, 1.1f, 0.00008f,
                  fr ? 0.020f : 0.014f, 1.3f, 0.55f);
    sfx_lay(id, n, 0, 0.0f, 0);                        // thinned crack
    n = sfx_burst(0.26f, fr ? 150.0f : 210.0f, 0.9f, 0.0004f,
                  fr ? 0.075f : 0.055f, 1.0f, 0.52f);
    sfx_lay(id, n, 2, 0.0f, 2);                        // the "thump"
    {
      static const float D[4] = {0.024f, 0.043f, 0.068f, 0.099f};
      static const float G[4] = {0.40f, 0.31f, 0.23f, 0.16f};
      sfx_reflect(id, (int)(0.12f * SFX_HZ), D, G, 4, 1400.0f);
    }
    // At 30 m what reaches you is the arena, not the muzzle: the tail has to be the
    // loudest layer, and being generated per channel it is also the only decorrelated
    // one.
    sfx_tail(id, (int)(0.018f * SFX_HZ), fr ? 1.10f : 0.72f,
             fr ? 320.0f : 380.0f, 0.7f, 0.030f, fr ? 0.290f : 0.185f, 0.62f);
    sfx_tail(id, (int)(0.026f * SFX_HZ), fr ? 0.80f : 0.55f,
             fr ? 900.0f : 1150.0f, 0.8f, 0.040f, fr ? 0.150f : 0.105f, 0.55f);
    sfx_finish(id, 0.92f, 0.0876f, 0.0f);
  }
}

static void sfx_build_confirms(void) {
  int n;  // sfx_alloc reseeds g_rng_sfx per sound id — no global seed here

  // ---- The four confirmations --------------------------------------------
  // Two axes, three recipes, four buffers. See THE CONFIRMATION MATRIX above
  // `sfx_head_sig` for what each piece is.

  sfx_alloc(SFX_HIT, 0.30f);
  sfx_hit_base(SFX_HIT, 1.00f);
  sfx_room_hit(SFX_HIT);
  sfx_finish(SFX_HIT, 0.94f, 0.4200f, 0.30f);

  // The two signature gains differ, and the reason is not taste. `g` scales the
  // signature against WHATEVER BASE IT IS RIDING ON, and the kill base is a much bigger
  // sound than the hit base — same ding at the same number would read as a sizzle on
  // the hit (measured: 63% of the buffer's energy above 2 kHz) and as almost nothing on
  // the kill (centroid moved 451 -> 1083 Hz and the level did not move at all).
  sfx_alloc(SFX_HIT_HEAD, 0.30f);
  sfx_hit_base(SFX_HIT_HEAD, SFX_HEAD_DUCK);
  sfx_head_sig(SFX_HIT_HEAD, 0.80f);
  sfx_room_hit(SFX_HIT_HEAD);
  sfx_finish(SFX_HIT_HEAD, 0.95f, 0.2620f, 0.30f);

  sfx_alloc(SFX_KILL, 0.72f);
  sfx_kill_base(SFX_KILL, 1.00f);
  sfx_room_kill(SFX_KILL);
  // 0.3750, not 0.3470: sharing the hit's transient made the kill's own onset
  // brighter and its gain stage therefore quieter, and measured the 300 ms
  // ladder step from hit_head to kill had thinned to 0.22 dB. Re-derived rather
  // than left, because that ladder IS the consequence axis; the crest comes back
  // at 11.7 dB (the number this file records as a squashed hitmarker is 8.4) and
  // an isolated kill still renders at limiter 0.89 with clipped = 0.
  sfx_finish(SFX_KILL, 0.96f, 0.3750f, 0.30f);

  sfx_alloc(SFX_KILL_HEAD, 0.72f);
  sfx_kill_base(SFX_KILL_HEAD, SFX_HEAD_DUCK);
  sfx_head_sig(SFX_KILL_HEAD, 1.45f);
  sfx_room_kill(SFX_KILL_HEAD);
  sfx_finish(SFX_KILL_HEAD, 0.96f, 0.3120f, 0.30f);

  // ---- Taking a round ----------------------------------------------------
  // Deliberately dark and dull: it must never be mistaken for your own hitmarker, which
  // is the mistake the old build made by replaying the hit tick at 0.62x rate.
  sfx_alloc(SFX_HURT, 0.30f);
  n = sfx_click(0.004f, 0.00060f, 300.0f, 0.60f);
  sfx_lay(SFX_HURT, n, 0, 0.0f, 0);
  n = sfx_burst(0.16f, 380.0f, 0.9f, 0.00020f, 0.055f, 1.2f, 1.05f);
  sfx_lay(SFX_HURT, n, 0, 0.0f, 3);
  n = sfx_burst(0.12f, 900.0f, 1.1f, 0.00008f, 0.028f, 1.3f, 0.55f);
  sfx_lay(SFX_HURT, n, 0, 0.0f, 2);   // enough edge to survive a laptop
  n = sfx_sweep(0.26f, 145.0f, 52.0f, 0.030f, 0.075f, 1.0f, 0.62f);
  sfx_lay(SFX_HURT, n, 0, 0.0f, 0);
  sfx_tail(SFX_HURT, (int)(0.006f * SFX_HZ), 0.26f, 260.0f, 0.7f,
           0.012f, 0.075f, 0.30f);
  sfx_finish(SFX_HURT, 0.90f, 0.0663f, 0.30f);
}

static void sfx_build_world(void) {
  int n;  // sfx_alloc reseeds g_rng_sfx per sound id — no global seed here

  // ---- A round arriving in the world -------------------------------------
  sfx_alloc(SFX_IMPACT, 0.26f);
  n = sfx_click(0.003f, 0.00035f, 2800.0f, 0.80f);
  sfx_lay(SFX_IMPACT, n, 0, 0.0f, 0);
  n = sfx_burst(0.09f, 3100.0f, 1.3f, 0.00004f, 0.014f, 1.6f, 0.70f);
  sfx_lay(SFX_IMPACT, n, 0, 0.0f, 1);                  // stone chip
  n = sfx_burst(0.14f, 520.0f, 1.0f, 0.00012f, 0.038f, 1.2f, 0.60f);
  sfx_lay(SFX_IMPACT, n, 1, 0.0f, 2);
  n = sfx_burst(0.20f, 130.0f, 0.9f, 0.00030f, 0.055f, 1.0f, 0.45f);
  sfx_lay(SFX_IMPACT, n, 2, 0.0f, 3);                  // dust thump
  sfx_tail(SFX_IMPACT, (int)(0.005f * SFX_HZ), 0.22f, 900.0f, 0.8f,
           0.008f, 0.060f, 0.24f);
  sfx_finish(SFX_IMPACT, 0.85f, 0.0940f, 0.30f);

  // ---- ...and a round arriving in a MAN -----------------------------------
  // A ROUND LANDING IN A BODY WAS SILENT IN THE WORLD WHILE A ROUND LANDING IN A WALL
  // WAS NOT: aimed at a bot, sfxlog routed the shot and the player's own screen-space
  // confirmation and nothing else, so a bot shooting another bot five metres in front
  // of you produced a muzzle report and silence at the target.
  sfx_alloc(SFX_FLESH, 0.22f);
  n = sfx_click(0.0025f, 0.00030f, 1400.0f, 1.10f);
  sfx_lay(SFX_FLESH, n, 0, 0.0f, 0);                   // the arrival
  n = sfx_burst(0.075f, 1900.0f, 1.2f, 0.00004f, 0.016f, 1.4f, 2.20f);
  sfx_lay(SFX_FLESH, n, 0, 0.0f, 1);                   // the slap
  n = sfx_burst(0.13f, 620.0f, 1.0f, 0.00010f, 0.034f, 1.2f, 1.55f);
  sfx_lay(SFX_FLESH, n, (int)(0.002f * SFX_HZ), 0.0f, 2);  // the meat
  n = sfx_burst(0.17f, 190.0f, 0.9f, 0.00025f, 0.050f, 1.0f, 0.80f);
  sfx_lay(SFX_FLESH, n, (int)(0.008f * SFX_HZ), 0.0f, 3);  // the thump
  // Per-event reflections, in the reload's idiom rather than a global verb: what
  // comes back off a body hit at 8 m is the nearest wall, not the whole arena.
  {
    static const float D[2] = {0.032f, 0.086f};
    static const float G[2] = {0.20f, 0.11f};
    sfx_reflect(SFX_FLESH, (int)(0.07f * SFX_HZ), D, G, 2, 1100.0f);
  }
  sfx_finish(SFX_FLESH, 0.86f, 0.0700f, 0.30f);

  // ---- One going past your head -------------------------------------------
  // A supersonic round's bow wave: a very short, very bright band that sweeps DOWN as
  // it passes and pans across.
  sfx_alloc(SFX_WHIZZ, 0.30f);
  n = sfx_click(0.0025f, 0.00022f, 3600.0f, 3.40f);
  sfx_lay(SFX_WHIZZ, n, 0, 0.0f, 0);                   // the pressure step
  // AN N-WAVE HAS TWO SHOCKS. The bow and the tail of the pressure profile are
  // ~1.3 ms apart at rifle calibre, and the ear fuses the pair into one snap
  // that is distinctly NASTIER than either click alone — the double-shock is
  // most of why a real near miss sounds like a whip and a synthetic one like a
  // camera flash.
  n = sfx_click(0.0022f, 0.00020f, 2900.0f, 2.10f);
  sfx_lay(SFX_WHIZZ, n, (int)(0.0013f * SFX_HZ), 0.0f, 0);   // the tail shock
  // ...and the AIR ITSELF gets punched: a short 175 Hz thump under the snap.
  // Threat lives down here, not in the treble — and it is deliberately small,
  // because sfx_finish charges the whole buffer for every dB of low end.
  n = sfx_burst(0.050f, 175.0f, 1.0f, 0.00010f, 0.016f, 1.1f, 0.80f);
  sfx_lay(SFX_WHIZZ, n, (int)(0.002f * SFX_HZ), 0.0f, 1);    // the air punch
  // KNOWN EXCEPTION to "every generator ends in scr_norm": this wake sweep writes
  // filtered noise straight into the buffer at a hard 3.0, so its level rides the
  // filter's response instead of a normalised peak.
  for (int c = 0; c < 2; c++) {
    int m = (int)(0.16f * SFX_HZ);
    svf_t f1 = {0, 0};
    float *b = g_sfx_buf[SFX_WHIZZ];
    for (int i = 0; i < m; i++) {
      float t = (float)i / SFX_HZ;
      float hz = 5200.0f * expf(-t / 0.018f) + 700.0f;
      float x = svf(&f1, rng_rangef(&g_rng_sfx, -1, 1), hz, 2.2f);
      b[i * SFX_CH + c] += x * sfx_env(t, 0.0006f, 0.024f, 1.3f) * 3.0f;
    }
  }
  // 1.50, and the number is a sweep rather than the 1.7-2.3 rule of thumb this file
  // quotes for a mid burst that has to be heard UNDER something else.
  n = sfx_burst(0.055f, 640.0f, 1.2f, 0.00004f, 0.014f, 1.3f, 1.50f);
  sfx_lay(SFX_WHIZZ, n, (int)(0.005f * SFX_HZ), 0.0f, 2);   // the body
  // THE ZIP: turbulence in the wake, darker and longer than the bow sweep — the
  // angry-bee tail that says the round kept going somewhere behind you. 12-15 dB
  // under the crack it follows, or the whole event turns into a cartoon ricochet.
  {
    int m = (int)(0.14f * SFX_HZ);
    for (int c = 0; c < 2; c++) {
      svf_t f2 = {0, 0};
      float *b = g_sfx_buf[SFX_WHIZZ];
      for (int i = 0; i < m; i++) {
        float t = (float)i / SFX_HZ;
        float hz = 980.0f * expf(-t / 0.075f) + 430.0f;
        float x = svf(&f2, rng_rangef(&g_rng_sfx, -1, 1), hz, 1.6f);
        b[(i + (int)(0.008f * SFX_HZ)) * SFX_CH + c] +=
            x * sfx_env(t, 0.004f, 0.055f, 1.2f) * 0.55f;
      }
    }
  }
  // ...AND IT HAPPENS IN THE ARENA. This was one of only three sounds in the file with
  // neither a verb nor a reflection — a supersonic crack in a 48 m concrete box is the
  // single most obviously ROOMED thing in it.
  {
    static const float D[2] = {0.055f, 0.140f};
    static const float G[2] = {0.14f, 0.08f};
    sfx_reflect(SFX_WHIZZ, (int)(0.09f * SFX_HZ), D, G, 2, 1800.0f);
  }
  sfx_finish(SFX_WHIZZ, 0.86f, 0.0896f, 0.30f);

  // ---- a boot landing ------------------------------------------------------
  // Short, dry and quiet.
  sfx_alloc(SFX_STEP, 0.16f);
  // The two bright layers are generated PER CHANNEL from independent noise, not laid
  // once and offset by a sample or two.
  for (int c = 0; c < 2; c++) {
    float *b = g_sfx_buf[SFX_STEP];
    n = sfx_burst(0.05f, 2400.0f, 0.9f, 0.00008f, 0.016f, 1.5f, 2.20f);
    for (int i = 0; i < n; i++) b[i * SFX_CH + c] += g_scr[i];
    n = sfx_burst(0.04f, 5200.0f, 1.4f, 0.00005f, 0.006f, 1.6f, 1.40f);
    for (int i = 0; i < n; i++) b[(i + 1) * SFX_CH + c] += g_scr[i];
  }
  n = sfx_burst(0.10f, 620.0f, 0.8f, 0.00020f, 0.022f, 1.3f, 0.75f);
  sfx_lay(SFX_STEP, n, 0, 0.0f, 2);                    // sole
  n = sfx_burst(0.13f, 135.0f, 0.9f, 0.00040f, 0.034f, 1.1f, 0.62f);
  sfx_lay(SFX_STEP, n, 1, 0.0f, 3);                    // weight
  // ...and a boot lands in the arena like everything else does.
  {
    static const float D[2] = {0.022f, 0.062f};
    static const float G[2] = {0.24f, 0.13f};
    sfx_reflect(SFX_STEP, (int)(0.05f * SFX_HZ), D, G, 2, 1400.0f);
  }
  sfx_finish(SFX_STEP, 0.70f, 0.0477f, 0.30f);
}

static void sfx_build_mech(void) {
  int n;  // sfx_alloc reseeds g_rng_sfx per sound id — no global seed here

  // ---- dry fire and reload -------------------------------------------------
  // A hammer falling on an empty chamber is a CLACK WITH WEIGHT. The rewrite turned it
  // into nearly pure click — the 80-250 Hz share fell from 82% to 5% — which reads as a
  // UI tick rather than as a mechanism.
  sfx_alloc(SFX_DRY, 0.12f);
  sfx_clack(SFX_DRY, 0, 2600.0f, 0.55f);
  n = sfx_burst(0.05f, 4200.0f, 1.6f, 0.00003f, 0.006f, 1.5f, 0.30f);
  sfx_lay(SFX_DRY, n, 0, 0.0f, 0);
  n = sfx_burst(0.09f, 175.0f, 1.0f, 0.00012f, 0.022f, 1.2f, 0.95f);
  sfx_lay(SFX_DRY, n, 1, 0.0f, 2);
  n = sfx_ring(0.08f, 330.0f, 520.0f, 0.016f, 0.45f);
  sfx_lay(SFX_DRY, n, 1, 0.0f, 1);
  sfx_finish(SFX_DRY, 0.62f, 0.0374f, 0.30f);

  // RELOADS ARE PER WEAPON AND TIMED FROM reload_ticks.
  {
    const float hz = (float)TICK_HZ;
    float t_ar = (float)WPN_DEF[WPN_AR].reload_ticks / hz;
    // FIVE events, not four.
    sfx_alloc(SFX_RELOAD_AR, t_ar + 0.22f);
    sfx_mech(SFX_RELOAD_AR, (int)(0.055f * t_ar * SFX_HZ), 1900.0f, 240.0f,
             0.42f, 0.10f);                              // mag release button
    // ...and the catch snapping back 14 ms later: a button is TWO clicks — in
    // and out — and one click reads as UI where a click-clack reads as a part.
    n = sfx_click(0.0030f, 0.00020f, 3300.0f, 0.30f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(0.055f * t_ar * SFX_HZ + 0.014f * SFX_HZ),
            0.0f, 0);
    sfx_mech(SFX_RELOAD_AR, (int)(0.175f * t_ar * SFX_HZ), 950.0f, 190.0f,
             0.54f, 0.55f);                              // magazine falls free
    sfx_mech(SFX_RELOAD_AR, (int)(0.360f * t_ar * SFX_HZ), 760.0f, 150.0f,
             0.34f, 0.30f);                              // fresh mag to the well
    // 820, not 1150: a magazine body going into an alloy well is not a 1.1 kHz
    // ping, and at 1150 the event's LOUDEST band was 700 Hz-2 kHz with the meat
    // 3.3 dB under it. 98, not 122: the ring's fundamental has to land INSIDE
    // the 20-120 Hz band rather than 2 Hz above it — this file's own note says
    // a ~104 Hz ring at amp 0.30 is enough to be FELT, and the whole reload
    // measured -43.5 dB at 20-120 Hz against the gunshot's -22.3.
    sfx_mech(SFX_RELOAD_AR, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ), 820.0f, 98.0f,
             0.94f, 0.22f);                              // THE SEAT
    // ...WITH REAL SUB UNDER IT. The whole buffer measured 0% share below
    // 120 Hz — the 98 Hz mass ring inside sfx_mech is one partial at 0.13 of
    // the event and it vanishes against the mid. A 63 Hz ring at +7 ms is the
    // slam FELT in the wrists; deliberately moderate, because sfx_finish's
    // A-weighted gain stage charges the whole buffer for every dB spent here.
    n = sfx_ring(0.110f, 63.0f, 96.0f, 0.026f, 0.20f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ
                                    + 0.007f * SFX_HZ), 0.0f, 0);
    n = sfx_click(0.0025f, 0.00018f, 2100.0f, 0.36f);    // ...and the SNAP on top
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ),
            0.0f, 0);
    // The bolt has to STAY the loudest event, and that is a contract rather than a
    // preference: `eng` — the energy peak — is the moment the sound says A LIVE WEAPON,
    // and it is the one thing in this sound the player acts on.
    sfx_mech(SFX_RELOAD_AR, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ), 2100.0f, 235.0f,
             1.66f, 0.14f);                              // bolt release
    // The bolt slamming into battery is the heaviest metal-on-metal contact in
    // the whole mechanism, so it gets MORE weight than the seat (which also
    // keeps `eng` on this event, the buffer's contract): a 116 Hz chunk at
    // +6 ms and the buffer/spring SHRING of the carrier running its rails.
    n = sfx_ring(0.135f, 116.0f, 176.0f, 0.030f, 0.52f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ
                                    + 0.006f * SFX_HZ), 0.0f, 0);
    n = sfx_burst(0.070f, 3600.0f, 1.6f, 0.00005f, 0.020f, 1.5f, 0.40f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ
                                    + 0.003f * SFX_HZ), 0.06f, 1);
    // A SIXTH EVENT, and it fills the largest hole in the sound: between the magazine
    // seating and the bolt going home there were 386 ms of sample-exact silence in one
    // block — the mechanism stopped existing for a third of a second, in exactly the
    // window where the ANIMATION also goes static.
    sfx_bed(SFX_RELOAD_AR, (int)(0.235f * t_ar * SFX_HZ), 0.130f, 2600.0f, 0.14f, 7);
    sfx_bed(SFX_RELOAD_AR, (int)(0.400f * t_ar * SFX_HZ), 0.150f, 2300.0f, 0.16f, 5);
    sfx_bed(SFX_RELOAD_AR, (int)(0.600f * t_ar * SFX_HZ), 0.140f, 1750.0f, 0.13f, 9);
    sfx_bed(SFX_RELOAD_AR, (int)(0.680f * t_ar * SFX_HZ), 0.110f, 2900.0f, 0.10f, 3);
    sfx_mech(SFX_RELOAD_AR, (int)(0.760f * t_ar * SFX_HZ), 620.0f, 165.0f,
             0.30f, 0.0f);                               // hand to the handle
    n = sfx_burst(0.10f, 2600.0f, 1.4f, 0.00004f, 0.012f, 1.4f, 0.66f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ), 0.10f, 2);
    // ...AND THE TACTICAL VARIANT, which is the same mechanism with the one event that
    // cannot have happened removed.
    {
      float t_tac = (float)WPN_DEF[WPN_AR].reload_tac_ticks / hz;
      sfx_alloc(SFX_RELOAD_AR_TAC, t_tac + 0.22f);
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.055f * t_tac * SFX_HZ), 1900.0f, 240.0f,
               0.42f, 0.10f);                            // mag release button
      n = sfx_click(0.0030f, 0.00020f, 3300.0f, 0.30f);  // the catch, see above
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(0.055f * t_tac * SFX_HZ
                                          + 0.014f * SFX_HZ), 0.0f, 0);
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.185f * t_tac * SFX_HZ), 950.0f, 190.0f,
               0.54f, 0.55f);                            // magazine falls free
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.400f * t_tac * SFX_HZ), 760.0f, 150.0f,
               0.34f, 0.30f);                            // fresh mag to the well
      sfx_bed(SFX_RELOAD_AR_TAC, (int)(0.260f * t_tac * SFX_HZ), 0.130f, 2600.0f, 0.14f, 7);
      sfx_bed(SFX_RELOAD_AR_TAC, (int)(0.450f * t_tac * SFX_HZ), 0.150f, 2300.0f, 0.16f, 5);
      // THE SEAT IS THE LAST EVENT HERE, so it is also the one `atk`/`eng` must land on
      // — the same contract the empty reload puts on its bolt, moved to whichever event
      // actually ends the mechanism.
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.880f * t_tac * SFX_HZ), 820.0f, 98.0f,
               1.55f, 0.22f);                            // THE SEAT
      // The last event of THIS reload, so it carries the bolt-class weight the
      // empty reload spends on its bolt: sub chunk plus snap, sized a notch
      // above the empty seat's.
      n = sfx_ring(0.150f, 63.0f, 96.0f, 0.034f, 0.38f);
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(0.880f * t_tac * SFX_HZ
                                          + 0.007f * SFX_HZ), 0.0f, 0);
      n = sfx_click(0.0025f, 0.00018f, 2100.0f, 0.48f);
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(0.880f * t_tac * SFX_HZ), 0.0f, 0);
      sfx_verb(SFX_RELOAD_AR_TAC, g_sfx_len[SFX_RELOAD_AR_TAC], 0.006f, 0.028f,
               0.15f, 3000.0f, 0.14f);
      sfx_finish(SFX_RELOAD_AR_TAC, 0.93f, 0.1050f, 0.30f);
    }
  // A ROOM ON THE RELOAD. The recorded reason for refusing one was computed against a
  // reload_ticks that has since shortened, so the sniper's last events now land well
  // INSIDE sfx_verb's 2.000 s snapshot cap. This was the only sound in the game with
  // no room at all — 100 ms after every transient the buffer was 46-51 dB down against
  // the AR report's 11.6. A reload is heard at 30 cm, so the room is SMALL and DARK,
  // at a wet level low enough that sfx_finish's gain stage barely notices it and `eng`
  // cannot migrate off the last event, which is this buffer's own contract.
  // RT60 0.15, NOT 0.42, AND THE NUMBER IS THE BUFFER'S OWN TAIL: a reverb tail that
  // runs off the end of its buffer is a step to zero, i.e. a click, exactly when the
  // ear is listening to the decay. At 0.42 s against the 269 ms of buffer left after
  // the last authored event, `T60` had quietly become the distance to the end of the
  // buffer — the milder form of the "T20 and T60 come back EQUAL" tell.
  sfx_verb(SFX_RELOAD_AR, g_sfx_len[SFX_RELOAD_AR], 0.006f, 0.028f, 0.15f,
           3000.0f, 0.14f);
  sfx_finish(SFX_RELOAD_AR, 0.88f, 0.1320f, 0.0f);

    // THE SNIPER CHANGES A MAGAZINE TOO. This used to be a five-round stripper load —
    // bolt lift, five rounds pressed in one at a time, bolt forward and down — and the
    // whole of it was audio for an animation that did not exist: the viewmodel just
    // rolled the rifle over for 1.8 seconds with nothing moving on it.
    float t_sr = (float)WPN_DEF[WPN_SR].reload_ticks / hz;
    sfx_alloc(SFX_RELOAD_SR, t_sr + 0.22f);
    sfx_mech(SFX_RELOAD_SR, (int)(0.050f * t_sr * SFX_HZ), 1620.0f, 210.0f,
             0.46f, 0.10f);                              // paddle release
    sfx_mech(SFX_RELOAD_SR, (int)(0.185f * t_sr * SFX_HZ), 840.0f, 160.0f,
             0.60f, 0.55f);                              // the box falls free
    sfx_mech(SFX_RELOAD_SR, (int)(0.380f * t_sr * SFX_HZ), 700.0f, 132.0f,
             0.36f, 0.30f);                              // fresh box to the well
    // THE SEAT. 92 Hz rather than the AR's 98: a ten-round .308 box is heavier
    // than a STANAG and it lands in a chassis rather than in an alloy lower.
    sfx_mech(SFX_RELOAD_SR, (int)(RELOAD_BEAT[WPN_SR][0] * t_sr * SFX_HZ), 760.0f, 92.0f,
             0.84f, 0.24f);
    // Sub under the seat, the AR's argument at the SR's own pitch: a ten-round
    // .308 box is the heavier magazine, so the ring sits at 58 Hz.
    n = sfx_ring(0.160f, 58.0f, 90.0f, 0.036f, 0.34f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][0] * t_sr * SFX_HZ
                                    + 0.007f * SFX_HZ), 0.0f, 0);
    // The hand leaving the grip for the knob, in the window where the animation is
    // travel rather than a struck part — the same hole the AR's sixth event fills, at
    // the same level relative to the seat.
    sfx_bed(SFX_RELOAD_SR, (int)(0.250f * t_sr * SFX_HZ), 0.190f, 2100.0f, 0.17f, 6);
    sfx_bed(SFX_RELOAD_SR, (int)(0.450f * t_sr * SFX_HZ), 0.150f, 1600.0f, 0.14f, 11);
    sfx_bed(SFX_RELOAD_SR, (int)(0.620f * t_sr * SFX_HZ), 0.130f, 2600.0f, 0.11f, 4);
    sfx_mech(SFX_RELOAD_SR, (int)(0.700f * t_sr * SFX_HZ), 600.0f, 150.0f,
             0.30f, 0.0f);                               // hand to the bolt
    sfx_mech(SFX_RELOAD_SR, (int)(0.790f * t_sr * SFX_HZ), 1150.0f, 176.0f,
             0.52f, 0.20f);                              // bolt lift + draw
    // BOLT DOWN AND LOCKED stays the loudest event in the buffer, because `eng`
    // — the energy peak — is the moment the sound says A LIVE WEAPON, and on
    // this weapon that moment is the bolt handle, not the magazine.
    sfx_mech(SFX_RELOAD_SR, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ), 1900.0f, 205.0f,
             1.78f, 0.15f);
    n = sfx_burst(0.10f, 2300.0f, 1.4f, 0.00004f, 0.013f, 1.4f, 0.62f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ), -0.08f, 2);
    // Bolt down and LOCKED gets the heaviest chunk in the buffer — camming a
    // .338 action shut is a bank-vault move, and `eng` lives here anyway.
    n = sfx_ring(0.145f, 108.0f, 168.0f, 0.032f, 0.44f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ
                                    + 0.006f * SFX_HZ), 0.0f, 0);
    n = sfx_burst(0.065f, 3400.0f, 1.6f, 0.00005f, 0.018f, 1.5f, 0.34f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ
                                    + 0.003f * SFX_HZ), -0.06f, 1);
    sfx_verb(SFX_RELOAD_SR, g_sfx_len[SFX_RELOAD_SR], 0.006f, 0.028f, 0.15f,
           3000.0f, 0.14f);
  sfx_finish(SFX_RELOAD_SR, 0.88f, 0.1210f, 0.0f);
  }
}

static void sfx_build_all(void) {
  sfx_build_shots();
  sfx_build_confirms();
  sfx_build_world();
  sfx_build_mech();
}

