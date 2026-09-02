// =============================================================================
// audio/route.inc  —  event -> sound routing, sfx_prio, the mixer and its
// SPSC trigger ring.
// Proof: sfxlog, mixdown. One TU: included once by game.c, in definition
// order — moving a definition out is a semantic change.
// Contract: TRIG_RING (64) is the only state shared with the audio thread,
// classic SPSC (uint32 wrap, relaxed/acquire/release); MAX_VOICES 96 with
// priority classes. mixdown clears voices+triggers at start — mixer-internal
// paths are not bus-testable headless.
// =============================================================================
// ---------------------------------------------------------------------------
// Audio routing. WHICH sound a game event becomes, how loud, how far to the
// side and how late, is ordinary presentation logic and lives outside the platform
// guard: both native builds feed it to the mixer below, and the headless harness feeds
// it to `sfxlog` and, through that same mixer, to `mixdown` — which is the only way any
// of this gets tested at all, since there is no way to listen in the container the game
// is developed in.

// A voice carries its own L/R gains and a start DELAY. Both matter: panning is how a
// shot from behind-left becomes information rather than noise, and the delay is how the
// hit confirmation gets heard at all — see audio_on_event.
typedef struct {
  int active; const float *buf; int len;
  float pos, rate, gl, gr; int delay;
  int prio;                  // SFX_P_* — who ducks whom
  int dying;                 // fading out after a kill trigger — see audio_kill
  int tag;                   // 1 = the player's OWN voice: a kill trigger only
                             // fades matching tags, so cancelling your reload
                             // cannot silence a bot's on the same buffer
} voice_t;

// Three priorities, and the middle one exists because the duck below would
// otherwise take 6.7 dB off the two cues a player cannot afford to miss.
//   LEAD   your own hit/kill confirmation. Ducks everything else, is never
//          stolen, is never dropped.
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
// hard). One voice is 56 bytes of state; the pool costs nothing to keep wide.
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

// `kill` inverts the trigger: instead of starting a voice for `id`, fade out
// every voice playing it WITH THE SAME TAG. It rides the same SPSC ring
// because the voice pool may only ever be mutated on the mixer thread.
typedef struct { int id; float gain, rate, pan; int delay; int kill, tag; } sfx_trig_t;
#define TRIG_RING 64
static sfx_trig_t     g_trig[TRIG_RING];
static _Atomic uint32_t g_trig_w, g_trig_r;
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

static void audio_play_tag(int id, float gain, float rate, float pan,
                           float delay_s, int tag) {
  // Anything this quiet is below the noise floor of a firefight; spawning it
  // only costs a voice that a nearer sound needs. `sfxlog` measured 64 sounds
  // wanting to be alive at once in a 20-bot fight against a 24-voice mixer,
  // and most of them were distant shots at gain 0.02.
  if (gain < 0.035f) return;
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
  uint32_t w = atomic_load_explicit(&g_trig_w, memory_order_relaxed);
  uint32_t r = atomic_load_explicit(&g_trig_r, memory_order_acquire);
  if (w - r >= TRIG_RING) { g_trig_flooded++; return; }  // flooded: drop,
                                                         // never block the game
  g_trig[w % TRIG_RING] = (sfx_trig_t){id, gain, rate, f_clamp(pan, -1, 1),
                                       (int)(delay_s * SFX_HZ), 0, tag};
  atomic_store_explicit(&g_trig_w, w + 1, memory_order_release);
}
static void audio_play(int id, float gain, float rate, float pan, float delay_s) {
  audio_play_tag(id, gain, rate, pan, delay_s, 0);
}

// Fade out every voice playing `id`.
static void audio_kill(int id, int tag) {
  uint32_t w = atomic_load_explicit(&g_trig_w, memory_order_relaxed);
  uint32_t r = atomic_load_explicit(&g_trig_r, memory_order_acquire);
  if (w - r >= TRIG_RING) { g_trig_flooded++; return; }
  g_trig[w % TRIG_RING] = (sfx_trig_t){.id = id, .kill = 1, .tag = tag};
  atomic_store_explicit(&g_trig_w, w + 1, memory_order_release);
}

