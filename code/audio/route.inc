// =============================================================================
// audio/route.inc — event routing, protected admission and mixer output.
// Proof: sfxlog, mixdown, spstart, audiocheck, acousticcheck, filmcueproof, feedbackproof.
// One ordered translation unit; audiotrace is opt-in and harness-only.
// Contract: one producer publishes two bounded SPSC lanes, session epochs and
// per-sound/entity cancellation generations. Only the mixer consumes commands,
// owns voices/envelopes and clears timeline state. Volume and pause are atomic.
// =============================================================================
// ---------------------------------------------------------------------------
// Event routing is shared presentation logic. Native backends and headless
// captures feed the same mixer. sfxlog observes offered routing decisions;
// audiotrace separately records admission and actual source/dry sample onsets.
// Acoustic room state is published atomically; only the mixer evolves wet tails.
// A voice carries its own L/R gains and a start DELAY. Both matter: panning is how a
// shot from behind-left becomes information rather than noise, and the delay is how the
// hit confirmation gets heard at all — see audio_on_event.
typedef struct {
  int active; const float *buf; int len;
  float pos, rate, gl, gr; int delay;
  int prio;                  // SFX_P_* — who ducks whom
  int dying;                 // fading out after a kill trigger — see audio_kill
  uint64_t trace;             // optional harness census identity
  int id; uint32_t cancel;   // immutable sound and cancellation generation
  int tag;                   // 1 = the player's OWN voice: a kill trigger only
                             // fades matching tags, so cancelling your reload
                             // cannot silence a bot's on the same buffer
} voice_t;

// Three priorities, and the middle one exists because the duck below would
// otherwise take 6.7 dB off the two cues a player cannot afford to miss.
//   LEAD   local hit/kill confirmation. Ducks the ordinary bus and is not
//          stolen; bounded protected admission and pool pressure are counted.
//   KEEP   taking a round, and a round cracking past your head. Neither ducks
//          nor is ducked: this is the audio that tells you you are being shot
//          and from where, and trading it for your own hitmarker is a bad trade
//          however satisfying the hitmarker is. They barely overlap in spectrum
//          anyway — `hurt` is 72% under 120 Hz, a confirmation's fat is 200-800.
//   NORM   everything else.
// Derived from the sound id rather than passed in, so it cannot be forgotten at
// a call site and cannot be handed to something that is not a confirmation —
// EV_END plays the headshot-kill buffer and legitimately gets LEAD, because the
// end of a match is the one other moment that owns the mix.
// How many voices of ONE sound may be live at once. 0 = uncapped. Derived from
// the id for the same reason sfx_prio is: a call site cannot forget it and
// cannot hand it to something it does not belong on.
static int sfx_class_cap(int id) {
  // 52 and 12, measured rather than guessed.
  if (id == SFX_FAR_AR || id == SFX_FAR_SR) return 52;
  if (id == SFX_STEP) return 12;
  if (id == SFX_THUNDER || id == SFX_RAIN || id == SFX_WIND) return 2;
  return 0;
}

enum { SFX_P_NORM, SFX_P_KEEP, SFX_P_LEAD };
static int sfx_prio(int id) {
  if (id == SFX_HIT || id == SFX_HIT_HEAD || id == SFX_KILL
      || id == SFX_KILL_HEAD) return SFX_P_LEAD;
  if (id == SFX_HURT || id == SFX_WHIZZ) return SFX_P_KEEP;
  return SFX_P_NORM;
}

// The pool is a CEILING, not a target: sfx_prio's classes decide what goes when the
// arena is loud, so this only has to be past the worst case (measured 94 at 20 bots
// hard). Admission and traversal both retain this fixed ceiling.
#define MAX_VOICES 96
static voice_t g_voices[MAX_VOICES];

// A decibel UNDER the headroom the bus has, deliberately. One centred sound at routing
// gain 1.0 puts the master bus at 0.68 against a 0.92 limiter threshold, so +2.6 dB is
// the ceiling — but a confirmation arrives on top of the shot that caused it, and a
// lead voice exactly at the ceiling made the SUM clip. +1.6 dB leaves the ducked
// remainder room underneath. Buying the last decibel with level is a bad trade anyway:
// a confirmation that pulls the master gain down drags its own tail with it. The rest
// of the contrast comes from the sidechain duck in audio_mix, which costs nothing.
#define SFX_LEAD_GAIN 1.20f

// Two SPSC lanes keep ordinary pressure out of the protected cue reserve.
// Cancellation is a separate atomic generation; neither full lane can lose it.
typedef struct { int id; float gain, rate, pan; int delay, tag; uint32_t epoch, cancel; uint64_t trace; } sfx_trig_t;
#define TRIG_RING 64
#define AUDIO_TAGS (MAX_ENTS + 2)
static sfx_trig_t g_trig[TRIG_RING], g_protected_trig[TRIG_RING];
static _Atomic uint32_t g_trig_w, g_trig_r, g_protected_w, g_protected_r;
static _Atomic uint32_t g_audio_cancel[SFX_COUNT][AUDIO_TAGS];
typedef struct { uint32_t normal, protected; } audio_snapshot_t;
static audio_snapshot_t audio_mix_snapshot(void) {
  return (audio_snapshot_t){
    atomic_load_explicit(&g_trig_w, memory_order_acquire),
    atomic_load_explicit(&g_protected_w, memory_order_acquire)};
}
// One producer and one acquired coefficient word. No callback world reads.
enum { ACOUSTIC_OPEN, ACOUSTIC_EDGE, ACOUSTIC_NARROW, ACOUSTIC_SHELTER };
#define ACOUSTIC_PROBES 9
#define ACOUSTIC_INTERVAL 24
#define ACOUSTIC_RANGE 12.0f
#define ACOUSTIC_ROOF_RANGE 8.0f
#define ACOUSTIC_EARLY_MAX 0.30f
#define ACOUSTIC_LATE_MAX 0.22f
#define ACOUSTIC_FEEDBACK_MAX 0.65f
typedef struct {
  float delay[2], early[2], late, feedback, damping;
  int kind;
} acoustic_params_t;
static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2, "lock-free coefficient publication");
static _Atomic uint64_t g_acoustic_word;
static long g_acoustic_next = -1, g_acoustic_updates, g_acoustic_queries;
static int g_acoustic_last_queries;
static long g_acoustic_whizz_rays, g_acoustic_whizz_solids;
static void audio_acoustic_invalidate(void) {
  g_acoustic_next = -1;
  atomic_store_explicit(&g_acoustic_word, 0, memory_order_release);
}
static unsigned acoustic_byte(float value, float low, float high) {
  if (f_is_nonfinite(value)) value = low;
  return (unsigned)(f_clamp((value - low) / (high - low), 0, 1) * 255 + .5f);
}
static uint64_t acoustic_pack(acoustic_params_t p) {
  unsigned bytes[8] = {
    acoustic_byte(p.delay[0], .002f, .070f), acoustic_byte(p.delay[1], .002f, .070f),
    acoustic_byte(p.early[0], 0, ACOUSTIC_EARLY_MAX),
    acoustic_byte(p.early[1], 0, ACOUSTIC_EARLY_MAX),
    acoustic_byte(p.late, 0, ACOUSTIC_LATE_MAX),
    acoustic_byte(p.feedback, 0, ACOUSTIC_FEEDBACK_MAX),
    acoustic_byte(p.damping, 1500, 9000),
    0x80u | ((unsigned)p.kind & 3u)};
  uint64_t word = 0;
  for (int i = 0; i < 8; i++) word |= (uint64_t)bytes[i] << (i * 8);
  return word;
}
static void audio_acoustic_publish(acoustic_params_t p) {
  atomic_store_explicit(&g_acoustic_word, acoustic_pack(p), memory_order_release);
}
static acoustic_params_t acoustic_unpack(uint64_t word) {
  acoustic_params_t p = {.delay = {.020f, .020f}, .damping = 7000};
  if ((word >> 56) < 0x80 || (word >> 56) > 0x83) return p;
  float b[7];
  for (int i = 0; i < 7; i++) b[i] = (float)((word >> (i * 8)) & 255) / 255.0f;
  p.delay[0] = .002f + .068f * b[0]; p.delay[1] = .002f + .068f * b[1];
  p.early[0] = ACOUSTIC_EARLY_MAX * b[2]; p.early[1] = ACOUSTIC_EARLY_MAX * b[3];
  p.late = ACOUSTIC_LATE_MAX * b[4]; p.feedback = ACOUSTIC_FEEDBACK_MAX * b[5];
  p.damping = 1500 + 7500 * b[6]; p.kind = (int)(word >> 56) & 3;
  return p;
}
// Horizontal axes are world-fixed; turning only changes reflection placement.
static const v3 ACOUSTIC_DIR[ACOUSTIC_PROBES] = {
  {{1,0,0}}, {{.707106781f,0,.707106781f}}, {{0,0,1}}, {{-.707106781f,0,.707106781f}},
  {{-1,0,0}}, {{-.707106781f,0,-.707106781f}}, {{0,0,-1}}, {{.707106781f,0,-.707106781f}},
  {{0,1,0}}
};
static acoustic_params_t acoustic_describe(const float distance[ACOUSTIC_PROBES], v3 right) {
  float close[8], coverage = 0, weight[2] = {0}, travel[2] = {0};
  for (int i = 0; i < 8; i++) {
    float d = f_is_nonfinite(distance[i]) ? ACOUSTIC_RANGE : distance[i];
    close[i] = f_clamp(1 - d / ACOUSTIC_RANGE, 0, 1);
    coverage += close[i] / 8;
    float pan = f_clamp(v3_dot(ACOUSTIC_DIR[i], right), -1, 1);
    for (int c = 0; c < 2; c++) {
      float w = close[i] * (.5f + (c ? .35f : -.35f) * pan);
      weight[c] += w;
      travel[c] += w * f_clamp(d, .343f, ACOUSTIC_RANGE);
    }
  }
  float roof_distance = f_is_nonfinite(distance[8]) ? ACOUSTIC_ROOF_RANGE : distance[8];
  float roof = f_clamp(1 - roof_distance / ACOUSTIC_ROOF_RANGE, 0, 1);
  float opposed = 0;
  for (int i = 0; i < 4; i++) {
    float nearest = fminf(close[i], close[i + 4]);
    if (nearest > opposed) opposed = nearest;
  }
  acoustic_params_t p = {.feedback = .35f + .30f * f_clamp(coverage + .45f * roof, 0, 1)};
  float enclosure = f_clamp(coverage + .45f * roof, 0, 1);
  p.late = .18f * enclosure;
  p.damping = 7000 - 3500 * enclosure;
  p.kind = roof > .35f ? ACOUSTIC_SHELTER : opposed > .55f ? ACOUSTIC_NARROW
    : coverage > .025f ? ACOUSTIC_EDGE : ACOUSTIC_OPEN;
  for (int c = 0; c < 2; c++) {
    p.early[c] = f_clamp(.06f * weight[c] + .08f * roof, 0, ACOUSTIC_EARLY_MAX);
    float roof_weight = roof * 2;
    float sum = weight[c] + roof_weight;
    p.delay[c] = sum > .001f
      ? f_clamp(2 * (travel[c] + roof_weight * f_clamp(roof_distance, .343f, ACOUSTIC_ROOF_RANGE)) / (343 * sum), .002f, .070f)
      : .020f;
  }
  return p;
}

