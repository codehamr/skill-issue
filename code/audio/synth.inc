// =============================================================================
// audio/synth.inc — runtime procedural palette and startup source metadata.
// Proof: sfx DIR, audiocheck. Included once by game.c in definition order.
// Every sound is generated in C from a sound-ID seed, filters and envelopes.
// Recipes preserve the selected sound directions; no recordings or generated
// sample bank are build inputs. Playback only reads the resulting float buffers.
// =============================================================================
[[maybe_unused]] static const char *SFX_NAME[SFX_COUNT] = {  // harness WAV dump
  "fire_ar", "fire_sr", "far_ar", "far_sr", "hit", "hit_head",
  "kill", "kill_head", "hurt", "impact", "whizz", "step", "dryfire",
  "reload_ar", "reload_sr", "flesh", "reload_ar_tac",
  "thunder", "rain", "wind", "slide",
};

#define SFX_HZ 48000
#define SFX_CH 2            // every buffer is interleaved stereo

// Generated buffers live for the process lifetime.
static float *g_sfx_buf[SFX_COUNT];   // interleaved L,R
static int    g_sfx_len[SFX_COUNT];   // FRAMES, not samples
static uint64_t g_rng_sfx;

#define SFX_SCRATCH (SFX_HZ * 2)
static float g_scr[SFX_SCRATCH];      // one mono layer at a time

static float *sfx_alloc_frames(int id, int frames) {
  if ((unsigned)id >= SFX_COUNT || frames <= 0 || frames > 10 * SFX_HZ || g_sfx_buf[id])
    fatal("invalid procedural audio allocation");
  // Sound-ID streams isolate recipes from map seeds and catalogue build order.
  g_rng_sfx = 0x5F31Aull + 0x9E3779B97F4A7C15ull * (uint64_t)(id + 1);
  g_sfx_len[id] = frames;
  g_sfx_buf[id] = calloc((size_t)frames * SFX_CH, sizeof(float));
  if (!g_sfx_buf[id]) fatal("procedural audio out of memory");
  return g_sfx_buf[id];
}