static uint64_t g_rng_audio = 0xA0D10;
// The confirmation offset depends on WHICH weapon fired, and neither EV_HIT nor EV_KILL
// carries it.
static int      g_last_wpn;  // weapon of the player's most recent shot
static long     g_multi_t = -100000;  // tick of the player's last kill...
static int      g_multi_n;            // ...and how many are inside the window
#define MULTI_WIN 360        // ticks: 3 s. NOT the killfeed's window — that is
                             // KILLFEED_TICKS 480 (4 s) — this is the genre's
                             // own double-kill window, which is shorter.
static v3       g_listen;    // player eye
static v3       g_listen_r;  // ...and which way is right, for panning

// Where a sound sits relative to the listener: -1 hard left, +1 hard right, plus the
// distance it travelled.
static float audio_place(v3 at, float *dist) {
  v3 d = v3_sub(at, g_listen);
  float len = sqrtf(v3_dot(d, d));
  *dist = len;
  if (len < 0.25f) return 0.0f;
  return f_clamp(v3_dot(d, g_listen_r) / len, -1.0f, 1.0f) * 0.85f;
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
    if (e->src == g_local_ent && e->k <= 0) break;
    if (e->src == g_local_ent) {
      // DELAYED, and that is the whole trick.
      audio_play(e->type == EV_HIT_HEAD ? SFX_HIT_HEAD : SFX_HIT, SFX_LEAD_GAIN,
                 rng_rangef(&g_rng_audio, 0.99f, 1.01f), 0.0f,
                 WPN_DEF[g_last_wpn].sfx_hit_t);
    } else if (e->dst == g_local_ent) {
      // Taking a round: its own dark sound, panned to where it came from, so the audio
      // tells you which way to turn.
      if (e->k <= 0) break;
      float dist, pan = audio_place(e->a, &dist);
      audio_play(SFX_HURT, 1.0f, rng_rangef(&g_rng_audio, 0.94f, 1.06f),
                 pan * 0.8f, 0.0f);
    }
    break;
  }
  case EV_KILL:
    if (e->src == g_local_ent) {
      // MULTI-KILL ESCALATION, for zero new synthesis.
      if (g_tick - g_multi_t > MULTI_WIN) g_multi_n = 0;
      g_multi_t = g_tick;
      static const float MRATE[3] = {1.0f, 1.122f, 1.260f};
      audio_play(e->k ? SFX_KILL_HEAD : SFX_KILL, SFX_LEAD_GAIN,
                 MRATE[g_multi_n], 0.0f, WPN_DEF[g_last_wpn].sfx_hit_t);
      if (g_multi_n < 2) g_multi_n++;
    }
    else if (e->dst == g_local_ent)
      audio_play(SFX_HURT, 1.0f, 0.72f, 0.0f, 0.0f);   // your own death: dull
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
      // tag 1 = player-owned: the cancel's audio_kill fades only this voice.
      audio_play_tag(rid, 0.9f,
                     rng_rangef(&g_rng_audio, 0.975f, 1.025f), -0.12f, 0.0f, 1);
    } else {
      float dist, pan = audio_place(e->a, &dist);
      if (dist < 14.0f)
        audio_play_tag(rid,
                       0.75f * f_clamp(1.0f / (1.0f + dist * dist * 0.028f), 0.0f, 1.0f),
                       rng_rangef(&g_rng_audio, 0.96f, 1.04f), pan, dist / 343.0f,
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
                 WPN_DEF[g_last_wpn].sfx_hit_t + 0.38f);
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
    if (e->k) break;                 // a through hole: see the push site
    float dist, pan = audio_place(e->a, &dist);
    // A ROUND HITTING THE WALL BESIDE YOUR HEAD AND ONE 20 M AWAY SOUNDED THE SAME. The
    // old law spans 7.6 dB from 0.5 m to 20 m — measured over 992 logged impacts only
    // 21 (2.1%) ever reached its 0.85 clamp — where the physics is ~32 dB. Split law:
    // beyond the seam the old curve, which is tuned and which most impacts live on;
    // inside it an inverse-square, giving 0.5 m -> 0.98.
    if (dist < 22.0f)
      audio_play(SFX_IMPACT,
                 dist < 1.33f
                     ? f_clamp(1.0f / (1.0f + 0.10f * dist * dist), 0.08f, 1.0f)
                     : f_clamp(1.2f / (1.0f + dist * 0.12f), 0.10f, 0.85f),
                 rng_rangef(&g_rng_audio, 0.90f, 1.12f), pan, dist / 343.0f);
    break;
  }
  case EV_RESPAWN:
    break;
  }
}