static long g_whizz_t = -1000;
static float g_whizz_g;

// Reset cannot be lost in a full ring. The producer publishes a new epoch;
// only the mixer clears voices and consumes old triggers, by the next callback.
static _Atomic uint32_t g_audio_epoch;
static uint32_t g_audio_mix_epoch;
static void audio_session_reset(void) {
  audio_acoustic_invalidate();
  g_whizz_t = -1000; g_whizz_g = 0;
  atomic_fetch_add_explicit(&g_audio_epoch, 1, memory_order_release);
}
[[maybe_unused]] static long g_audio_underruns; // ALSA reports XRUN; WASAPI has no equivalent query here
static long g_audio_device_errors, g_audio_stream_errors, g_audio_restarts; // device/mixer-owned
static float          g_dev_rate;   // 48 kHz samples per DEVICE sample (48000/device_hz)

// Stream-loss back-off, SHARED by both audio backends: rebuild a stream that STARTED
// and broke (the sound server restarted, the sink went away), never a device that was
// never there. The slice is what keeps the retry inside the teardown's 800 ms budget —
// sleeping the whole AUDIO_RETRY_MS spends half of it before the first syscall and then
// re-enters device acquisition the teardown just asked the thread to leave. The count
// is a BURST budget: it falls back once a stream has run for 30 s.
#define AUDIO_RETRY_MAX      8
#define AUDIO_RETRY_MS       400
#define AUDIO_RETRY_SLICE_MS 20   // how often the back-off re-reads g_audio_run
#define AUDIO_RETRY_GOOD_MS  30000u  // ran this long = a working device: reset the count

// Every trigger is also recorded, always: the harness reads this back and it costs one
// branch.
typedef struct { long tick; int id; float gain, rate, pan, delay; } sfx_log_t;
#define SFX_LOG_N 512
static sfx_log_t g_sfx_log[SFX_LOG_N];
static int  g_sfx_log_n, g_sfx_log_on;
static long g_sfx_log_drops;   // decisions the full log refused — "every
                               // routing decision" must not silently mean
                               // "the first 512 of them"
static long g_sfx_log_tick;
// Whether the headless sim runs the audio routing at all. `sfxlog` wants the decisions,
// `mixdown` wants the signal, and neither may perturb a movement or determinism test
// that did not ask for audio.
[[maybe_unused]] static int g_audio_route;

// A flooded ring is the same audible symptom as a pool refusal — a sound that
// never played — so it gets a counter too, or the "dropped must read 0" proof
// contract has a loss channel it cannot see.
static long g_trig_flooded;
static long g_trig_protected_flooded, g_audio_invalid;  // producer-owned
static long g_trig_stale, g_trig_cancelled, g_protected_dropped, g_audio_bad_snapshot; // mixer-owned

// Opt-in observation for a single-thread harness take. Native producer/device
// threads never enable this census. Neither export nor allocation runs in audio_mix.
#define AUDIO_TRACE_MAX 4096
#define AUDIO_TRACE_UNKNOWN UINT64_MAX
enum { ATR_QUEUED, ATR_QUIET, ATR_INVALID, ATR_NORMAL_FLOOD, ATR_PROTECTED_FLOOD,
       ATR_STALE, ATR_CANCELLED, ATR_CAPPED, ATR_POOL_DROP, ATR_ADMITTED,
       ATR_STARTED, ATR_RESET, ATR_STOLEN, ATR_FINISHED };
typedef struct {
  uint64_t serial;
  int type, src, dst, index, weapon, kind;
} audio_trace_event_t;
static audio_trace_event_t g_audio_trace_event;
static int g_audio_event_cursor = -1; // -1 outside the producer drain
static uint64_t g_audio_trace_event_next = 1;
typedef struct {
  audio_trace_event_t event;
  uint64_t serial, first_source, first_nonzero, milestone_frame[2];
  int milestone_source[2];
  long source_tick, script_tick;
  uint32_t epoch;
  int id, tag, source_delay, device_delay, status;
  float rate;
} audio_trace_t;
static audio_trace_t g_audio_trace[AUDIO_TRACE_MAX];
static int g_audio_trace_on, g_audio_trace_n, g_audio_trace_unobserved;
static uint64_t g_audio_trace_next = 1, g_audio_trace_first, g_audio_trace_frames;
static uint64_t g_audio_trace_overflow, g_audio_trace_bad_rate;
static long g_audio_trace_start_tick, g_audio_trace_start_script;
static audio_trace_t *audio_trace_find(uint64_t serial) {
  if (!g_audio_trace_on || serial < g_audio_trace_first ||
      serial - g_audio_trace_first >= (uint64_t)g_audio_trace_n) return NULL;
  return &g_audio_trace[serial - g_audio_trace_first];
}
// Optional filming witnesses use the same source offsets as the synthesized
// reloads. A cursor crossing is observation, not a new sound or a voice update.
static void audio_trace_reload_marks(int id, int source[2]) {
  source[0] = source[1] = -1;
  if (id != SFX_RELOAD_AR && id != SFX_RELOAD_AR_TAC && id != SFX_RELOAD_SR) return;
  int weapon = id == SFX_RELOAD_SR ? WPN_SR : WPN_AR;
  int tactical = id == SFX_RELOAD_AR_TAC;
  float duration = (float)wpn_reload_ticks(weapon, tactical) / TICK_HZ;
  float seat = tactical ? RELOAD_SEAT_TAC : RELOAD_BEAT[weapon][0];
  source[0] = (int)(seat * duration * SFX_HZ);
  if (!tactical) source[1] = (int)(RELOAD_BEAT[weapon][1] * duration * SFX_HZ);
}