static float *sfx_alloc(int id, float seconds) {
  return sfx_alloc_frames(id, (int)(seconds * SFX_HZ));
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
  // Eight source frames shape the initial edge before loudness measurement.
  // The first event remains at frame zero; internal mechanical attacks retain
  // their authored envelopes and shared animation timing.
  for (int f = 0; f < 8 && f * SFX_CH < n; f++) {
    float x = (float)f / 8.0f;
    float ramp = x * x * (3.0f - 2.0f * x);
    for (int c = 0; c < SFX_CH; c++) b[f * SFX_CH + c] *= ramp;
  }
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

// Dry hand/rail friction connects authored mechanical landmarks. The edges
// fade gently so movement cannot be mistaken for another seating impact.
static void sfx_bed(int id, int at, float dur, float hz, float amp, int width) {
  int n = (int)(dur * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  op_t low = {0}, high = {0}, motion = {0};
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float noise = rng_rangef(&g_rng_sfx, -1, 1);
    float grain = 0.75f + 0.25f * tanhf(op_lp(&motion, noise, 85.0f) * 9.0f);
    float edge = f_clamp((dur - t) / 0.025f, 0.0f, 1.0f);
    g_scr[i] = op_hp(&high, op_lp(&low, noise, hz * 1.4f), hz * 0.18f)
             * sfx_env(t, 0.015f, dur * 0.8f, 1.0f) * edge * edge * grain;
  }
  scr_norm(n, amp);
  sfx_lay(id, n, at, 0.0f, width);
}

// The transient, broad material knock and brief friction share one event
// onset. Small seeded material differences preserve every animation landmark;
// the final seat/bolt remains the loudest event of its reload.
static void sfx_mech(int id, int at, float tone_hz, float mass_hz, float amp,
                     float spring) {
  int n, d6 = (int)(0.006f * SFX_HZ);
  tone_hz *= rng_rangef(&g_rng_sfx, 0.96f, 1.04f);
  n = sfx_click(0.0025f, 0.00020f, tone_hz * 1.6f, 1.15f * amp);
  sfx_lay(id, n, at, 0.0f, 0);                              // contact
  n = sfx_burst(0.060f, tone_hz, 0.72f, 0.00010f, 0.0095f, 1.3f, 2.50f * amp);
  sfx_lay(id, n, at, 0.0f, 1);                              // material
  if (spring > 0.0f) {
    n = sfx_burst(0.070f, tone_hz * 1.35f, 0.8f, 0.001f, 0.019f, 1.3f, spring * amp);
    sfx_lay(id, n, at + (int)(0.003f * SFX_HZ), 0.0f, 2);    // detent/rail
  }
  float lk = f_clamp(150.0f / mass_hz, 1.0f, 1.60f);
  float low = 0.10f * lk * lk;
  n = sfx_ring(0.105f, mass_hz, mass_hz * 1.52f, 0.022f,
               low * amp);
  sfx_lay(id, n, at + d6, 0.0f, 0);                         // mass
  n = sfx_burst(0.080f, mass_hz * 1.35f, 0.70f, 0.0005f, 0.018f, 1.2f,
                0.72f * amp);
  sfx_lay(id, n, at + d6, 0.0f, 3);                         // the weight
  float meat = 0.95f * f_clamp(175.0f / mass_hz, 0.42f, 1.45f);
  n = sfx_burst(0.085f, mass_hz * 3.40f, 0.75f, 0.0004f, 0.021f, 1.15f,
                meat * amp);
  sfx_lay(id, n, at + (int)(0.008f * SFX_HZ), 0.0f, 2);     // 200-800 Hz
  sfx_bed(id, at + (int)(0.004f * SFX_HZ), 0.070f, tone_hz * 0.8f,
          amp * 0.24f, 3);
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
  sfx_verb(id, (int)(0.09f * SFX_HZ), 0.005f, 0.013f, 0.13f, 3400.0f, 0.08f);
}
static void sfx_room_kill(int id) {  // a real room: a kill has size a hit lacks
  sfx_verb(id, (int)(0.30f * SFX_HZ), 0.009f, 0.021f, 0.18f, 2200.0f, 0.12f);
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
// A short broadband edge, noisy body and restrained low pressure partial make
// one report. Dry shots contain no delayed report copies or fixed room reverb;
// world geometry supplies the live reflections and diffuse decay.
// ---------------------------------------------------------------------------
static void sfx_build_confirms(void) {

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

  sfx_alloc(SFX_KILL, 0.50f);
  sfx_kill_base(SFX_KILL, 1.00f);
  sfx_room_kill(SFX_KILL);
  // 0.3750, not 0.3470: sharing the hit's transient made the kill's own onset
  // brighter and its gain stage therefore quieter, and measured the 300 ms
  // ladder step from hit_head to kill had thinned to 0.22 dB. Re-derived rather
  // than left, because that ladder IS the consequence axis; the crest comes back
  // at 11.7 dB (the number this file records as a squashed hitmarker is 8.4) and
  // an isolated kill still renders at limiter 0.89 with clipped = 0.
  sfx_finish(SFX_KILL, 0.96f, 0.3750f, 0.30f);

  sfx_alloc(SFX_KILL_HEAD, 0.50f);
  sfx_kill_base(SFX_KILL_HEAD, SFX_HEAD_DUCK);
  sfx_head_sig(SFX_KILL_HEAD, 1.45f);
  sfx_room_kill(SFX_KILL_HEAD);
  sfx_finish(SFX_KILL_HEAD, 0.96f, 0.3120f, 0.30f);

}

// Broad pressure bands and irregular material clouds define the chosen weapon
// family. Every excitation comes from the sound-ID stream at startup.
static int sfx_weapon_noise(float sec, float lo, float hi, float atk,
                            float decay, float amp, float rough) {
  int n = (int)(sec * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  op_t low = {0}, upper = {0}, high = {0}, color = {0};
  double energy = 0;
  for (int i = -4096; i < n; i++) {
    float noise = rng_rangef(&g_rng_sfx, -1.0f, 1.0f);
    float x = op_hp(&high, op_lp(&upper, op_lp(&low, noise, hi), hi), lo);
    if (rough > 0.0f)
      x = op_lp(&color, tanhf(x * rough + 0.24f) - tanhf(0.24f), hi);
    if (i >= 0) { g_scr[i] = x; energy += (double)x * x; }
  }
  float gain = amp / ((float)sqrt(energy / n) + 1e-9f);
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    g_scr[i] *= gain * (1.0f - expf(-t / atk)) * expf(-t / decay);
  }
  return n;
}

static void sfx_weapon_front(int id, float width, float gain) {
  op_t high = {0}, low = {0};
  int n = (int)(0.070f * SFX_HZ);
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float x = (expf(-t / width) - 0.28f * expf(-t / (3.8f * width)))
            * (1.0f - expf(-t / 0.000025f));
    g_scr[i] = op_hp(&high, op_lp(&low, x, 9500.0f), 24.0f) * gain;
  }
  sfx_lay(id, n, 0, 0.0f, 0);
}

// Source level and runtime bus gain are separate. This stage sets the authored
// source energy, with a peak ceiling and short silent boundaries for cancellation.
static void sfx_weapon_finish(int id, float target, float window, float peak) {
  float *buf = g_sfx_buf[id];
  int n = g_sfx_len[id], count = n;
  if (window > 0.0f && window * SFX_HZ < n) count = (int)(window * SFX_HZ);
  double energy = 0;
  op_t dc[SFX_CH] = {0}, top[SFX_CH] = {0}, upper[SFX_CH] = {0};
  bool distant = id == SFX_FAR_AR || id == SFX_FAR_SR;
  for (int i = 0; i < n; i++) {
    float edge = f_clamp((float)i / 16.0f, 0.0f, 1.0f)
               * f_clamp((float)(n - 1 - i) / 480.0f, 0.0f, 1.0f);
    for (int c = 0; c < SFX_CH; c++) {
      int at = i * SFX_CH + c;
      float x = op_hp(&dc[c], buf[at], 24.0f);
      if (distant) x = op_lp(&upper[c], op_lp(&top[c], x, 2900.0f), 2900.0f);
      buf[at] = x * edge;
      if (i < count) energy += (double)buf[at] * buf[at];
    }
  }
  float gain = target / ((float)sqrt(energy / (count * SFX_CH)) + 1e-9f);
  float max_sample = 1e-9f;
  for (int i = 0; i < n * SFX_CH; i++) {
    buf[i] *= gain;
    max_sample = fmaxf(max_sample, fabsf(buf[i]));
  }
  if (max_sample > peak)
    for (int i = 0; i < n * SFX_CH; i++) buf[i] *= peak / max_sample;
}

static void sfx_build_shots(void) {
  // Kontur: compact fracture over the rolling Donnerkino body. Clouds overlap
  // continuously; none replays the muzzle attack as a second report.
  int id = SFX_FIRE_AR, n;
  sfx_alloc_frames(id, 33600);
  sfx_weapon_front(id, 0.0010f, 2.4f);
  n = sfx_weapon_noise(0.10f, 1800, 7800, 0.000035f, 0.0052f, 1.65f, 0);
  sfx_lay(id, n, 0, 0, 0);
  n = sfx_weapon_noise(0.34f, 160, 2200, 0.0006f, 0.056f, 1.45f, 3.2f);
  sfx_lay(id, n, 0, 0, 2);
  n = sfx_weapon_noise(0.45f, 48, 330, 0.0013f, 0.079f, 0.87f, 1.8f);
  sfx_lay(id, n, 0, 0, 0);
  n = sfx_weapon_noise(0.46f, 430, 3400, 0.011f, 0.073f, 0.42f, 0);
  sfx_lay(id, n, (int)(0.007f * SFX_HZ), 0, 3);
  n = sfx_weapon_noise(0.55f, 120, 1350, 0.011f, 0.126f, 0.46f, 2.0f);
  sfx_lay(id, n, (int)(0.018f * SFX_HZ), 0, 4);
  sfx_tail(id, (int)(0.028f * SFX_HZ), 0.60f, 490, 0.65f, 0.016f, 0.13f, 0.31f);
  sfx_verb(id, (int)(0.20f * SFX_HZ), 0.009f, 0.019f, 0.48f, 1900, 0.12f);
  sfx_weapon_finish(id, 0.1074f, 0.30f, 0.90f);

  // Klare Entladung: a clear broadband tear and sustained middle pressure,
  // with the low diffuse shape of the selected Tiefdruck/Raum ancestry.
  id = SFX_FIRE_SR;
  sfx_alloc_frames(id, 49440);
  sfx_weapon_front(id, 0.00207f, 2.8f);
  n = sfx_weapon_noise(0.12f, 1450, 7900, 0.000035f, 0.0082f, 2.20f, 0);
  sfx_lay(id, n, 0, 0, 0);
  n = sfx_weapon_noise(0.52f, 180, 1800, 0.0009f, 0.115f, 1.20f, 5.0f);
  sfx_lay(id, n, 0, 0, 1);
  n = sfx_weapon_noise(0.33f, 800, 3600, 0.0007f, 0.058f, 0.78f, 1.3f);
  sfx_lay(id, n, 0, 0, 0);
  n = sfx_weapon_noise(0.58f, 36, 260, 0.0020f, 0.126f, 0.77f, 2.0f);
  sfx_lay(id, n, (int)(0.003f * SFX_HZ), 0, 0);
  n = sfx_weapon_noise(0.48f, 150, 950, 0.018f, 0.077f, 0.38f, 1.8f);
  sfx_lay(id, n, (int)(0.027f * SFX_HZ), 0, 3);
  sfx_tail(id, (int)(0.038f * SFX_HZ), 0.90f, 710, 0.70f, 0.023f, 0.105f, 0.32f);
  sfx_verb(id, (int)(0.28f * SFX_HZ), 0.014f, 0.024f, 0.55f, 1450, 0.14f);
  sfx_weapon_finish(id, 0.0899f, 0.30f, 0.90f);

  // Offener Kern: distance softens the fracture, while an open 450–2600 Hz
  // pressure band keeps the automatic rhythm distinct through the mix.
  id = SFX_FAR_AR;
  sfx_alloc_frames(id, 26698);
  sfx_weapon_front(id, 0.0014f, 1.4f);
  n = sfx_weapon_noise(0.09f, 850, 4300, 0.0001f, 0.008f, 0.78f, 0);
  sfx_lay(id, n, 0, 0, 0);
  n = sfx_weapon_noise(0.44f, 180, 1550, 0.0009f, 0.095f, 1.12f, 4.5f);
  sfx_lay(id, n, 0, 0, 2);
  n = sfx_weapon_noise(0.48f, 47, 330, 0.0012f, 0.094f, 1.55f, 2.2f);
  sfx_lay(id, n, 0, 0, 1);
  sfx_tail(id, (int)(0.019f * SFX_HZ), 0.50f, 730, 0.70f, 0.018f, 0.090f, 1.60f);
  sfx_verb(id, (int)(0.23f * SFX_HZ), 0.012f, 0.025f, 0.46f, 1250, 0.20f);
  sfx_weapon_finish(id, 0.1422f, 0.30f, 0.88f);

  // Donnerkino: the distant sniper unfolds into several independently excited
  // low clouds and a diffuse, dark tail instead of a pitched descending sweep.
  id = SFX_FAR_SR;
  sfx_alloc_frames(id, 72182);
  sfx_weapon_front(id, 0.00163f, 2.4f);
  n = sfx_weapon_noise(0.15f, 650, 3000, 0.00012f, 0.0130f, 1.10f, 0);
  sfx_lay(id, n, 0, 0, 1);
  n = sfx_weapon_noise(0.55f, 180, 1250, 0.0006f, 0.0848f, 1.30f, 0);
  sfx_lay(id, n, 0, 0, 2);
  n = sfx_weapon_noise(0.72f, 29, 310, 0.0013f, 0.143f, 0.79f, 0);
  sfx_lay(id, n, 0, 0, 2);
  static const float cloud[][5] = {
    {0.0114f, 430, 2900, 0.119f, 0.504f},
    {0.0293f, 120, 1350, 0.205f, 0.552f},
    {0.0717f, 53, 720, 0.277f, 0.52f},
    {0.1483f, 75, 440, 0.334f, 0.40f},
  };
  for (int i = 0; i < 4; i++) {
    n = sfx_weapon_noise(1.34f, cloud[i][1], cloud[i][2], 0.018f,
                         cloud[i][3], cloud[i][4], 1.8f);
    sfx_lay(id, n, (int)(cloud[i][0] * SFX_HZ), 0, 4 + 3 * i);
  }
  sfx_tail(id, (int)(0.035f * SFX_HZ), 1.42f, 510, 0.70f, 0.045f, 0.36f, 0.95f);
  sfx_verb(id, (int)(0.45f * SFX_HZ), 0.021f, 0.036f, 1.05f, 1250, 0.38f);
  sfx_weapon_finish(id, 0.1453f, 0.30f, 0.88f);
}

// Existing synthesized contact material supplies the added weight. A short
// low-mid band and gentle saturation fill each part without introducing a ring.
static void sfx_mech_material(int id, const float *events, int count, float target) {
  float *buf = g_sfx_buf[id];
  int frames = g_sfx_len[id];
  op_t low[SFX_CH] = {0}, high[SFX_CH] = {0};
  for (int i = 0; i < frames; i++) {
    float t = (float)i / SFX_HZ, gate = 0;
    for (int e = 0; e < count; e++) {
      float age = t - events[e];
      if (age >= 0.0f) gate += (1.0f - expf(-age / 0.00012f)) * expf(-age / 0.024f);
    }
    for (int c = 0; c < SFX_CH; c++) {
      int at = i * SFX_CH + c;
      float source = buf[at];
      float body = op_hp(&high[c], op_lp(&low[c], source, 1450.0f), 85.0f);
      buf[at] = source + (tanhf(body * 7.0f + 0.24f) - tanhf(0.24f)) * gate * 0.23f;
    }
  }
  sfx_weapon_finish(id, target, 0, id == SFX_DRY ? 0.72f : 0.88f);
}

// Mechanical landmarks share the animation timing. The selected original
// sniper reload retains its complete procedural generator sequence.
static void sfx_build_mech(void) {
  int n;

  sfx_alloc(SFX_DRY, 0.12f);
  sfx_clack(SFX_DRY, 0, 2600.0f, 0.55f);
  n = sfx_burst(0.05f, 4200.0f, 1.6f, 0.00003f, 0.006f, 1.5f, 0.30f);
  sfx_lay(SFX_DRY, n, 0, 0.0f, 0);
  n = sfx_burst(0.09f, 175.0f, 1.0f, 0.00012f, 0.022f, 1.2f, 0.95f);
  sfx_lay(SFX_DRY, n, 1, 0.0f, 2);
  n = sfx_ring(0.08f, 330.0f, 520.0f, 0.016f, 0.45f);
  sfx_lay(SFX_DRY, n, 1, 0.0f, 1);
  sfx_finish(SFX_DRY, 0.62f, 0.0374f, 0.30f);

  {
    const float hz = (float)TICK_HZ;
    float t_ar = (float)wpn_reload_ticks(WPN_AR, 0) / hz;

    sfx_alloc(SFX_RELOAD_AR, t_ar + 0.22f);
    sfx_mech(SFX_RELOAD_AR, (int)(0.055f * t_ar * SFX_HZ), 1900.0f, 240.0f,
             0.42f, 0.10f);

    n = sfx_click(0.0030f, 0.00020f, 3300.0f, 0.30f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(0.055f * t_ar * SFX_HZ + 0.014f * SFX_HZ),
            0.0f, 0);
    sfx_mech(SFX_RELOAD_AR, (int)(0.175f * t_ar * SFX_HZ), 950.0f, 190.0f,
             0.54f, 0.55f);
    sfx_mech(SFX_RELOAD_AR, (int)(0.360f * t_ar * SFX_HZ), 760.0f, 150.0f,
             0.34f, 0.30f);

    sfx_mech(SFX_RELOAD_AR, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ), 820.0f, 98.0f,
             0.94f, 0.22f);

    n = sfx_ring(0.110f, 63.0f, 96.0f, 0.026f, 0.20f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ
                                    + 0.007f * SFX_HZ), 0.0f, 0);
    n = sfx_click(0.0025f, 0.00018f, 2100.0f, 0.36f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][0] * t_ar * SFX_HZ),
            0.0f, 0);

    sfx_mech(SFX_RELOAD_AR, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ), 2100.0f, 235.0f,
             2.12f, 0.14f);

    n = sfx_ring(0.135f, 116.0f, 176.0f, 0.030f, 0.52f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ
                                    + 0.006f * SFX_HZ), 0.0f, 0);
    n = sfx_burst(0.070f, 3600.0f, 1.6f, 0.00005f, 0.020f, 1.5f, 0.40f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ
                                    + 0.003f * SFX_HZ), 0.06f, 1);

    sfx_bed(SFX_RELOAD_AR, (int)(0.235f * t_ar * SFX_HZ), 0.130f, 2600.0f, 0.14f, 7);
    sfx_bed(SFX_RELOAD_AR, (int)(0.400f * t_ar * SFX_HZ), 0.150f, 2300.0f, 0.16f, 5);
    sfx_bed(SFX_RELOAD_AR, (int)(0.600f * t_ar * SFX_HZ), 0.140f, 1750.0f, 0.13f, 9);
    sfx_bed(SFX_RELOAD_AR, (int)(0.680f * t_ar * SFX_HZ), 0.110f, 2900.0f, 0.10f, 3);
    sfx_mech(SFX_RELOAD_AR, (int)(0.760f * t_ar * SFX_HZ), 620.0f, 165.0f,
             0.30f, 0.0f);
    n = sfx_burst(0.10f, 2600.0f, 1.4f, 0.00004f, 0.012f, 1.4f, 0.66f);
    sfx_lay(SFX_RELOAD_AR, n, (int)(RELOAD_BEAT[WPN_AR][1] * t_ar * SFX_HZ), 0.10f, 2);

    {
      float t_tac = (float)wpn_reload_ticks(WPN_AR, 1) / hz;
      sfx_alloc(SFX_RELOAD_AR_TAC, t_tac + 0.22f);
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.055f * t_tac * SFX_HZ), 1900.0f, 240.0f,
               0.42f, 0.10f);
      n = sfx_click(0.0030f, 0.00020f, 3300.0f, 0.30f);
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(0.055f * t_tac * SFX_HZ
                                          + 0.014f * SFX_HZ), 0.0f, 0);
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.185f * t_tac * SFX_HZ), 950.0f, 190.0f,
               0.54f, 0.55f);
      sfx_mech(SFX_RELOAD_AR_TAC, (int)(0.400f * t_tac * SFX_HZ), 760.0f, 150.0f,
               0.34f, 0.30f);
      sfx_bed(SFX_RELOAD_AR_TAC, (int)(0.260f * t_tac * SFX_HZ), 0.130f, 2600.0f, 0.14f, 7);
      sfx_bed(SFX_RELOAD_AR_TAC, (int)(0.450f * t_tac * SFX_HZ), 0.150f, 2300.0f, 0.16f, 5);

      sfx_mech(SFX_RELOAD_AR_TAC, (int)(RELOAD_SEAT_TAC * t_tac * SFX_HZ), 820.0f, 98.0f,
               1.55f, 0.22f);

      n = sfx_ring(0.150f, 63.0f, 96.0f, 0.034f, 0.38f);
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(RELOAD_SEAT_TAC * t_tac * SFX_HZ
                                          + 0.007f * SFX_HZ), 0.0f, 0);
      n = sfx_click(0.0025f, 0.00018f, 2100.0f, 0.48f);
      sfx_lay(SFX_RELOAD_AR_TAC, n, (int)(RELOAD_SEAT_TAC * t_tac * SFX_HZ), 0.0f, 0);
      sfx_verb(SFX_RELOAD_AR_TAC, g_sfx_len[SFX_RELOAD_AR_TAC], 0.006f, 0.028f,
               0.12f, 3000.0f, 0.06f);
      sfx_finish(SFX_RELOAD_AR_TAC, 0.93f, 0.1050f, 0.30f);
    }

  sfx_verb(SFX_RELOAD_AR, g_sfx_len[SFX_RELOAD_AR], 0.006f, 0.022f, 0.12f,
           3000.0f, 0.06f);
  sfx_finish(SFX_RELOAD_AR, 0.88f, 0.1320f, 0.0f);

    float t_sr = (float)wpn_reload_ticks(WPN_SR, 0) / hz;
    sfx_alloc(SFX_RELOAD_SR, t_sr + 0.22f);
    sfx_mech(SFX_RELOAD_SR, (int)(0.050f * t_sr * SFX_HZ), 1620.0f, 210.0f,
             0.46f, 0.10f);
    sfx_mech(SFX_RELOAD_SR, (int)(0.185f * t_sr * SFX_HZ), 840.0f, 160.0f,
             0.60f, 0.55f);
    sfx_mech(SFX_RELOAD_SR, (int)(0.380f * t_sr * SFX_HZ), 700.0f, 132.0f,
             0.36f, 0.30f);

    sfx_mech(SFX_RELOAD_SR, (int)(RELOAD_BEAT[WPN_SR][0] * t_sr * SFX_HZ), 760.0f, 92.0f,
             0.84f, 0.24f);

    n = sfx_ring(0.160f, 58.0f, 90.0f, 0.036f, 0.34f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][0] * t_sr * SFX_HZ
                                    + 0.007f * SFX_HZ), 0.0f, 0);

    sfx_bed(SFX_RELOAD_SR, (int)(0.250f * t_sr * SFX_HZ), 0.190f, 2100.0f, 0.17f, 6);
    sfx_bed(SFX_RELOAD_SR, (int)(0.450f * t_sr * SFX_HZ), 0.150f, 1600.0f, 0.14f, 11);
    sfx_bed(SFX_RELOAD_SR, (int)(0.620f * t_sr * SFX_HZ), 0.130f, 2600.0f, 0.11f, 4);
    sfx_mech(SFX_RELOAD_SR, (int)(0.700f * t_sr * SFX_HZ), 600.0f, 150.0f,
             0.30f, 0.0f);
    sfx_mech(SFX_RELOAD_SR, (int)(0.790f * t_sr * SFX_HZ), 1150.0f, 176.0f,
             0.52f, 0.20f);

    sfx_mech(SFX_RELOAD_SR, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ), 1900.0f, 205.0f,
             1.78f, 0.15f);
    n = sfx_burst(0.10f, 2300.0f, 1.4f, 0.00004f, 0.013f, 1.4f, 0.62f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ), -0.08f, 2);

    n = sfx_ring(0.145f, 108.0f, 168.0f, 0.032f, 0.44f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ
                                    + 0.006f * SFX_HZ), 0.0f, 0);
    n = sfx_burst(0.065f, 3400.0f, 1.6f, 0.00005f, 0.018f, 1.5f, 0.34f);
    sfx_lay(SFX_RELOAD_SR, n, (int)(RELOAD_BEAT[WPN_SR][1] * t_sr * SFX_HZ
                                    + 0.003f * SFX_HZ), -0.06f, 1);
    sfx_verb(SFX_RELOAD_SR, g_sfx_len[SFX_RELOAD_SR], 0.006f, 0.022f, 0.12f,
           3000.0f, 0.06f);
  sfx_finish(SFX_RELOAD_SR, 0.88f, 0.1210f, 0.0f);
  }

  float t_ar = (float)wpn_reload_ticks(WPN_AR, 0) / TICK_HZ;
  float t_tac = (float)wpn_reload_ticks(WPN_AR, 1) / TICK_HZ;
  const float ar_events[] = {0.055f * t_ar, 0.175f * t_ar, 0.360f * t_ar,
    RELOAD_BEAT[WPN_AR][0] * t_ar, 0.760f * t_ar, RELOAD_BEAT[WPN_AR][1] * t_ar};
  const float tac_events[] = {0.055f * t_tac, 0.185f * t_tac,
    0.400f * t_tac, RELOAD_SEAT_TAC * t_tac};
  const float hammer[] = {0.0f};
  sfx_mech_material(SFX_RELOAD_AR, ar_events, 6, 0.0592f);
  sfx_mech_material(SFX_RELOAD_AR_TAC, tac_events, 4, 0.0980f);
  sfx_mech_material(SFX_DRY, hammer, 1, 0.1380f);
}