// The near-miss crack is spawned from the SHOT event rather than from its own event
// type: it is a property of the round's whole flight segment, which only EV_FIRE
// carries.
static long  g_whizz_t = -1000;
static float g_whizz_g;
#define WHIZZ_HOLD 11        // ticks: 90 ms
// 4.2 m, not 3.0.
#define WHIZZ_MISS 4.2f

static void audio_shot_world(const event_t *e) {
  if (e->type != EV_FIRE) return;
  // EXCLUDE ONLY ROUNDS THAT HIT THE LISTENER. The old guard rejected `k == 2 || k ==
  // 3`, i.e.
  if (e->src != g_local_ent && e->dst != g_local_ent) {
    // A round that went past you. The segment is the ballistic one: it begins at the
    // SHOOTER'S EYE, where the round was traced from, not at the rifle's barrel tip —
    // half a metre of it lies behind a shoulder nobody stands next to, and the 0.5 m
    // end margins below already refuse a crack from inside the shooter.
    v3 start = v3_sub(e->a, v3_scale(e->b, e->f));
    v3 ax = v3_norm(e->b);
    v3 d = v3_sub(g_listen, start);
    float t = v3_dot(d, ax);
    v3 perp = v3_sub(d, v3_scale(ax, t));
    float miss = sqrtf(v3_dot(perp, perp));
    if (t > 0.5f && t < e->f - 0.5f && miss < WHIZZ_MISS) {
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

static void audio_event(const event_t *e) { audio_on_event(e); audio_shot_world(e); }

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

static void audio_mix(float *out, uint32_t frames, int channels) {
  uint32_t r = atomic_load_explicit(&g_trig_r, memory_order_relaxed);
  uint32_t w = atomic_load_explicit(&g_trig_w, memory_order_acquire);
  for (; r != w; r++) {
    sfx_trig_t t = g_trig[r % TRIG_RING];
    if (t.kill) {  // fade out, don't cut — see audio_kill; tags must match
      for (int i = 0; i < MAX_VOICES; i++)
        if (g_voices[i].active && g_voices[i].buf == g_sfx_buf[t.id] &&
            g_voices[i].tag == t.tag) {
          // Still inside its scheduled delay = never was audible: freeing the
          // slot outright cannot click, and a fade would let it start first.
          if (g_voices[i].delay > 0) g_voices[i].active = 0;
          else g_voices[i].dying = 1;
        }
      continue;
    }
    // Equal-power pan. Linear panning drops 3 dB in the middle, which on a sound
    // sweeping across the field reads as a hole in the centre.
    int cap = sfx_class_cap(t.id);
    if (cap > 0) {
      // AUDIBLE voices only.
      int live = 0;
      for (int i = 0; i < MAX_VOICES; i++)
        if (g_voices[i].active && !g_voices[i].dying && g_voices[i].delay == 0
            && g_voices[i].buf == g_sfx_buf[t.id]) live++;
      if (live >= cap) { g_trig_capped++; continue; }
    }
    float gl = t.gain * sqrtf(0.5f * (1.0f - t.pan));
    float gr = t.gain * sqrtf(0.5f * (1.0f + t.pan));
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; i++)
      if (!g_voices[i].active) { slot = i; break; }
    int prio = sfx_prio(t.id);
    int lead = prio == SFX_P_LEAD;
    if (slot < 0) {
      // Steal, but only from a voice that is already MOSTLY PLAYED and quieter than the
      // newcomer.
      float worst = 1e9f;
      for (int i = 0; i < MAX_VOICES; i++) {
        if (g_voices[i].prio == SFX_P_LEAD) continue;
        if (g_voices[i].prio == SFX_P_KEEP && prio == SFX_P_NORM) continue;
        float g = g_voices[i].gl + g_voices[i].gr;
        if (!lead && g_voices[i].pos < (float)g_voices[i].len * 0.55f) continue;
        if (g < worst) { worst = g; slot = i; }
      }
      if (slot < 0 || (!lead && worst > gl + gr)) { g_trig_dropped++; continue; }
    }
    // The delay arrives in 48 kHz samples and is consumed one per DEVICE frame, so on a
    // 44.1 kHz device every scheduled offset ran 8.8% long — including the propagation
    // delays that carry distance and the confirmation offsets that keep the hit tick
    // out from under the shot.
    g_voices[slot] = (voice_t){1, g_sfx_buf[t.id], g_sfx_len[t.id],
                               0, t.rate, gl, gr,
                               (int)((float)t.delay / (g_dev_rate > 0.01f
                                                       ? g_dev_rate : 1.0f)),
                               prio, 0, t.tag};
  }
  atomic_store_explicit(&g_trig_r, r, memory_order_release);

  // THE VOLUME KNOB IS AN OUTPUT TRIM, NOT A DRIVE. Applied before the limiter, turning
  // it up fed the limiter harder instead of making anything louder.
  const float vol = VOL_REF;
  const float out_vol = g_cfg.vol / VOL_REF;
  int paused = atomic_load_explicit(&g_audio_pause, memory_order_relaxed);
  for (uint32_t f = 0; f < frames; f++) {
    // Pause envelope: fade to silence, then hold every voice in place.
    if (paused) {
      g_pause_env *= 0.9945f;
      if (g_pause_env < 1e-4f) {  // fully faded: freeze voices, emit silence
        if (channels == 1) out[f] = 0;
        else for (int c = 0; c < channels; c++)
          out[f * (uint32_t)channels + (uint32_t)c] = 0;
        continue;
      }
    } else if (g_pause_env < 1.0f) {
      g_pause_env += (1.0f - g_pause_env) * 0.0055f;
      if (g_pause_env > 0.9995f) g_pause_env = 1.0f;
    }
    float sl = 0, sr = 0, ll = 0, lr = 0, kl = 0, kr = 0;
    int live = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
      voice_t *v = &g_voices[i];
      if (!v->active) continue;
      if (v->delay > 0) { v->delay--; continue; }  // scheduled, not yet audible
      if (v->dying) {  // ~35 ms exponential fade at 48 kHz, then the slot frees
        v->gl *= 0.9945f;
        v->gr *= 0.9945f;
        if (v->gl + v->gr < 1e-4f) { v->active = 0; continue; }
      }
      int ip = (int)v->pos;
      if (ip + 1 >= v->len) { v->active = 0; continue; }
      float fr = v->pos - (float)ip;
      const float *b = v->buf + ip * SFX_CH;
      float xl = (b[0] * (1.0f - fr) + b[SFX_CH] * fr) * v->gl;
      float xr = (b[1] * (1.0f - fr) + b[SFX_CH + 1] * fr) * v->gr;
      // KEEP voices bypass the duck by riding the lead bus WITHOUT feeding the
      // sidechain: they are neither attenuated nor allowed to attenuate.
      if (v->prio == SFX_P_LEAD)      { ll += xl; lr += xr; }
      else if (v->prio == SFX_P_KEEP) { kl += xl; kr += xr; }
      else                            { sl += xl; sr += xr; }
      v->pos += v->rate * g_dev_rate;
      live++;
    }
    if (live > g_voice_peak) g_voice_peak = live;
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
      float a = *x < 0 ? -*x : *x;
      if (a > 0.90f) {
        float over = a - 0.90f;
        a = 0.90f + over / (1.0f + over * 10.0f);
        *x = *x < 0 ? -a : a;
      }
    }
    // Address the first two channels and ZERO the rest.
    if (channels == 1) {
      // The knee above bounded sl and sr PER CHANNEL, so their centred sum can
      // still reach ~1.34 — the no-clip-by-construction guarantee has to be
      // re-applied to the one sample this endpoint actually outputs.
      float m = (sl + sr) * 0.7071f;
      float a = m < 0 ? -m : m;
      if (a > 0.90f) {
        float over = a - 0.90f;
        a = 0.90f + over / (1.0f + over * 10.0f);
        m = m < 0 ? -a : a;
      }
      out[f] = m;
    } else {
      for (int c = 0; c < channels; c++)
        out[f * (uint32_t)channels + (uint32_t)c] =
            c == 0 ? sl : (c == 1 ? sr : 0.0f);
    }
  }
}