static uint64_t audio_trace_offer(int id, int tag, float rate, float delay) {
  if (!g_audio_trace_on) return 0;
  uint64_t serial = g_audio_trace_next++;
  if (g_audio_trace_n == AUDIO_TRACE_MAX) { g_audio_trace_overflow++; return 0; }
  audio_trace_t *t = &g_audio_trace[g_audio_trace_n++];
  *t = (audio_trace_t){.event = g_audio_trace_event, .serial = serial, .first_source = AUDIO_TRACE_UNKNOWN,
    .first_nonzero = AUDIO_TRACE_UNKNOWN,
    .milestone_frame = {AUDIO_TRACE_UNKNOWN, AUDIO_TRACE_UNKNOWN},
    .source_tick = g_audio_event_cursor >= 0 ? g_event_tick[g_audio_event_cursor] : g_tick,
    .script_tick = g_sfx_log_tick,
    .epoch = atomic_load_explicit(&g_audio_epoch, memory_order_relaxed),
    .id = id, .tag = tag, .source_delay = f_is_nonfinite(delay) || delay < 0 || delay > 60
        ? -1 : (int)(delay * SFX_HZ), .device_delay = -1, .status = ATR_QUEUED, .rate = rate};
  audio_trace_reload_marks(id, t->milestone_source);
  return serial;
}
static void audio_trace_status(uint64_t serial, int status) {
  audio_trace_t *t = audio_trace_find(serial);
  if (t) t->status = status;
}
static void audio_trace_sample(uint64_t serial, uint64_t frame, float source_cursor,
                                 float left, float right) {
  audio_trace_t *t = audio_trace_find(serial);
  if (!t) return;
  if (t->first_source == AUDIO_TRACE_UNKNOWN) {
    t->first_source = frame; t->status = ATR_STARTED;
  }
  if (t->first_nonzero == AUDIO_TRACE_UNKNOWN && (left != 0 || right != 0)) t->first_nonzero = frame;
  for (int mark = 0; mark < 2; mark++)
    if (t->milestone_source[mark] >= 0 && t->milestone_frame[mark] == AUDIO_TRACE_UNKNOWN &&
        source_cursor >= (float)t->milestone_source[mark]) t->milestone_frame[mark] = frame;
}
[[maybe_unused]] static int audio_trace_begin(void) {
#ifdef _WIN32
  return 0;
#else
  if (!g_headless || g_audio_trace_on) return 0;
  g_audio_trace_first = g_audio_trace_next;
  g_audio_trace_n = g_audio_trace_unobserved = 0;
  g_audio_trace_frames = g_audio_trace_overflow = g_audio_trace_bad_rate = 0;
  g_audio_trace_start_tick = g_tick; g_audio_trace_start_script = g_sfx_log_tick;
  for (int i = 0; i < MAX_VOICES; i++) g_audio_trace_unobserved += g_voices[i].active != 0;
  g_audio_trace_on = 1;
  return 1;
#endif
}
[[maybe_unused]] static void audio_trace_end(void) { g_audio_trace_on = 0; }
[[maybe_unused]] static int audio_trace_write(FILE *out) {
  static const char *names[] = {"queued", "quiet", "invalid", "normal_flood", "protected_flood",
    "stale", "cancelled", "capped", "pool_drop", "admitted", "started", "reset", "stolen", "finished"};
  if (!g_audio_trace_on || !out) return 0;
  fprintf(out, "{\"schema\":\"audio-cue-trace-v1\",\"start_source_tick\":%ld,\"start_script_tick\":%ld,"
    "\"sample_rate\":%d,\"output_frames\":%llu,\"count\":%d,\"overflow\":%llu,\"bad_rate\":%llu,"
    "\"unobserved_active\":%d}\n", g_audio_trace_start_tick, g_audio_trace_start_script, SFX_HZ,
    (unsigned long long)g_audio_trace_frames, g_audio_trace_n, (unsigned long long)g_audio_trace_overflow,
    (unsigned long long)g_audio_trace_bad_rate, g_audio_trace_unobserved);
  for (int i = 0; i < g_audio_trace_n; i++) {
    const audio_trace_t *t = &g_audio_trace[i];
    fprintf(out, "{\"serial\":%llu,\"source_tick\":%ld,\"script_tick\":%ld,\"id\":%d,\"tag\":%d,"
      "\"epoch\":%u,\"rate\":", (unsigned long long)t->serial, t->source_tick, t->script_tick,
      t->id, t->tag, t->epoch);
    if (f_is_nonfinite(t->rate)) fputs("null", out); else fprintf(out, "%.9g", (double)t->rate);
    fputs(",\"event_serial\":", out);
    if (t->event.serial) {
      fprintf(out, "%llu,\"event_type\":%d,\"event_src\":%d,\"event_dst\":%d,"
        "\"event_index\":%d,\"event_weapon\":%d,\"event_kind\":%d",
        (unsigned long long)t->event.serial, t->event.type, t->event.src, t->event.dst,
        t->event.index, t->event.weapon, t->event.kind);
    } else fputs("null,\"event_type\":null,\"event_src\":null,\"event_dst\":null,"
        "\"event_index\":null,\"event_weapon\":null,\"event_kind\":null", out);
    fputs(",\"name\":", out);
    if ((unsigned)t->id < SFX_COUNT) fprintf(out, "\"%s\"", SFX_NAME[t->id]);
    else fputs("null", out);
    fputs(",\"source_delay_frames\":", out);
    if (t->source_delay < 0) fputs("null", out); else fprintf(out, "%d", t->source_delay);
    fputs(",\"device_delay_frames\":", out);
    if (t->device_delay < 0) fputs("null", out); else fprintf(out, "%d", t->device_delay);
    fprintf(out, ",\"status\":\"%s\",\"first_source_sample\":", names[t->status]);
    if (t->first_source == AUDIO_TRACE_UNKNOWN) fputs("null", out);
    else fprintf(out, "%llu", (unsigned long long)t->first_source);
    fputs(",\"first_nonzero_dry_sample\":", out);
    if (t->first_nonzero == AUDIO_TRACE_UNKNOWN) fputs("null", out);
    else fprintf(out, "%llu", (unsigned long long)t->first_nonzero);
    fputs(",\"milestones\":[", out);
    for (int mark = 0; mark < 2; mark++) {
      if (t->milestone_source[mark] < 0) continue;
      fprintf(out, "%s{\"name\":\"%s\",\"source_sample\":%d,\"output_sample\":",
        mark ? "," : "", mark ? "bolt" : "seat", t->milestone_source[mark]);
      if (t->milestone_frame[mark] == AUDIO_TRACE_UNKNOWN) fputs("null", out);
      else fprintf(out, "%llu", (unsigned long long)t->milestone_frame[mark]);
      fputc('}', out);
    }
    fputs("]}\n", out);
  }
  return fflush(out) == 0 && !ferror(out) && !g_audio_trace_overflow && !g_audio_trace_bad_rate;
}

// Consumer only. A newly published command after either captured boundary stays.
static void audio_mix_discard_pending(void) {
  audio_snapshot_t end = audio_mix_snapshot();
  if (g_audio_trace_on) {
    uint32_t begin = atomic_load_explicit(&g_trig_r, memory_order_relaxed);
    if (end.normal - begin <= TRIG_RING)
      for (uint32_t i = begin; i != end.normal; i++) audio_trace_status(g_trig[i % TRIG_RING].trace, ATR_RESET);
    begin = atomic_load_explicit(&g_protected_r, memory_order_relaxed);
    if (end.protected - begin <= TRIG_RING)
      for (uint32_t i = begin; i != end.protected; i++) audio_trace_status(g_protected_trig[i % TRIG_RING].trace, ATR_RESET);
  }
  atomic_store_explicit(&g_trig_r, end.normal, memory_order_release);
  atomic_store_explicit(&g_protected_r, end.protected, memory_order_release);
}