// Broad stochastic contact: two low-pass poles round the edge without a
// resonant pitch. Each layer consumes only its sound's private noise stream.
static int sfx_contact(float seconds, float lo, float hi, float attack,
                       float decay, float amp) {
  int n = (int)(seconds * SFX_HZ);
  if (n > SFX_SCRATCH) n = SFX_SCRATCH;
  op_t high = {0}, low = {0}, round = {0};
  for (int i = 0; i < n; i++) {
    float t = (float)i / SFX_HZ;
    float x = op_hp(&high, rng_rangef(&g_rng_sfx, -1, 1), lo);
    x = op_lp(&round, op_lp(&low, x, hi), hi);
    g_scr[i] = x * sfx_env(t, attack, decay, 1.0f);
  }
  return scr_norm(n, amp);
}

// Source level is independent of its route in the live mix. The eight-frame
// entrance and ten-millisecond exit keep direct and repeated contacts quiet
// at their buffer boundaries; only a linear safety gain limits the peak.
static void sfx_world_finish(int id, float target, float peak, float window) {
  float *dst = g_sfx_buf[id];
  int frames = g_sfx_len[id];
  int count = window > 0 ? (int)(window * SFX_HZ) : frames;
  if (count > frames) count = frames;
  int silence = id == SFX_IMPACT || id == SFX_FLESH || id == SFX_HURT
             || id == SFX_WHIZZ || id == SFX_STEP ? 1024 : 0;
  op_t dc[2] = {{0}};
  double sum = 0;
  float largest = 0;
  for (int i = 0; i < frames; i++) {
    float entry = f_clamp((float)i / 8.0f, 0, 1);
    float exit = f_clamp((float)(frames - silence - 1 - i) / 480.0f, 0, 1);
    for (int c = 0; c < SFX_CH; c++) {
      float x = op_hp(&dc[c], dst[i * SFX_CH + c], 22.0f) * entry * exit;
      dst[i * SFX_CH + c] = x;
      if (i < count) sum += (double)x * (double)x;
      largest = fmaxf(largest, fabsf(x));
    }
  }
  float gain = target / ((float)sqrt(sum / (count * SFX_CH)) + 1e-9f);
  gain = fminf(gain, peak / (largest + 1e-9f));
  for (int i = 0; i < frames * SFX_CH; i++) dst[i] *= gain;
}