static void audio_play_tag(int id, float gain, float rate, float pan,
                           float delay_s, int tag) {
  uint64_t trace = audio_trace_offer(id, tag, rate, delay_s);
  // Anything this quiet is below the noise floor of a firefight; spawning it
  // only costs a voice that a nearer sound needs. `sfxlog` measured 64 sounds
  // wanting to be alive at once in a 20-bot fight against a 24-voice mixer,
  // and most of them were distant shots at gain 0.02.
  if ((unsigned)id >= SFX_COUNT || (unsigned)tag >= AUDIO_TAGS ||
      f_is_nonfinite(gain) || f_is_nonfinite(rate) || f_is_nonfinite(pan) ||
      f_is_nonfinite(delay_s) || gain < 0 || gain > 16 || rate <= 0 || rate > 8 ||
      delay_s < 0 || delay_s > 60) { g_audio_invalid++; audio_trace_status(trace, ATR_INVALID); return; }
  if (gain < 0.035f) { audio_trace_status(trace, ATR_QUIET); return; }
  // The log records ROUTING decisions and deliberately sits BEFORE the ring
  // admit: in the harness's routing-only mode (`sfxlog`) nothing consumes the
  // ring at all, so a log gated on admission would fall silent after 64
  // entries — the command drains the ring itself instead (see its loop).
  if (g_sfx_log_on) {
    if (g_sfx_log_n < SFX_LOG_N)
      g_sfx_log[g_sfx_log_n++] = (sfx_log_t){g_sfx_log_tick, id, gain, rate,
                                             f_clamp(pan, -1, 1), delay_s};
    else
      g_sfx_log_drops++;
  }
  int protected = sfx_prio(id) != SFX_P_NORM;
  _Atomic uint32_t *write = protected ? &g_protected_w : &g_trig_w;
  _Atomic uint32_t *read = protected ? &g_protected_r : &g_trig_r;
  sfx_trig_t *ring = protected ? g_protected_trig : g_trig;
  uint32_t w = atomic_load_explicit(write, memory_order_relaxed);
  uint32_t r = atomic_load_explicit(read, memory_order_acquire);
  if (w - r >= TRIG_RING) {
    if (protected) g_trig_protected_flooded++; else g_trig_flooded++;
    audio_trace_status(trace, protected ? ATR_PROTECTED_FLOOD : ATR_NORMAL_FLOOD);
    return;
  }
  ring[w % TRIG_RING] = (sfx_trig_t){id, gain, rate, f_clamp(pan, -1, 1),
      (int)(delay_s * SFX_HZ), tag,
      atomic_load_explicit(&g_audio_epoch, memory_order_relaxed),
      atomic_load_explicit(&g_audio_cancel[id][tag], memory_order_relaxed), trace};
  atomic_store_explicit(write, w + 1, memory_order_release);
}
static void audio_play(int id, float gain, float rate, float pan, float delay_s) {
  audio_play_tag(id, gain, rate, pan, delay_s, 0);
}

// The producer publishes cancellation without touching consumer-owned voices.
// New same-tag plays capture the new generation and remain eligible.
static void audio_kill(int id, int tag) {
  if ((unsigned)id >= SFX_COUNT || (unsigned)tag >= AUDIO_TAGS) {
    g_audio_invalid++; return;
  }
  atomic_fetch_add_explicit(&g_audio_cancel[id][tag], 1, memory_order_release);
}

static uint64_t g_rng_audio = 0xA0D10;
// The confirmation offset depends on WHICH weapon fired, and neither EV_HIT nor EV_KILL
// carries it.
// Set only while the shared frame drain routes an already admitted event.
static float g_audio_event_age;
static float audio_confirmation_delay(int weapon, float extra) {
  return fmaxf(0.0f, WPN_DEF[weapon].sfx_hit_t + extra - g_audio_event_age);
}
static int      g_last_wpn;  // weapon of the player's most recent shot
static long     g_multi_t = -100000;  // tick of the player's last kill...
static int      g_multi_n;            // ...and how many are inside the window
#define MULTI_WIN 360        // ticks: 3 s. NOT the killfeed's window — that is
                             // KILLFEED_TICKS 480 (4 s) — this is the genre's
                             // own double-kill window, which is shorter.
static v3       g_listen;    // player eye
static v3       g_listen_r;  // ...and which way is right, for panning

// Personal feedback resets cancel offered cues and invalidate older queued feedback.
// A reset encountered during the drain preserves every later event in that frame.
static void audio_feedback_reset(void) {
  int end = g_audio_event_cursor >= 0 ? g_audio_event_cursor + 1 : g_num_events;
  for (int i = 0; i < end; i++) g_event_feedback[i] = 0;
  static const int cue[] = {SFX_HIT, SFX_HIT_HEAD, SFX_KILL, SFX_KILL_HEAD, SFX_HURT};
  for (size_t i = 0; i < sizeof cue / sizeof *cue; i++) audio_kill(cue[i], 0);
  g_last_wpn = WPN_AR;
  g_multi_t = -100000; g_multi_n = 0;
}

// Where a sound sits relative to the listener: -1 hard left, +1 hard right, plus the
// distance it travelled.
static float audio_place(v3 at, float *dist) {
  v3 d = v3_sub(at, g_listen);
  float len = sqrtf(v3_dot(d, d));
  *dist = len;
  if (len < 0.25f) return 0.0f;
  return f_clamp(v3_dot(d, g_listen_r) / len, -1.0f, 1.0f) * 0.85f;
}

// Runs only on the listener producer; never consumes simulation random state.
static void audio_acoustic_update(void) {
  g_acoustic_last_queries = 0;
  if (g_tick >= 0 && g_acoustic_next > g_tick && g_acoustic_next - g_tick <= ACOUSTIC_INTERVAL) return;
  float distance[ACOUSTIC_PROBES];
  for (int axis = 0; axis < 3; axis++)
    if (f_is_nonfinite(g_listen.e[axis]) || f_is_nonfinite(g_listen_r.e[axis])) {
      audio_acoustic_invalidate(); return;
    }
  for (int i = 0; i < ACOUSTIC_PROBES; i++) {
    v3 normal;
    float reach = i == 8 ? ACOUSTIC_ROOF_RANGE : ACOUSTIC_RANGE;
    distance[i] = fminf(trace_world(g_listen, ACOUSTIC_DIR[i], &normal, NULL), reach);
    g_acoustic_queries++; g_acoustic_last_queries++;
  }
  g_acoustic_updates++;
  g_acoustic_next = g_tick + ACOUSTIC_INTERVAL;
  audio_acoustic_publish(acoustic_describe(distance, g_listen_r));
}

static void audio_on_event(const event_t *e) {
  switch (e->type) {
  case EV_FIRE: {
    float rate = rng_rangef(&g_rng_audio, 0.97f, 1.03f);
    if (e->src == g_local_ent) {  // your own weapon: dead centre, full level
      g_last_wpn = e->wpn;
      // PER WEAPON, because both buffers are PEAK-normalised and both were routed at
      // the same gain — so a one-shot rifle arrived at the bus exactly as hard as an
      // assault rifle, by construction, with 4.5 dB of headroom sitting unused in front
      // of it.
      float lvl = e->wpn == WPN_SR ? 1.12f : 1.0f;
      audio_play(WPN_DEF[e->wpn].sfx_fire,
                 lvl * rng_rangef(&g_rng_audio, 0.92f, 1.0f), rate, 0.0f, 0.0f);
      break;
    }
    // Someone else's, placed where the round started — the shooter's eye, close
    // enough to the rifle that no ear could name the difference.
    v3 o = v3_add(e->a, v3_scale(e->b, -e->f));
    float dist, pan = audio_place(o, &dist);
    float fmix = f_clamp((dist - 12.0f) / 14.0f, 0.0f, 1.0f);
    float att = f_clamp(1.0f / (1.0f + dist * dist * 0.0032f), 0.06f, 1.0f);
    // Sound travels: 343 m/s is ~3 ms per metre, and across a 48 m arena that
    // is 140 ms — long enough to hear, and it is most of why a distant shot
    // reads as distant rather than as quiet.
    float t = dist / 343.0f;
    int id_near = WPN_DEF[e->wpn].sfx_fire;
    int id_far  = e->wpn == WPN_SR ? SFX_FAR_SR : SFX_FAR_AR;
    // Equal power, not linear.
    if (fmix < 0.98f)
      audio_play(id_near, att * sqrtf(1.0f - fmix) * 0.95f, rate, pan, t);
    if (fmix > 0.02f)
      audio_play(id_far, att * sqrtf(fmix), rate, pan * 0.7f, t);
    break;
  }
  case EV_HIT:
  case EV_HIT_HEAD: {
    // THE WORLD HEARS IT, WHOEVER FIRED. This runs before and independently of the
    // screen-space confirmation below, because the two are different things for
    // different listeners: the LEAD tick is feedback about the player and is centred
    // and undistanced, this is an event at a POINT in the arena and every shooter makes
    // it.
    float fd, fp = audio_place(e->a, &fd);
    // 0.52 and an 11 m gate, not 0.85 and 14.
    if (fd < 11.0f)
      audio_play(SFX_FLESH, f_clamp(0.52f / (1.0f + fd * 0.16f), 0.05f, 0.48f),
                 rng_rangef(&g_rng_audio, 0.92f, 1.10f), fp, fd / 343.0f);
    // A hit that KILLED gets the kill confirmation and nothing else.
    if (e->src == g_local_ent && (!event_local_outcome(e) || e->k <= 0)) break;
    if (event_local_outcome(e)) {
      // DELAYED, and that is the whole trick.
      audio_play(e->type == EV_HIT_HEAD ? SFX_HIT_HEAD : SFX_HIT, SFX_LEAD_GAIN,
                 rng_rangef(&g_rng_audio, 0.99f, 1.01f), 0.0f,
                 audio_confirmation_delay(g_last_wpn, 0));
    } else if (e->dst == g_local_ent) {
      // Taking a round: its own dark sound, panned to where it came from, so the audio
      // tells you which way to turn.
      if (e->k <= 0 || e->i <= 0) break;
      v3 direction;
      float pan = event_actor_pair(e) && event_direction(e->b, e->a, &direction) &&
                  event_direction(e->b, g_listen, &direction)
        ? f_clamp(v3_dot(direction, g_listen_r), -1.0f, 1.0f) * 0.85f : 0.0f;
      audio_play(SFX_HURT, 1.0f, rng_rangef(&g_rng_audio, 0.94f, 1.06f),
                 pan * 0.8f, 0.0f);
    }
    break;
  }
  case EV_KILL:
    if (!event_actor_pair(e)) break;
    if (event_local_outcome(e)) {
      // MULTI-KILL ESCALATION, for zero new synthesis.
      if (g_tick - g_multi_t > MULTI_WIN) g_multi_n = 0;
      g_multi_t = g_tick;
      static const float MRATE[3] = {1.0f, 1.122f, 1.260f};
      audio_play(e->k ? SFX_KILL_HEAD : SFX_KILL, SFX_LEAD_GAIN,
                 MRATE[g_multi_n], 0.0f, audio_confirmation_delay(g_last_wpn, 0));
      if (g_multi_n < 2) g_multi_n++;
    }
    else if (e->dst == g_local_ent) {
      audio_feedback_reset();
      audio_play(SFX_HURT, 1.0f, 0.72f, 0.0f, 0.0f);   // current death cue survives reset
    }
    break;
  case EV_STEP: {
    float dist, pan = audio_place(e->a, &dist);
    // Your own boots are felt more than heard, so they sit low and centred; everyone
    // else's are the information.
    float rs = g_cfg.mv[MV_RUN] > 1.0f ? g_cfg.mv[MV_RUN] : 1.0f;
    float sp = f_clamp(e->f / rs, 0.22f, 1.0f);
    float g = (e->src == g_local_ent ? 0.30f : 1.15f)
            * (0.30f + 0.90f * sp * sp)
            * (1.0f - 0.55f * e->g)
            * f_clamp(1.0f / (1.0f + dist * dist * 0.020f), 0.0f, 1.0f);
    // A FLOOR, not a fade to nothing.
    if (e->src != g_local_ent && g < 0.10f) break;
    audio_play(SFX_STEP, g, rng_rangef(&g_rng_audio, 0.86f, 1.16f),
               e->src == g_local_ent ? 0.0f : pan, dist / 343.0f);
    break;
  }
  case EV_LAND: {
    // The same boot, heavier and slower.
    float dist, pan = audio_place(e->a, &dist);
    float k = f_clamp((e->f - 1.6f) / 7.0f, 0.15f, 1.0f);
    float g = (e->src == g_local_ent ? 0.85f : 1.5f) * (0.35f + 0.85f * k)
            * f_clamp(1.0f / (1.0f + dist * dist * 0.016f), 0.0f, 1.0f);
    if (g < 0.10f) break;
    audio_play(SFX_STEP, g, 0.66f + 0.14f * (1.0f - k) +
               rng_rangef(&g_rng_audio, -0.03f, 0.03f),
               e->src == g_local_ent ? 0.0f : pan, dist / 343.0f);
    break;
  }
  // The dry click is a hands-on cue: it says YOUR magazine is empty, so it is the local
  // shooter's only.
  case EV_DRYFIRE:
    if (e->src == g_local_ent) audio_play(SFX_DRY, 1.0f, 1.0f, 0.0f, 0.0f);
    break;
  case EV_RELOAD:
    // The player's own reload is in his hands: centred, close, undistanced. A bot's is
    // an event at a point in the arena and is routed like one.
    {
    // A NEUTRALISED src is dropped. net_ev_to_live lands a hostile entity id on -1, and
    // `src + 2` would then hand it tag 1 — the PLAYER's reserved tag — so a later
    // cancel would fade the player's own reload.
    if (e->src < 0) break;
    int rid = (e->k && e->wpn == WPN_AR) ? SFX_RELOAD_AR_TAC
                                         : WPN_DEF[e->wpn].sfx_reload;
    if (e->src == g_local_ent) {
      // Shared reload beats stay on the animation time axis at playback rate 1.
      // Tag 1 makes cancellation affect only the local player's reload.
      audio_play_tag(rid, 0.9f,
                     1.0f, -0.12f, 0.0f, 1);
    } else {
      float dist, pan = audio_place(e->a, &dist);
      if (dist < 14.0f)
        audio_play_tag(rid,
                       0.75f * f_clamp(1.0f / (1.0f + dist * dist * 0.028f), 0.0f, 1.0f),
                       1.0f, pan, dist / 343.0f,
                       e->src + 2);
    }
    }
    break;
  // The tag is entity-specific, so a bot's aborted reload fades only its own
  // voice rather than every nearby bot sharing the same weapon buffer.
  case EV_RELOAD_CANCEL:
    {
      if (e->src < 0) break;                 // same reserved-tag rule as above
      int tag = e->src == g_local_ent ? 1 : e->src + 2;
      audio_kill(WPN_DEF[e->wpn].sfx_reload, tag);
      if (e->wpn == WPN_AR) audio_kill(SFX_RELOAD_AR_TAC, tag);
    }
    break;
  case EV_END:
    // The victory sting is the SAME BUFFER as the headshot kill, one whole tone down,
    // and it was played at delay 0 while the kill that won the match was waiting out
    // its per-weapon confirmation offset.
    if (e->k == g_local_ent)
      audio_play(SFX_KILL_HEAD, SFX_LEAD_GAIN, 0.85f, 0.0f,
                 audio_confirmation_delay(g_last_wpn, 0.38f));
    else
      audio_play(SFX_HURT, 0.85f, 0.62f, 0.0f, 0.30f);
    break;
  // Listed rather than defaulted, so -Wswitch makes a new ev_type_t a COMPILE
  // ERROR here instead of a silently inaudible event. ev_push's exhaustive
  // printer is gated on `g_headless`, so on any WINDOWED target this switch and
  // vfx_on_event's are the only enforcement that exists.
  case EV_IMPACT: {
    // A ROUND ARRIVING IN THE WORLD IS ITS OWN EVENT, and it has to be routed off that
    // event rather than off the shot's outcome.
    if (e->k) break;                 // entry/exit remain suppressed; the final solid emits once
    float dist, pan = audio_place(e->a, &dist);
    // A ROUND HITTING THE WALL BESIDE YOUR HEAD AND ONE 20 M AWAY SOUNDED THE SAME. The
    // old law spans 7.6 dB from 0.5 m to 20 m — measured over 992 logged impacts only
    // 21 (2.1%) ever reached its 0.85 clamp — where the physics is ~32 dB. Split law:
    // beyond the seam the old curve, which is tuned and which most impacts live on;
    // inside it an inverse-square, giving 0.5 m -> 0.98.
    if (dist < 22.0f) {
      int wall = e->i >= 0 && e->i < g_num_solids && g_solids[e->i].mat == SOLID_WALL;
      int floor = e->i < 0;
      float body = floor ? .78f : wall ? .92f : 1.0f;
      float pitch = floor ? .72f : wall ? .86f : 1.08f;
      float gain = dist < 1.33f
        ? f_clamp(1.0f / (1.0f + 0.10f * dist * dist), 0.08f, 1.0f)
        : f_clamp(1.2f / (1.0f + dist * 0.12f), 0.10f, 0.85f);
      audio_play(SFX_IMPACT, gain * body, pitch * rng_rangef(&g_rng_audio, .97f, 1.03f),
                 pan, dist / 343.0f);
    }
    break;
  }
  case EV_RESPAWN:
    if ((unsigned)g_local_ent < MAX_ENTS && e->dst == g_local_ent) audio_feedback_reset();
    break;
  }
}