static void sfx_build_world(void) {
  int n;

  // Stone fracture, a broad compressed body, then scattered material settling.
  sfx_alloc_frames(SFX_IMPACT, 16320);
  n = sfx_contact(.030f, 900, 4300, .00025f, .004f, .54f);
  sfx_lay(SFX_IMPACT, n, 0, 0, 1);
  n = sfx_contact(.19f, 65, 770, .0007f, .036f, .75f);
  sfx_lay(SFX_IMPACT, n, 144, 0, 2);
  n = sfx_contact(.13f, 160, 1400, .001f, .023f, .29f);
  sfx_lay(SFX_IMPACT, n, 528, 0, 1);
  for (int k = 0; k < 18; k++) {
    int at = (int)(rng_rangef(&g_rng_sfx, .018f, .11f) * SFX_HZ);
    float gain = rng_rangef(&g_rng_sfx, .025f, .070f);
    float pan = rng_rangef(&g_rng_sfx, -.24f, .24f);
    n = sfx_contact(.027f, 650, 3300, .0015f, .006f, gain);
    sfx_lay(SFX_IMPACT, n, at, pan, 3);
  }
  sfx_world_finish(SFX_IMPACT, .101f, .88f, .30f);

  // A rounded cloth/slap edge and dense low-middle compression distinguish
  // a body contact from the short, brighter screen-space confirmation.
  sfx_alloc_frames(SFX_FLESH, 13440);
  n = sfx_contact(.043f, 750, 3500, .0002f, .006f, .42f);
  sfx_lay(SFX_FLESH, n, 0, 0, 0);
  n = sfx_contact(.16f, 140, 1200, .0007f, .045f, .86f);
  sfx_lay(SFX_FLESH, n, 48, 0, 0);
  n = sfx_contact(.19f, 80, 430, .003f, .046f, .34f);
  sfx_lay(SFX_FLESH, n, 240, 0, 0);
  n = sfx_contact(.10f, 270, 1900, .009f, .024f, .18f);
  sfx_lay(SFX_FLESH, n, 960, 0, 0);
  sfx_world_finish(SFX_FLESH, .081f, .88f, .30f);

  // Close incoming damage: a short dull contact followed by compressed cloth
  // and body pressure. No confirmation pitch or additional room signature.
  sfx_alloc_frames(SFX_HURT, 18720);
  n = sfx_contact(.032f, 500, 2800, .0006f, .007f, .44f);
  sfx_lay(SFX_HURT, n, 0, 0, 0);
  n = sfx_contact(.23f, 65, 750, .0015f, .047f, .94f);
  sfx_lay(SFX_HURT, n, 192, 0, 0);
  n = sfx_contact(.15f, 320, 1600, .002f, .027f, .38f);
  sfx_lay(SFX_HURT, n, 528, 0, 0);
  n = sfx_contact(.25f, 75, 310, .008f, .052f, .28f);
  sfx_lay(SFX_HURT, n, 960, 0, 0);
  sfx_world_finish(SFX_HURT, .099f, .88f, .30f);

  // The selected passage has a short approach and its hard shock at 48 ms.
  // Dense middle-frequency pressure follows the crack; the wake clears fast.
  sfx_alloc_frames(SFX_WHIZZ, 12000);
  n = sfx_contact(.048f, 850, 3300, .035f, .09f, .028f);
  sfx_lay(SFX_WHIZZ, n, 0, 0, 0);
  n = sfx_contact(.022f, 1150, 6500, .00008f, .0021f, 1.0f);
  sfx_lay(SFX_WHIZZ, n, 2304, 0, 0);
  n = sfx_contact(.026f, 550, 3300, .0004f, .0047f, .75f);
  sfx_lay(SFX_WHIZZ, n, 2352, 0, 0);
  n = sfx_contact(.060f, 230, 1500, .001f, .010f, .30f);
  sfx_lay(SFX_WHIZZ, n, 2496, 0, 0);
  n = sfx_contact(.12f, 650, 2700, .003f, .016f, .11f);
  sfx_lay(SFX_WHIZZ, n, 2688, 0, 0);
  sfx_world_finish(SFX_WHIZZ, .057f, .89f, .30f);

  // Soft heel compression and a separate forefoot load make the two contacts
  // readable. The late rub is short and quiet, without a bright click layer.
  sfx_alloc_frames(SFX_STEP, 20160);
  n = sfx_contact(.11f, 170, 1350, .0014f, .018f, .49f);
  sfx_lay(SFX_STEP, n, 0, 0, 2);
  n = sfx_contact(.18f, 65, 740, .003f, .029f, .66f);
  sfx_lay(SFX_STEP, n, 240, 0, 2);
  n = sfx_contact(.12f, 140, 1050, .003f, .036f, .90f);
  sfx_lay(SFX_STEP, n, 1296, 0, 2);
  n = sfx_contact(.14f, 300, 1900, .012f, .030f, .095f);
  sfx_lay(SFX_STEP, n, 2352, 0, 3);
  sfx_world_finish(SFX_STEP, .117f, .82f, .30f);

  // Loaded cloth/sole friction, with a shared random motion envelope and a
  // small independent side field. The mixer owns the 250 ms segment overlap.
  float *slide = sfx_alloc_frames(SFX_SLIDE, 60000);
  op_t low = {0}, high = {0}, bass = {0}, motion = {0};
  op_t sides[2] = {{0}};
  for (int i = 0; i < g_sfx_len[SFX_SLIDE]; i++) {
    float t = (float)i / SFX_HZ;
    float end = (float)(g_sfx_len[SFX_SLIDE] - 1 - i) / SFX_HZ;
    float x = rng_rangef(&g_rng_sfx, -1, 1);
    float load = .78f + .22f * tanhf(op_lp(&motion, x, 17.0f) * 13.0f);
    float body = op_hp(&high, op_lp(&low, x, 1450.0f), 65.0f);
    float weight = op_lp(&bass, x, 340.0f);
    float envelope = f_clamp(t / .025f, 0, 1) * f_clamp(end / .025f, 0, 1);
    for (int c = 0; c < SFX_CH; c++) {
      float side = op_lp(&sides[c], rng_rangef(&g_rng_sfx, -1, 1), 1900.0f);
      slide[i * SFX_CH + c] = (body + .60f * weight + .08f * side) * load * envelope;
    }
  }
  sfx_world_finish(SFX_SLIDE, .18f, .84f, .30f);
}