// The near-miss crack is spawned from the SHOT event rather than from its own event
// type: EV_FIRE carries the finite travelled span. Endpoint margins, material
// containment and a short listener ray restrict the cue to an audible air pass.
#define WHIZZ_HOLD 11        // ticks: 90 ms
// 4.2 m, not 3.0.
#define WHIZZ_MISS 4.2f

static void audio_shot_world(const event_t *e) {
  if (e->type != EV_FIRE || (unsigned)e->wpn >= WPN_COUNT || f_is_nonfinite(e->f) || e->f <= 1) return;
  // EXCLUDE ONLY ROUNDS THAT HIT THE LISTENER. The old guard rejected `k == 2 || k ==
  // 3`, i.e.
  if (e->src != g_local_ent && e->dst != g_local_ent) {
    // A round that went past you. The segment is the ballistic one: it begins at the
    // SHOOTER'S EYE, where the round was traced from, not at the rifle's barrel tip —
    // half a metre of it lies behind a shoulder nobody stands next to, and the 0.5 m
    // end margins below already refuse a crack from inside the shooter.
    v3 start = v3_sub(e->a, v3_scale(e->b, e->f));
    segment_near_t nearest;
    if (!segment_near(start, e->a, g_listen, &nearest)) return;
    float t = nearest.along, miss = nearest.miss;
    v3 perp = nearest.offset;
    if (t > 0.5f && t < nearest.length - 0.5f && miss < WHIZZ_MISS) {
      // Penetrated material is part of the event span, but not an air pass.
      for (int i = 0; i < g_num_solids; i++) {
        g_acoustic_whizz_solids++;
        const solid_t *box = &g_solids[i];
        if (nearest.closest.x > box->min.x && nearest.closest.x < box->max.x &&
            nearest.closest.y > box->min.y && nearest.closest.y < box->max.y &&
            nearest.closest.z > box->min.z && nearest.closest.z < box->max.z) return;
      }
      if (miss > .01f) {
        g_acoustic_whizz_rays++;
        v3 normal;
        if (trace_world(g_listen, v3_scale(perp, 1 / miss), &normal, NULL) < miss - .01f) return;
      }
      // ...and WHICH side it went past, which was hardcoded to centre.
      float pan = miss > 0.01f
                ? f_clamp(v3_dot(perp, g_listen_r) / miss, -1.0f, 1.0f) * 0.9f
                : 0.0f;
      // A CONCAVE LEVEL LAW, because a linear one cannot say "that was CLOSE".
      float k = f_clamp(1.0f - miss / WHIZZ_MISS, 0.0f, 1.0f);
      float g = powf(k, 3.3f) * 0.92f;
      // THE CRACK ARRIVES BEFORE THE BANG. The round covers `t` metres at its muzzle
      // velocity and its bow wave then travels `miss` metres of air to the ear; the
      // report covers the whole `t` at 343 m/s.
      float v = WPN_DEF[e->wpn].v_round;
      float delay = t / (v > 1.0f ? v : 343.0f) + miss / 343.0f;
      // One crack per 90 ms unless the new one is closer, so a burst is a crack and a
      // rattle.
      if (g_tick - g_whizz_t < WHIZZ_HOLD && g <= g_whizz_g) return;
      g_whizz_t = g_tick;
      g_whizz_g = g;
      // A near pass is SHARPER as well as louder: the rate rides the same curve,
      // so the bow wave's whole spectrum shifts up as it gets closer instead of
      // only its level. And a .338 is not a 5.56: the heavier round plays the
      // same buffer a fifth lower, which reads as MASS without a second buffer
      // — the same trick fx_flash uses for the weapon-differentiated star.
      audio_play(SFX_WHIZZ, g, (e->wpn == WPN_SR ? 0.81f : 1.0f)
                 * rng_rangef(&g_rng_audio, 0.92f, 1.10f)
                 * (0.90f + 0.22f * k), pan, delay);
    }
  }
}

static void audio_event(const event_t *e) {
  // One event may produce near/far/world cues; the observer retains their common
  // identity without adding fields to gameplay events or the network protocol.
  if (g_audio_trace_on) g_audio_trace_event = (audio_trace_event_t){
    .serial = g_audio_trace_event_next++, .type = e->type, .src = e->src, .dst = e->dst,
    .index = e->i, .weapon = e->wpn, .kind = e->k};
  audio_on_event(e); audio_shot_world(e);
  if (g_audio_trace_on) g_audio_trace_event = (audio_trace_event_t){0};
}

// MAX_VOICES mixer, fed through a lock-free SPSC ring: the game thread posts
// triggers, the audio thread (WASAPI or ALSA — or, headless, the `mixdown`
// command) starts voices and mixes.
static float g_lim_g = 1.0f;   // master limiter gain state
// Two measurement hooks, one compare each.
static float g_lim_min = 1.0f;   // deepest bus gain reduction seen
static int   g_voice_peak;       // most voices simultaneously audible
static long  g_trig_dropped;     // triggers the pool refused
// ...and the ones sfx_class_cap refused, which is a DIFFERENT fact.
static long  g_trig_capped;
static float g_duck_env;         // lead-voice envelope driving the sidechain
static float g_duck_min = 1.0f;  // deepest duck applied to the non-lead bus

// The menu-pause reaches the AUDIO TIMELINE too: freezing only the tick accumulator
// lets every in-flight voice run ahead of the frozen sim — a paused mid-reload played
// its seat and bolt beats ("A LIVE WEAPON") over a world in which the magazine never
// moved, then completed silently on resume.
static _Atomic int g_audio_pause;
static float       g_pause_env = 1.0f;

// Callback-owned fixed storage, independent from offline synthesis scratch.
// The four-line feedback transform is orthogonal; feedback is bounded by 0.65.
#define AUDIO_WET_RING 16384u
#define AUDIO_WET_MASK (AUDIO_WET_RING - 1u)
typedef struct {
  float early[2][AUDIO_WET_RING], line[4][AUDIO_WET_RING], damp[4];
  unsigned cursor, length[4], old_delay[2], delay[2], fade_left, fade_frames;
  float early_gain[2], late_gain, feedback, damping, smooth, crossfade, device_rate;
  acoustic_params_t target;
  uint64_t word;
  int configured, paused_clear;
} audio_wet_t;
static audio_wet_t g_audio_wet;
static_assert(sizeof(audio_wet_t) <= 394000, "fixed wet storage ceiling");
static_assert(AUDIO_WET_RING > 192000u * 70u / 1000u, "maximum early delay fits");
static void audio_wet_reset(void) {
  memset(&g_audio_wet, 0, sizeof g_audio_wet);
}
static void audio_wet_prepare(void) {
  audio_wet_t *w = &g_audio_wet;
  float hz = (float)SFX_HZ / g_dev_rate;
  if (w->configured && w->device_rate != g_dev_rate) audio_wet_reset();
  if (!w->configured) {
    static const float seconds[4] = {.0311f, .0377f, .0433f, .0531f};
    for (int i = 0; i < 4; i++) w->length[i] = (unsigned)(seconds[i] * hz + .5f);
    for (int c = 0; c < 2; c++) w->delay[c] = w->old_delay[c] = (unsigned)(.020f * hz);
    w->fade_frames = (unsigned)(.050f * hz + .5f);
    w->crossfade = 1.0f / (float)w->fade_frames;
    w->smooth = 1 - expf(-1 / (.035f * hz));
    w->damping = 1 - expf(-2 * F_PI * 7000 / hz);
    w->device_rate = g_dev_rate;
    w->configured = 1;
    w->word = UINT64_MAX;
  }
  uint64_t word = atomic_load_explicit(&g_acoustic_word, memory_order_acquire);
  // Finish an existing two-tap fade before accepting another tap location.
  // Continuous levels still follow the newest packet without discontinuities.
  w->target = acoustic_unpack(word);
  if (word != w->word && !w->fade_left) {
    for (int c = 0; c < 2; c++) {
      w->old_delay[c] = w->delay[c];
      w->delay[c] = (unsigned)(w->target.delay[c] * hz + .5f);
    }
    w->fade_left = w->fade_frames;
    w->word = word;
  }
  w->target.damping = 1 - expf(-2 * F_PI * w->target.damping / hz);
}
static void audio_wet_sample(float left, float right, float *wet_l, float *wet_r) {
  audio_wet_t *w = &g_audio_wet;
  unsigned at = w->cursor & AUDIO_WET_MASK;
  w->early[0][at] = left; w->early[1][at] = right;
  float mix = 1 - (float)w->fade_left * w->crossfade;
  float early[2], input[2] = {left, right};
  for (int c = 0; c < 2; c++) {
    w->early_gain[c] += (w->target.early[c] - w->early_gain[c]) * w->smooth;
    unsigned a = (w->cursor - w->old_delay[c]) & AUDIO_WET_MASK;
    unsigned b = (w->cursor - w->delay[c]) & AUDIO_WET_MASK;
    // Preserve the dry channel bearing with only a small reflected crossfeed.
    float xa = .85f * w->early[c][a] + .15f * w->early[1-c][a];
    float xb = .85f * w->early[c][b] + .15f * w->early[1-c][b];
    early[c] = (xa + (xb - xa) * mix) * w->early_gain[c];
  }
  if (w->fade_left) w->fade_left--;
  w->late_gain += (w->target.late - w->late_gain) * w->smooth;
  w->feedback += (w->target.feedback - w->feedback) * w->smooth;
  w->damping += (w->target.damping - w->damping) * w->smooth;
  float z[4], sum = 0;
  for (int i = 0; i < 4; i++) {
    float x = w->line[i][(w->cursor - w->length[i]) & AUDIO_WET_MASK];
    w->damp[i] += (x - w->damp[i]) * w->damping;
    z[i] = w->damp[i]; sum += z[i];
  }
  for (int i = 0; i < 4; i++)
    w->line[i][at] = input[i & 1] * .25f + (.5f * sum - z[i]) * w->feedback;
  *wet_l = early[0] + (z[0] + z[1] + z[2] - z[3]) * (.25f * w->late_gain);
  *wet_r = early[1] + (z[0] - z[1] + z[2] + z[3]) * (.25f * w->late_gain);
  w->cursor++;
}

// All timeline clearing is mixer-owned. Device, epoch and independent captures
// share this owner; contiguous capture segments do not clear it.
static void audio_mix_reset_state(void) {
  if (g_audio_trace_on) for (int i = 0; i < MAX_VOICES; i++)
    if (g_voices[i].active) audio_trace_status(g_voices[i].trace, ATR_RESET);
  memset(g_voices, 0, sizeof g_voices);
  audio_wet_reset();
  g_duck_env = 0.0f;
  g_lim_g = g_pause_env = 1.0f;
}
static int audio_device_format_valid(unsigned hz, int channels) {
  return hz >= 8000 && hz <= 192000 && channels >= 1 && channels <= 8;
}
// Consumer only, after a negotiated endpoint format and before its first mix.
// Reset takes bounded queue snapshots; later producer commands remain pending.
static int audio_mix_device_reset(unsigned hz, int channels) {
  if (!audio_device_format_valid(hz, channels)) {
    g_audio_device_errors++; return 0;
  }
  g_dev_rate = (float)SFX_HZ / (float)hz;
  audio_mix_reset_state();
  audio_mix_discard_pending();
  return 1;
}
static void audio_mix_sync_epoch(void) {
  uint32_t epoch = atomic_load_explicit(&g_audio_epoch, memory_order_acquire);
  if (epoch == g_audio_mix_epoch) return;
  audio_mix_reset_state();
  g_audio_mix_epoch = epoch;
}
static void audio_mix_sync_cancel(void) {
  for (int i = 0; i < MAX_VOICES; i++) {
    voice_t *v = &g_voices[i];
    if (!v->active) continue;
    uint32_t cancel = atomic_load_explicit(&g_audio_cancel[v->id][v->tag], memory_order_acquire);
    if (cancel == v->cancel) continue;
    v->cancel = cancel;
    audio_trace_status(v->trace, ATR_CANCELLED);
    if (v->delay > 0) v->active = 0;
    else v->dying = 1;
    g_trig_cancelled++;
  }
}

static void audio_mix_admit_trigger(sfx_trig_t t) {
  // Recheck per command: a reset after the initial callback read may publish
  // a new cue before the captured boundary, and that cue must survive.
  audio_mix_sync_epoch();
  if (t.epoch != g_audio_mix_epoch) { g_trig_stale++; audio_trace_status(t.trace, ATR_STALE); return; }
  uint32_t cancel = atomic_load_explicit(&g_audio_cancel[t.id][t.tag], memory_order_acquire);
  if (cancel != t.cancel) { g_trig_cancelled++; audio_trace_status(t.trace, ATR_CANCELLED); return; }
  int cap = sfx_class_cap(t.id);
  if (cap > 0) {
    int live = 0;
    for (int i = 0; i < MAX_VOICES; i++)
      if (g_voices[i].active && !g_voices[i].dying && g_voices[i].delay == 0 &&
          g_voices[i].buf == g_sfx_buf[t.id]) live++;
    if (live >= cap) { g_trig_capped++; audio_trace_status(t.trace, ATR_CAPPED); return; }
  }
  float gl = t.gain * sqrtf(0.5f * (1.0f - t.pan));
  float gr = t.gain * sqrtf(0.5f * (1.0f + t.pan));
  int slot = -1;
  for (int i = 0; i < MAX_VOICES; i++)
    if (!g_voices[i].active) { slot = i; break; }
  int prio = sfx_prio(t.id);
  if (slot < 0) {
    float worst = 1e9f;
    int worst_prio = SFX_P_LEAD;
    for (int i = 0; i < MAX_VOICES; i++) {
      const voice_t *v = &g_voices[i];
      if (v->prio == SFX_P_LEAD || v->prio > prio) continue;
      if (v->prio == prio && v->pos < (float)v->len * 0.55f) continue;
      float gain = v->gl + v->gr;
      if (v->prio < worst_prio || (v->prio == worst_prio && gain < worst)) {
        worst_prio = v->prio; worst = gain; slot = i;
      }
    }
    if (slot < 0 || (worst_prio == prio && worst > gl + gr)) {
      g_trig_dropped++;
      if (prio != SFX_P_NORM) g_protected_dropped++;
      audio_trace_status(t.trace, ATR_POOL_DROP);
      return;
    }
  }
  if (g_voices[slot].active) audio_trace_status(g_voices[slot].trace, ATR_STOLEN);
  g_voices[slot] = (voice_t){.active = 1, .buf = g_sfx_buf[t.id], .len = g_sfx_len[t.id],
    .rate = t.rate, .gl = gl, .gr = gr,
    .delay = (int)((float)t.delay / (g_dev_rate > 0.01f ? g_dev_rate : 1.0f)),
    .prio = prio, .id = t.id, .cancel = t.cancel, .tag = t.tag, .trace = t.trace};
  audio_trace_t *witness = audio_trace_find(t.trace);
  if (witness) { witness->status = ATR_ADMITTED; witness->device_delay = g_voices[slot].delay; }
}