static void sfx_build_weather(void) {
  // Independent, overlapping broadband pressure cells yield irregular thunder
  // rolls. Their low/mid/high envelopes decay separately without tonal modes.
  float *thunder = sfx_alloc_frames(SFX_THUNDER, 384000);
  static const float cells[][3] = {
    {0, 2.05f, .58f}, {.58f, 2.60f, .48f}, {1.70f, 2.65f, .38f},
    {3.16f, 2.75f, .27f}, {4.78f, 2.60f, .15f},
  };
  for (int k = 0; k < 5; k++) {
    op_t low[2] = {{0}}, high[2] = {{0}}, middle[2] = {{0}}, contour[2] = {{0}};
    op_t movement = {0};
    int offset = (int)(cells[k][0] * SFX_HZ);
    int frames = (int)(cells[k][1] * SFX_HZ);
    for (int i = 0; i < frames && offset + i < g_sfx_len[SFX_THUNDER]; i++) {
      float t = (float)i / SFX_HZ;
      float left = (float)(frames - 1 - i) / SFX_HZ;
      float shared = rng_rangef(&g_rng_sfx, -1, 1);
      float motion = .70f + .30f * tanhf(op_lp(&movement, shared, 7.0f) * 23.0f);
      float envelope = sfx_env(t, k == 0 ? .008f : .10f, cells[k][1] * .36f, 1.0f);
      envelope *= f_clamp(left / .30f, 0, 1) * motion * cells[k][2];
      for (int c = 0; c < SFX_CH; c++) {
        float x = .70f * shared + .30f * rng_rangef(&g_rng_sfx, -1, 1);
        float bottom = op_hp(&high[c], op_lp(&low[c], x, 115.0f), 28.0f);
        float mid = op_lp(&middle[c], x, 360.0f) - op_lp(&contour[c], x, 90.0f);
        thunder[(offset + i) * SFX_CH + c] += (bottom * 5.0f + mid * 3.0f) * envelope;
      }
    }
  }
  for (int k = 0; k < 3; k++) {
    const int starts[] = {0, 864, 2640};
    int n = sfx_contact(.31f, 240, k == 0 ? 4200 : 2350,
                        .00035f + .0003f * k, .028f + .012f * k,
                        .23f * expf(-.48f * k));
    sfx_lay(SFX_THUNDER, n, starts[k], 0, 3 + k * 2);
  }
  // Broad pressure compression adds correlated texture inside the roll.
  for (int i = 0; i < g_sfx_len[SFX_THUNDER] * SFX_CH; i++)
    thunder[i] = tanhf(thunder[i] * 5.0f) * .2f;
  sfx_world_finish(SFX_THUNDER, .25f, .91f, .30f);

  // Rain remains a continuous sheet with dense stochastic surface contacts.
  // No discrete plinks or periodic modulation compete with gameplay cues.
  float *rain = sfx_alloc_frames(SFX_RAIN, 288000);
  op_t rain_low[2] = {{0}}, rain_high[2] = {{0}}, rain_body[2] = {{0}};
  op_t rain_motion[2] = {{0}};
  for (int i = 0; i < g_sfx_len[SFX_RAIN]; i++) {
    float t = (float)i / SFX_HZ;
    float left = (float)(g_sfx_len[SFX_RAIN] - 1 - i) / SFX_HZ;
    float envelope = f_clamp(t / .04f, 0, 1) * f_clamp(left / .04f, 0, 1);
    for (int c = 0; c < SFX_CH; c++) {
      float x = rng_rangef(&g_rng_sfx, -1, 1);
      float sheet = op_hp(&rain_high[c], op_lp(&rain_low[c], x, 5600.0f), 600.0f);
      float surface = op_lp(&rain_body[c], x, 1900.0f);
      float density = .94f + .06f * tanhf(op_lp(&rain_motion[c], x, 22.0f) * 12.0f);
      rain[i * SFX_CH + c] = (sheet + .22f * surface) * density * envelope;
    }
  }
  sfx_world_finish(SFX_RAIN, .15f, .76f, 0);

  // A shared slow gust carries open low-middle air while independently moving
  // side turbulence supplies space. It contains no whistling oscillator.
  float *wind = sfx_alloc_frames(SFX_WIND, 288000);
  op_t wind_low[2] = {{0}}, wind_high[2] = {{0}}, wind_air[2] = {{0}}, wind_mid[2] = {{0}};
  op_t wind_motion = {0};
  for (int i = 0; i < g_sfx_len[SFX_WIND]; i++) {
    float t = (float)i / SFX_HZ;
    float left = (float)(g_sfx_len[SFX_WIND] - 1 - i) / SFX_HZ;
    float shared = rng_rangef(&g_rng_sfx, -1, 1);
    float gust = .82f + .18f * tanhf(op_lp(&wind_motion, shared, .75f) * 80.0f);
    float envelope = f_clamp(t / .06f, 0, 1) * f_clamp(left / .06f, 0, 1);
    for (int c = 0; c < SFX_CH; c++) {
      float x = .60f * shared + .40f * rng_rangef(&g_rng_sfx, -1, 1);
      float body = op_hp(&wind_high[c], op_lp(&wind_low[c], x, 750.0f), 70.0f);
      float air = op_lp(&wind_air[c], x, 2450.0f) - op_lp(&wind_mid[c], x, 550.0f);
      wind[i * SFX_CH + c] = (body + .37f * air) * gust * envelope;
    }
  }
  sfx_world_finish(SFX_WIND, .11f, .76f, 0);
}

// Canonical sources and conservative remaining-peak bounds are captured once
// after synthesis. Harness replacement buffers use their own full extent.
static float *g_sfx_source[SFX_COUNT];
static int g_sfx_source_len[SFX_COUNT], g_sfx_play_len[SFX_COUNT];
static uint32_t g_sfx_source_hash[SFX_COUNT];
#define SFX_PEAK_BLOCK (SFX_HZ / 100)
static float *g_sfx_peak_storage, *g_sfx_peak[SFX_COUNT];

// Linear staging preserves the chosen timbre and envelopes. Weather/friction
// are aligned with the existing quiet world bed; reports retain transient
// headroom and the established hit/kill cues keep their mix hierarchy.
// Audition comparison gains are not a cross-family game mix.
static const float SFX_GAME_GAIN[SFX_COUNT] = {
  [SFX_FIRE_AR] = 1.15f, [SFX_FIRE_SR] = 1.20f,
  [SFX_FAR_AR] = .81f, [SFX_FAR_SR] = .84f,
  [SFX_HIT] = 1, [SFX_HIT_HEAD] = 1, [SFX_KILL] = 1, [SFX_KILL_HEAD] = 1,
  [SFX_HURT] = 1, [SFX_IMPACT] = 1, [SFX_WHIZZ] = 1.05f,
  [SFX_STEP] = .44f, [SFX_DRY] = .63f,
  [SFX_RELOAD_AR] = 1.15f, [SFX_RELOAD_SR] = .95f,
  [SFX_FLESH] = .84f, [SFX_RELOAD_AR_TAC] = 1.01f,
  [SFX_THUNDER] = .72f, [SFX_RAIN] = .223f, [SFX_WIND] = .20f,
  [SFX_SLIDE] = .258f,
};