static void audio_mix_admit_ring(const sfx_trig_t *ring, _Atomic uint32_t *read,
                                  uint32_t end) {
  uint32_t r = atomic_load_explicit(read, memory_order_relaxed);
  if (end - r > TRIG_RING) { g_audio_bad_snapshot++; return; }
  for (; r != end; r++) audio_mix_admit_trigger(ring[r % TRIG_RING]);
  atomic_store_explicit(read, r, memory_order_release);
}
static void audio_mix_admit_until(audio_snapshot_t end) {
  audio_mix_admit_ring(g_protected_trig, &g_protected_r, end.protected);
  audio_mix_admit_ring(g_trig, &g_trig_r, end.normal);
  audio_mix_sync_cancel();
}
static void audio_mix_admit(void) {
  audio_mix_sync_epoch();
  audio_mix_admit_until(audio_mix_snapshot());
}

// The output knee is the same scalar operation for stereo and folded mono.
static float audio_soft_knee(float sample) {
  float a = sample < 0 ? -sample : sample;
  if (a > 0.90f) {
    float over = a - 0.90f;
    a = 0.90f + over / (1.0f + over * 10.0f);
    sample = sample < 0 ? -a : a;
  }
  return sample;
}

static void audio_mix(float *out, uint32_t frames, int channels) {
  if ((!out && frames) || channels < 1 || channels > 8) { g_audio_device_errors++; return; }
  if (f_is_nonfinite(g_dev_rate) || g_dev_rate < .25f || g_dev_rate > 6.0f) {
    g_audio_device_errors++;
    if (out) memset(out, 0, (size_t)frames * (size_t)channels * sizeof *out);
    return;
  }
  if (g_audio_trace_on && g_dev_rate != 1.0f) g_audio_trace_bad_rate++;
  audio_mix_admit();
  audio_wet_prepare();

  // THE VOLUME KNOB IS AN OUTPUT TRIM, NOT A DRIVE. Applied before the limiter, turning
  // it up fed the limiter harder instead of making anything louder.
  const float vol = VOL_REF;
  const float out_vol = g_cfg.vol / VOL_REF;
  int paused = atomic_load_explicit(&g_audio_pause, memory_order_relaxed);
  const float fade = g_dev_rate == 1.0f ? 0.9945f : powf(0.9945f, g_dev_rate);
  for (uint32_t f = 0; f < frames; f++) {
    // Pause envelope: fade to silence, then hold every voice in place.
    if (paused) {
      g_pause_env *= fade;
      if (g_pause_env < 1e-4f) {  // fully faded: freeze voices, emit silence
        if (!g_audio_wet.paused_clear) { audio_wet_reset(); g_audio_wet.paused_clear = 1; }
        if (channels == 1) out[f] = 0;
        else for (int c = 0; c < channels; c++)
          out[f * (uint32_t)channels + (uint32_t)c] = 0;
        continue;
      }
    } else if (g_pause_env < 1.0f) {
      g_pause_env += (1.0f - g_pause_env) * (1.0f - fade);
      if (g_pause_env > 0.9995f) g_pause_env = 1.0f;
    }
    if (g_audio_wet.paused_clear) { audio_wet_reset(); audio_wet_prepare(); }
    float sl = 0, sr = 0, ll = 0, lr = 0, kl = 0, kr = 0;
    int live = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
      voice_t *v = &g_voices[i];
      if (!v->active) continue;
      if (v->delay > 0) { v->delay--; continue; }  // scheduled, not yet audible
      if (v->dying) {  // ~35 ms exponential fade at 48 kHz, then the slot frees
        v->gl *= fade;
        v->gr *= fade;
        if (v->gl + v->gr < 1e-4f) { v->active = 0; continue; }
      }
      int ip = (int)v->pos;
      if (ip + 1 >= v->len) { audio_trace_status(v->trace, ATR_FINISHED); v->active = 0; continue; }
      float fr = v->pos - (float)ip;
      const float *b = v->buf + ip * SFX_CH;
      float xl = (b[0] * (1.0f - fr) + b[SFX_CH] * fr) * v->gl;
      float xr = (b[1] * (1.0f - fr) + b[SFX_CH + 1] * fr) * v->gr;
      if (g_audio_trace_on && v->trace) audio_trace_sample(v->trace, g_audio_trace_frames + f, v->pos, xl, xr);
      // KEEP voices bypass the duck by riding the lead bus WITHOUT feeding the
      // sidechain: they are neither attenuated nor allowed to attenuate.
      if (v->prio == SFX_P_LEAD)      { ll += xl; lr += xr; }
      else if (v->prio == SFX_P_KEEP) { kl += xl; kr += xr; }
      else                            { sl += xl; sr += xr; }
      v->pos += v->rate * g_dev_rate;
      live++;
    }
    if (live > g_voice_peak) g_voice_peak = live;
    float wet_l, wet_r;
    audio_wet_sample(sl + .10f * kl, sr + .10f * kr, &wet_l, &wet_r);
    sl += wet_l; sr += wet_r;
    // Sidechain duck — where the confirmation gets to be the loudest thing in the
    // game. Arithmetic, not taste: one shot already puts the bus at 0.68 against a
    // 0.92 limiter threshold, so a confirmation loud enough to dominate by LEVEL
    // drives the master limiter 4-8 dB down, which makes everything quieter WITH it
    // — including its own tail — and pumps the whole arena for 170 ms. Ducking the
    // non-lead bus buys the same contrast and moves no peak.
    // Attack ~2.5 ms so the duck is already down when the confirmation's transient
    // arrives; release ~130 ms so it lifts under the decay instead of chattering.
    // Both device-rate relative: a coefficient in samples is a different time at
    // 44.1 kHz.
    float lm = fabsf(ll) > fabsf(lr) ? fabsf(ll) : fabsf(lr);
    float d_atk = 0.0084f * g_dev_rate, d_rel = 0.00016f * g_dev_rate;
    if (lm > g_duck_env) g_duck_env += (lm - g_duck_env) * d_atk;
    else                 g_duck_env += (lm - g_duck_env) * d_rel;
    // A DEEPER FLOOR, AND THE LAST FREE LEVER IN THE SYSTEM: in a real 20-bot fight the
    // bus is already 2.8 dB compressed before the confirmation arrives, so there is no
    // level headroom left and SFX_LEAD_GAIN is at the limiter's ceiling.
    float duck = 1.0f - 1.55f * g_duck_env;
    if (duck < 0.32f) duck = 0.32f;              // -9.9 dB, never silence
    sl = (sl * duck + ll + kl) * vol * 1.4142f * g_pause_env;
    sr = (sr * duck + lr + kr) * vol * 1.4142f * g_pause_env;
    if (duck < g_duck_min) g_duck_min = duck;
    // Master limiter, not a hard clamp.
    float mag = fabsf(sl) > fabsf(sr) ? fabsf(sl) : fabsf(sr);
    float want = mag > 0.92f ? 0.92f / mag : 1.0f;
    float rel = 0.00012f * g_dev_rate;   // ~170 ms whatever the device runs at
    if (want < g_lim_g) g_lim_g += (want - g_lim_g) * 0.55f;      // fast down
    else                g_lim_g += (want - g_lim_g) * rel;        // slow up
    if (g_lim_g < g_lim_min) g_lim_min = g_lim_g;
    sl *= g_lim_g; sr *= g_lim_g;
    sl *= out_vol; sr *= out_vol;
    // The safety net is a soft knee, not a clamp.
    for (int c = 0; c < 2; c++) {
      float *x = c ? &sr : &sl;
      *x = audio_soft_knee(*x);
    }
    // Address the first two channels and ZERO the rest.
    if (channels == 1) {
      // The knee above bounded sl and sr PER CHANNEL, so their centred sum can
      // still reach ~1.34 — the no-clip-by-construction guarantee has to be
      // re-applied to the one sample this endpoint actually outputs.
      float m = (sl + sr) * 0.7071f;
      m = audio_soft_knee(m);
      out[f] = m;
    } else {
      for (int c = 0; c < channels; c++)
        out[f * (uint32_t)channels + (uint32_t)c] =
            c == 0 ? sl : (c == 1 ? sr : 0.0f);
    }
  }
  if (g_audio_trace_on) g_audio_trace_frames += frames;
}