// Repeating beds overlap at most two source windows. Their equal-power
// playback envelopes are owned by the mixer, leaving generated buffers unchanged.
static int sfx_loop_hop_frames(int id) {
  if (id == SFX_RAIN || id == SFX_WIND) return 5 * SFX_HZ;
  if (id == SFX_SLIDE) return SFX_HZ;
  return 0;
}
static int sfx_loop_fade_frames(int id) {
  if (id == SFX_RAIN || id == SFX_WIND) return SFX_HZ;
  if (id == SFX_SLIDE) return SFX_HZ / 4;
  return 0;
}

// Only exact zero suffixes can release a voice early. Full generated lengths
// remain authoritative for cadence and admission priority.
static int sfx_play_frames(int id) {
  if (g_sfx_buf[id] != g_sfx_source[id] || g_sfx_len[id] != g_sfx_source_len[id])
    return g_sfx_len[id];
  return g_sfx_play_len[id];
}

// The entire remaining source contributes to this conservative bound. A quiet
// reload gap cannot hide its future bolt. Lookups occur only under pool pressure.
static float sfx_remaining_peak(int id, const float *buf, int frames, float pos) {
  if ((unsigned)id >= SFX_COUNT || !g_sfx_peak[id] ||
      buf != g_sfx_source[id] || frames != g_sfx_source_len[id]) return 1;
  int block = (int)pos / SFX_PEAK_BLOCK;
  if (block < 0) block = 0;
  if (block >= (frames + SFX_PEAK_BLOCK - 1) / SFX_PEAK_BLOCK) return 0;
  return g_sfx_peak[id][block];
}

static void sfx_build_all(void) {
  if (g_sfx_peak_storage) return;
  sfx_build_shots();
  sfx_build_confirms();
  sfx_build_world();
  sfx_build_mech();
  sfx_build_weather();
  size_t peak_count = 0;
  for (int id = 0; id < SFX_COUNT; id++) {
    if (!g_sfx_buf[id] || g_sfx_len[id] <= 0 || g_sfx_len[id] > 10 * SFX_HZ)
      fatal("missing procedural audio source");
    peak_count += (size_t)(g_sfx_len[id] + SFX_PEAK_BLOCK - 1) / SFX_PEAK_BLOCK;
  }
  g_sfx_peak_storage = malloc(peak_count * sizeof *g_sfx_peak_storage);
  if (!g_sfx_peak_storage) fatal("audio peak metadata out of memory");
  float *next_peak = g_sfx_peak_storage;
  for (int id = 0; id < SFX_COUNT; id++) {
    g_sfx_source[id] = g_sfx_buf[id];
    g_sfx_source_len[id] = g_sfx_len[id];
    uint32_t hash = 2166136261u;
    for (int i = 0; i < g_sfx_len[id] * SFX_CH; i++) {
      if (f_is_nonfinite(g_sfx_buf[id][i])) fatal("nonfinite procedural audio source");
      uint32_t bits;
      memcpy(&bits, &g_sfx_buf[id][i], sizeof bits);
      for (int byte = 0; byte < 4; byte++) {
        hash ^= (bits >> (byte * 8)) & 255u;
        hash *= 16777619u;
      }
    }
    g_sfx_source_hash[id] = hash;
    int end = g_sfx_len[id];
    if (!sfx_loop_hop_frames(id)) {
      while (end > 0 && g_sfx_buf[id][(end - 1) * SFX_CH] == 0 &&
             g_sfx_buf[id][(end - 1) * SFX_CH + 1] == 0) end--;
      // Retain a zero frame after the last nonzero pair for interpolation.
      if (end < g_sfx_len[id]) end++;
    }
    g_sfx_play_len[id] = end;
    int blocks = (g_sfx_len[id] + SFX_PEAK_BLOCK - 1) / SFX_PEAK_BLOCK;
    size_t used_peaks = (size_t)(next_peak - g_sfx_peak_storage);
    if (used_peaks > peak_count || (size_t)blocks > peak_count - used_peaks)
      fatal("invalid audio peak metadata extent");
    g_sfx_peak[id] = next_peak; next_peak += blocks;
    float peak = 0;
    for (int block = blocks - 1; block >= 0; block--) {
      int hi = (block + 1) * SFX_PEAK_BLOCK;
      if (hi > g_sfx_len[id]) hi = g_sfx_len[id];
      for (int frame = hi - 1; frame >= block * SFX_PEAK_BLOCK; frame--)
        for (int channel = 0; channel < SFX_CH; channel++)
          peak = fmaxf(peak, fabsf(g_sfx_buf[id][frame * SFX_CH + channel]));
      g_sfx_peak[id][block] = peak;
    }
  }
}
