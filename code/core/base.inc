// =============================================================================
// core/base.inc  —  GL 3.3 loader (GL_FUNC_LIST X-macro), math (v3/mat4,
// f_clamp), PCG32 streams and bounded monotonic diagnostic samples.
// Proof: framepace and native diagnostic reports.
// One TU: included once by game.c, in definition order — moving a definition
// out is a semantic change.
// Contract: f_is_nan/f_is_nonfinite/f_is_nonfinite64 use bit tests in this
// binary — isnan()/isfinite()/`x != x` compile away under -ffast-math's
// -ffinite-math-only.
// =============================================================================
// ---------------------------------------------------------------------------
// GL 3.3 core loader — only what we use beyond the GL 1.1 headers.
// Pointer names match the spec names; nothing here collides with the 1.1
// prototypes in either platform's GL/gl.h. glActiveTexture is the one
// exception (Mesa's gl.h declares it) and is loaded as gl_active_texture
// below; units 0..3 are in use — see the renderer's texture-unit contract.
// ---------------------------------------------------------------------------

#ifdef _WIN32  // Linux Mesa gl.h pulls in glext.h which already has these
#include <stddef.h>
typedef char      GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef uint64_t  GLuint64;
typedef struct __GLsync *GLsync;
#endif

#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_ARRAY_BUFFER         0x8892
#define GL_STATIC_DRAW          0x88E4
#define GL_STREAM_DRAW          0x88E0
#define GL_FRAMEBUFFER          0x8D40
#define GL_RENDERBUFFER         0x8D41
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT24    0x81A6
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_MAX_SAMPLES          0x8D57
#define GL_RENDERBUFFER_SAMPLES 0x8CAB
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_R8                   0x8229
#define GL_RED_CORE             0x1903
#define GL_DEPTH_COMPONENT      0x1902
#define GL_CLAMP_TO_BORDER      0x812D
#define GL_TEXTURE_BORDER_COLOR 0x1004
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT    0x00000001
// The screen-space passes (prepass normals, SSAO, the composite) read two
// textures at once, so a second unit exists after all. The enums are guarded:
// Mesa's gl.h already carries the GL 1.3 set, MinGW's does not.
#ifndef GL_TEXTURE0
  #define GL_TEXTURE0           0x84C0
#endif
#ifndef GL_TEXTURE1
  #define GL_TEXTURE1           0x84C1
#endif
#ifndef GL_TEXTURE2
  #define GL_TEXTURE2           0x84C2
#endif
#ifndef GL_TEXTURE3
  #define GL_TEXTURE3           0x84C3
#endif
#ifndef GL_RG
  #define GL_RG                 0x8227
#endif
#ifndef GL_TIME_ELAPSED
  #define GL_TIME_ELAPSED       0x88BF
#endif
#ifndef GL_QUERY_RESULT
  #define GL_QUERY_RESULT       0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
  #define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_WAIT_FAILED
  #define GL_WAIT_FAILED 0x911D
  #define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_RG16F
  #define GL_RG16F              0x822F
#endif
#ifndef GL_HALF_FLOAT
  #define GL_HALF_FLOAT         0x140B
#endif

#define GL_FUNC_LIST(X) \
  X(GLuint, glCreateShader, (GLenum)) \
  X(void,   glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
  X(void,   glCompileShader, (GLuint)) \
  X(void,   glGetShaderiv, (GLuint, GLenum, GLint *)) \
  X(void,   glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(GLuint, glCreateProgram, (void)) \
  X(void,   glAttachShader, (GLuint, GLuint)) \
  X(void,   glLinkProgram, (GLuint)) \
  X(void,   glGetProgramiv, (GLuint, GLenum, GLint *)) \
  X(void,   glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
  X(void,   glDeleteShader, (GLuint)) \
  X(void,   glUseProgram, (GLuint)) \
  X(GLint,  glGetUniformLocation, (GLuint, const GLchar *)) \
  X(void,   glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *)) \
  X(void,   glUniform1i, (GLint, GLint)) \
  X(void,   glUniform1f, (GLint, GLfloat)) \
  X(void,   glUniform2f, (GLint, GLfloat, GLfloat)) \
  X(void,   glUniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
  X(void,   glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  X(void,   glGenVertexArrays, (GLsizei, GLuint *)) \
  X(void,   glBindVertexArray, (GLuint)) \
  X(void,   glGenBuffers, (GLsizei, GLuint *)) \
  X(void,   glBindBuffer, (GLenum, GLuint)) \
  X(void,   glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
  X(void,   glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
  X(void,   glEnableVertexAttribArray, (GLuint)) \
  X(void,   glGenFramebuffers, (GLsizei, GLuint *)) \
  X(void,   glBindFramebuffer, (GLenum, GLuint)) \
  X(void,   glGenRenderbuffers, (GLsizei, GLuint *)) \
  X(void,   glBindRenderbuffer, (GLenum, GLuint)) \
  X(void,   glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
  X(void,   glGetRenderbufferParameteriv, (GLenum, GLenum, GLint *)) \
  X(void,   glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
  X(void,   glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
  X(GLenum, glCheckFramebufferStatus, (GLenum)) \
  X(void,   glDeleteFramebuffers, (GLsizei, const GLuint *)) \
  X(void,   glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
  X(void,   glRenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
  X(void,   glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
  X(void,   glGenSamplers, (GLsizei, GLuint *)) \
  X(void,   glBindSampler, (GLuint, GLuint)) \
  X(void,   glSamplerParameteri, (GLuint, GLenum, GLint)) \
  X(void,   glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
  X(void,   glGenQueries, (GLsizei, GLuint *)) \
  X(void,   glDeleteQueries, (GLsizei, const GLuint *)) \
  X(void,   glGetQueryObjectiv, (GLuint, GLenum, GLint *)) \
  X(void,   glBeginQuery, (GLenum, GLuint)) \
  X(void,   glEndQuery, (GLenum)) \
  X(void,   glGetQueryObjectui64v, (GLuint, GLenum, GLuint64 *)) \
  X(GLsync, glFenceSync, (GLenum, GLbitfield)) \
  X(GLenum, glClientWaitSync, (GLsync, GLbitfield, GLuint64)) \
  X(void,   glDeleteSync, (GLsync))

#define X(ret, name, args) typedef ret (APIENTRY *pfn_##name) args; static pfn_##name name;
GL_FUNC_LIST(X)
#undef X

typedef void *(*gl_loader_fn)(const char *);

// glActiveTexture is GL 1.3 and Mesa's gl.h DECLARES it, so a pointer under the
// spec name would collide with that prototype on Linux; it is loaded under its
// own name instead. Two units are used: 0 (everything that was here before)
// and 1 (the AO texture under the world pass, the scene under the composite).
typedef void (APIENTRY *pfn_gl_active_texture)(GLenum);
static pfn_gl_active_texture gl_active_texture;

static void gl_load_functions(gl_loader_fn load) {
#define X(ret, name, args) name = (pfn_##name)load(#name); \
  if (!name) fatal("GL 3.3 function missing: " #name);
  GL_FUNC_LIST(X)
#undef X
  gl_active_texture = (pfn_gl_active_texture)load("glActiveTexture");
  if (!gl_active_texture) fatal("GL 3.3 function missing: glActiveTexture");
}

// ---------------------------------------------------------------------------
// Math — column-major mat4, y-up right-handed, yaw 0 looks down -z.
// ---------------------------------------------------------------------------

typedef union { struct { float x, y, z; }; float e[3]; } v3;

// One spelling each, so no two call sites differ by an ULP. GLSL keeps its own
// literals — a shader cannot see a C macro.
#define F_PI    3.14159265f
#define F_TAU   6.2831853f     // full turn
#define F_PI_2  1.57079633f    // quarter turn
#define F_3PI_2 4.71238898f
#define F_PI_4  0.7853982f     // 45 deg threshold, 8-way cover fan step

static v3    v3_add(v3 a, v3 b) { return (v3){{a.x + b.x, a.y + b.y, a.z + b.z}}; }
static v3    v3_scale(v3 a, float s) { return (v3){{a.x * s, a.y * s, a.z * s}}; }
static v3    v3_lerp(v3 a, v3 b, float t) { return v3_add(a, v3_scale(v3_add(b, v3_scale(a, -1.0f)), t)); }
static float v3_dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static v3    v3_cross(v3 a, v3 b) {
  return (v3){{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}};
}
// DO NOT REMOVE THE volatile.
static v3 v3_norm(v3 a) {
  float l = sqrtf(v3_dot(a, a));
  if (!(l > 1e-6f)) return (v3){{0, 0, 0}};
  volatile float inv = 1.0f / l;
  return v3_scale(a, inv);
}
static v3    v3_sub(v3 a, v3 b) { return (v3){{a.x - b.x, a.y - b.y, a.z - b.z}}; }
static float v3_len(v3 a) { return sqrtf(v3_dot(a, a)); }
static float f_clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
// GLSL's smoothstep, so a ramp authored in a shader and the same ramp evaluated
// in C are the same curve rather than two hand-fitted ones.
static float f_smoothstep(float e0, float e1, float x) {
  float t = f_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// NaN/inf tests that survive -ffast-math, which folds isnan()/isfinite()/v != v to a
// constant.
static int f_is_nan(float v) {
  uint32_t b; memcpy(&b, &v, sizeof b); return (b & 0x7FFFFFFFu) > 0x7F800000u;
}
static int f_is_nonfinite(float v) {
  uint32_t b; memcpy(&b, &v, sizeof b); return (b & 0x7FFFFFFFu) >= 0x7F800000u;
}
// Monotonic diagnostics keep double timer validation bit based under fast math.
static int f_is_nonfinite64(double v) {
  uint64_t b; memcpy(&b, &v, sizeof b);
  return (b & UINT64_C(0x7fffffffffffffff)) >= UINT64_C(0x7ff0000000000000);
}

// Output geometry is independent of native APIs. Endpoints and areas promote
// before arithmetic so negative desktop origins and large dimensions stay valid.
#define APP_OUTPUT_MAX 64
typedef struct {
  int x, y, w, h, primary;
  uint64_t id;
} app_output_t;
static int app_output_valid(const app_output_t *output) {
  return output->w > 0 && output->h > 0 && output->id != 0;
}
static int app_output_tie(const app_output_t *candidate, const app_output_t *current,
                          uint64_t previous_id) {
  int a = candidate->id == previous_id, b = current->id == previous_id;
  if (a != b) return a > b;
  if (!!candidate->primary != !!current->primary)
    return !!candidate->primary > !!current->primary;
  return candidate->id < current->id;
}
[[maybe_unused]] static int app_output_select(const app_output_t *outputs, int count,
    int x, int y, int w, int h, uint64_t previous_id) {
  if (!outputs || count < 1 || count > APP_OUTPUT_MAX || w < 0 || h < 0 ||
      ((w == 0) != (h == 0))) return -1;
  int best = -1;
  int64_t best_overlap = 0, best_gap = 0;
  int64_t qx = x, qy = y, qr = (int64_t)x + w, qb = (int64_t)y + h;
  for (int i = 0; i < count; i++) {
    const app_output_t *o = &outputs[i];
    if (!app_output_valid(o)) continue;
    for (int j = 0; j < i; j++)
      if (app_output_valid(&outputs[j]) && outputs[j].id == o->id) return -1;
    int64_t left = o->x, top = o->y;
    int64_t right = left + o->w, bottom = top + o->h;
    int64_t overlap = 0;
    if (!w) overlap = qx >= left && qx < right && qy >= top && qy < bottom;
    else {
      int64_t width = (qr < right ? qr : right) - (qx > left ? qx : left);
      int64_t height = (qb < bottom ? qb : bottom) - (qy > top ? qy : top);
      if (width > 0 && height > 0) overlap = width * height;
    }
    int64_t gap_x = qr < left ? left - qr : qx > right ? qx - right : 0;
    int64_t gap_y = qb < top ? top - qb : qy > bottom ? qy - bottom : 0;
    int64_t gap = gap_x + gap_y;
    int better = best < 0 || overlap > best_overlap ||
        (overlap == best_overlap && ((!overlap && gap < best_gap) ||
         ((overlap || gap == best_gap) && app_output_tie(o, &outputs[best], previous_id))));
    if (better) { best = i; best_overlap = overlap; best_gap = gap; }
  }
  return best;
}
[[maybe_unused]] static double app_mode_hz(uint64_t dot_clock, unsigned h_total,
    unsigned v_total, int interlace, int double_scan) {
  if (!dot_clock || !h_total || !v_total || (interlace != 0 && interlace != 1) ||
      (double_scan != 0 && double_scan != 1)) return 0.0;
  double hz = (double)dot_clock / (double)h_total / (double)v_total;
  if (interlace) hz *= 2.0;
  if (double_scan) hz *= 0.5;
  return hz;
}
// A cap is policy; refresh remains actual measured output metadata or unknown.
[[maybe_unused]] static double app_limit_hz(int requested_cap, int vsync,
                                           double refresh_hz, int *reason) {
  if (reason) *reason = 0;
  if (requested_cap > 0) {
    if (reason) *reason = 1;
    return (double)requested_cap;
  }
  if (!vsync) return 0.0;
  if (!f_is_nonfinite64(refresh_hz) && refresh_hz > 0.0) {
    if (reason) *reason = 2;
    return refresh_hz;
  }
  if (reason) *reason = 3;
  return 60.0;
}
[[maybe_unused]] static double app_limit_remaining(double frame_start, double now,
                                                    double cap_hz, int *clock_bad) {
  if (clock_bad) *clock_bad = 0;
  if (f_is_nonfinite64(frame_start) || f_is_nonfinite64(now) ||
      f_is_nonfinite64(cap_hz) || frame_start < 0.0 || now < frame_start || cap_hz < 0.0) {
    if (clock_bad) *clock_bad = 1;
    return 0.0;
  }
  if (cap_hz == 0.0) return 0.0;
  double target = 1.0 / cap_hz;
  if (f_is_nonfinite64(target) || target > 86400.0) {
    if (clock_bad) *clock_bad = 1;
    return 0.0;
  }
  double remaining = target - (now - frame_start);
  return remaining > 0.0 ? remaining : 0.0;
}

#define DIAG_SAMPLES 32768
#define DIAG_PLATFORM_EVENTS 64
#define DIAG_GPU_PASSES 12

typedef enum {
  DIAG_WALL, DIAG_DELTA, DIAG_INPUT, DIAG_SIM, DIAG_SCENE, DIAG_SUBMIT,
  DIAG_FENCE, DIAG_SWAP, DIAG_CAP, DIAG_SLEEP, DIAG_INACTIVE, DIAG_DISCARDED,
  DIAG_BACKLOG, DIAG_CHANNELS
} diag_channel_t;
static const char *const DIAG_NAME[DIAG_CHANNELS] = {
  "wall", "input_delta", "input", "sim", "scene", "submit", "fence",
  "swap", "cap", "sleep", "inactive", "discarded", "backlog"
};
typedef struct {
  int logical_w, logical_h, drawable_w, drawable_h;
  int output_x, output_y, output_w, output_h;
  double refresh_hz, effective_cap_hz;
  uint64_t output_id;
  int rate_source, cap_reason;
  int swap_requested, swap_accepted, swap_queried, focused, output_source, fullscreen;
} diag_platform_t;
typedef struct {
  uint64_t id;
  double start, value[DIAG_CHANNELS];
  unsigned mask;
  int active, measured;
} diag_sample_t;
typedef struct {
  uint64_t frame;
  double value[DIAG_GPU_PASSES];
  unsigned mask;
} diag_gpu_sample_t;
static struct {
  int enabled, open;
  double collect_after;
  uint64_t serial, total, evicted, timer_errors, missing_frames;
  uint64_t fence_timeouts, fence_errors, gpu_skipped, gpu_unavailable, gpu_errors;
  uint64_t swap_errors, platform_errors;
  unsigned count, next, gpu_count, gpu_next;
  uint64_t gpu_total, gpu_evicted;
  diag_sample_t current, samples[DIAG_SAMPLES];
  diag_gpu_sample_t gpu[DIAG_SAMPLES];
  diag_platform_t platform, platform_events[DIAG_PLATFORM_EVENTS];
  uint64_t platform_frame[DIAG_PLATFORM_EVENTS], platform_dropped;
  unsigned platform_count;
  char vendor[160], renderer[160], version[160];
  int software, actual_msaa, initial_msaa, msaa_seen, profile_pinned;
  char initial_profile[8192], last_profile[8192], initial_profile_hash[65];
  uint64_t profile_changes, profile_first_change, profile_last_change;
  uint64_t msaa_changes, msaa_first_change, msaa_last_change;
} g_diag;

[[maybe_unused]] static double diag_now(void) {
#ifdef _WIN32
  static LARGE_INTEGER frequency;
  LARGE_INTEGER now;
  if ((!frequency.QuadPart && !QueryPerformanceFrequency(&frequency)) ||
      frequency.QuadPart <= 0 || !QueryPerformanceCounter(&now)) {
    if (g_diag.enabled) g_diag.timer_errors++;
    return -1.0;
  }
  return (double)now.QuadPart / (double)frequency.QuadPart;
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    if (g_diag.enabled) g_diag.timer_errors++;
    return -1.0;
  }
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}
static int diag_seconds_valid(double value) {
  return !f_is_nonfinite64(value) && value >= 0.0;
}
static void diag_frame_begin_at(double now) {
  if (!g_diag.enabled) return;
  if (g_diag.open) g_diag.missing_frames++;
  g_diag.current = (diag_sample_t){.id = ++g_diag.serial, .start = now,
      .active = 1, .measured = diag_seconds_valid(now) && now >= g_diag.collect_after};
  if (!diag_seconds_valid(now)) g_diag.timer_errors++;
  g_diag.open = 1;
}
[[maybe_unused]] static void diag_frame_begin(void) {
  if (g_diag.enabled) diag_frame_begin_at(diag_now());
}
static void diag_add(diag_channel_t channel, double seconds) {
  if (!g_diag.enabled || !g_diag.open) return;
  if (channel < 0 || channel >= DIAG_CHANNELS || !diag_seconds_valid(seconds) ||
      seconds > 86400.0) {
    g_diag.timer_errors++;
    return;
  }
  double sum = g_diag.current.value[channel] + seconds;
  if (f_is_nonfinite64(sum) || sum > 86400.0) { g_diag.timer_errors++; return; }
  g_diag.current.value[channel] = sum;
  g_diag.current.mask |= 1u << channel;
}
[[maybe_unused]] static void diag_frame_active(int active) {
  if (g_diag.enabled && g_diag.open) g_diag.current.active = active != 0;
}
static void diag_frame_end_at(double now) {
  if (!g_diag.enabled || !g_diag.open) return;
  if (g_diag.current.active) {
    if (diag_seconds_valid(now) && diag_seconds_valid(g_diag.current.start))
      diag_add(DIAG_WALL, now - g_diag.current.start);
    else g_diag.timer_errors++;
  }
  if (g_diag.current.measured) {
    g_diag.samples[g_diag.next] = g_diag.current;
    g_diag.next = (g_diag.next + 1) % DIAG_SAMPLES;
    if (g_diag.count < DIAG_SAMPLES) g_diag.count++;
    else g_diag.evicted++;
    g_diag.total++;
  }
  g_diag.open = 0;
}
[[maybe_unused]] static void diag_frame_end(void) {
  if (g_diag.enabled) diag_frame_end_at(diag_now());
}
[[maybe_unused]] static void diag_platform_update(diag_platform_t value) {
  if (!g_diag.enabled) return;
  // Compare fields, not padding supplied by independent native callers.
  diag_platform_t *p = &g_diag.platform;
  int changed = p->logical_w != value.logical_w || p->logical_h != value.logical_h ||
      p->drawable_w != value.drawable_w || p->drawable_h != value.drawable_h ||
      p->output_x != value.output_x || p->output_y != value.output_y ||
      p->output_w != value.output_w || p->output_h != value.output_h ||
      p->refresh_hz != value.refresh_hz || p->swap_requested != value.swap_requested ||
      p->swap_accepted != value.swap_accepted || p->swap_queried != value.swap_queried ||
      p->focused != value.focused || p->output_source != value.output_source ||
      p->fullscreen != value.fullscreen || p->output_id != value.output_id ||
      p->rate_source != value.rate_source || p->cap_reason != value.cap_reason ||
      p->effective_cap_hz != value.effective_cap_hz;
  if (!changed && g_diag.platform_count) return;
  *p = value;
  if (g_diag.platform_count < DIAG_PLATFORM_EVENTS) {
    unsigned i = g_diag.platform_count++;
    g_diag.platform_events[i] = value;
    g_diag.platform_frame[i] = g_diag.serial;
  } else g_diag.platform_dropped++;
}
static void diag_msaa_observe(int samples) {
  if (!g_diag.enabled) return;
  if (!g_diag.msaa_seen || !g_diag.serial) {
    g_diag.initial_msaa = samples;
    g_diag.msaa_seen = 1;
  } else if (samples != g_diag.actual_msaa) {
    if (!g_diag.msaa_changes) g_diag.msaa_first_change = g_diag.serial;
    g_diag.msaa_last_change = g_diag.serial;
    g_diag.msaa_changes++;
  }
  g_diag.actual_msaa = samples;
}

static int diag_double_cmp(const void *a, const void *b) {
  double x = *(const double *)a, y = *(const double *)b;
  return x < y ? -1 : x > y;
}
static double diag_quantile(const double *ordered, unsigned n, unsigned percent) {
  if (!n) return 0.0;
  unsigned rank = (n * percent + 99) / 100;
  return ordered[(rank ? rank : 1) - 1];
}

static float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }
static float wrap_pi(float a) {
  // Guard, not a rewrite: past |a| ~ 2.56e8 the subtraction is a float32 no-op and the
  // loop cannot terminate.
  if (fabsf(a) > 1.0e6f) a = fmodf(a, F_TAU);
  while (a > F_PI) a -= F_TAU;
  while (a < -F_PI) a += F_TAU;
  return a;
}
// Rodrigues rotation of p around unit axis u.
static v3 v3_rot_axis(v3 p, v3 u, float ang) {
  float c = cosf(ang), s = sinf(ang);
  return v3_add(v3_add(v3_scale(p, c), v3_scale(v3_cross(u, p), s)),
                v3_scale(u, v3_dot(u, p) * (1.0f - c)));
}

typedef struct { float m[16]; } mat4;

static mat4 m4_mul(mat4 a, mat4 b) {
  mat4 o;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a.m[k * 4 + r] * b.m[c * 4 + k];
      o.m[c * 4 + r] = s;
    }
  return o;
}

// General 4x4 inverse (cofactors), for the screen-space passes' unprojection:
// world = inverse(VP) * ndc. Column-major like everything else here. A
// singular matrix returns the identity rather than NaNs — the AO then reads a
// flat world, which is a dull frame, not a corrupted one.
static mat4 m4_inverse(const mat4 *a) {
  const float *m = a->m;
  float inv[16];
  inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
  inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
  inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
  inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
  inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
  inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
  float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  mat4 o = {0};
  if (fabsf(det) < 1.0e-20f) { o.m[0] = o.m[5] = o.m[10] = o.m[15] = 1.0f; return o; }
  float id = 1.0f / det;
  for (int i = 0; i < 16; i++) o.m[i] = inv[i] * id;
  return o;
}

static mat4 m4_perspective(float fovy, float aspect, float zn, float zf) {
  float f = 1.0f / tanf(fovy * 0.5f);
  mat4 o = {0};
  o.m[0] = f / aspect;
  o.m[5] = f;
  o.m[10] = (zf + zn) / (zn - zf);
  o.m[11] = -1.0f;
  o.m[14] = 2.0f * zf * zn / (zn - zf);
  return o;
}

static mat4 m4_ortho(float hw, float hh, float zn, float zf) {
  mat4 o = {0};
  o.m[0] = 1.0f / hw;
  o.m[5] = 1.0f / hh;
  o.m[10] = -2.0f / (zf - zn);
  o.m[14] = -(zf + zn) / (zf - zn);
  o.m[15] = 1.0f;
  return o;
}

// The one view-matrix fill: shadow sampling needs m4_look_dir and m4_look to
// agree on row layout and the -fwd sign to the last digit.
static mat4 m4_from_basis(v3 eye, v3 right, v3 up, v3 fwd) {
  mat4 o = {0};
  o.m[0] = right.x; o.m[4] = right.y; o.m[8]  = right.z; o.m[12] = -v3_dot(right, eye);
  o.m[1] = up.x;    o.m[5] = up.y;    o.m[9]  = up.z;    o.m[13] = -v3_dot(up, eye);
  o.m[2] = -fwd.x;  o.m[6] = -fwd.y;  o.m[10] = -fwd.z;  o.m[14] = v3_dot(fwd, eye);
  o.m[15] = 1.0f;
  return o;
}

// From a DIRECTION, not a yaw/pitch pair: a yaw/pitch basis is singular exactly
// where a high sun lives.
static mat4 m4_look_dir(v3 eye, v3 fwd) {
  v3 up = fabsf(fwd.y) > 0.95f ? (v3){{1, 0, 0}} : (v3){{0, 1, 0}};
  v3 right = v3_norm(v3_cross(fwd, up));
  up = v3_cross(right, fwd);
  return m4_from_basis(eye, right, up, fwd);
}

// Camera basis with a ROLL about the view axis (leaning rolls the picture).
static void cam_basis(float yaw, float pitch, float roll, v3 *fwd, v3 *rgt, v3 *up) {
  v3 f = {{cosf(pitch) * sinf(yaw), sinf(pitch), -cosf(pitch) * cosf(yaw)}};
  v3 r = v3_norm(v3_cross(f, (v3){{0, 1, 0}}));
  v3 u = v3_cross(r, f);
  float c = cosf(roll), s = sinf(roll);
  *fwd = f;
  *rgt = v3_add(v3_scale(r, c), v3_scale(u, -s));
  *up  = v3_add(v3_scale(u, c), v3_scale(r,  s));
}

static mat4 m4_look(v3 eye, float yaw, float pitch, float roll) {
  v3 fwd, right, up;
  cam_basis(yaw, pitch, roll, &fwd, &right, &up);
  return m4_from_basis(eye, right, up, fwd);
}

// ---------------------------------------------------------------------------
// PCG32 — deterministic map generation from a seed.
// ---------------------------------------------------------------------------

// ONE process serves SRV_LOBBIES arenas over ONE UDP port.
#define SRV_LOBBIES 16
static int g_lobby;   // 0..SRV_LOBBIES-1; always 0 off the dedicated server

// Independent streams, so gunplay and the bots never disturb map generation or
// each other.
static uint64_t g_rng_all[SRV_LOBBIES];      // map generation
#define g_rng (g_rng_all[g_lobby])
// PER HUMAN: one shared stream is a determinism promise that dies with the second
// human. [0] is the historical stream, byte-identical in singleplayer.
static uint64_t g_rng_gun_all[SRV_LOBBIES][8 /* == MAX_HUMANS, later */];
#define g_rng_gun (g_rng_gun_all[g_lobby])
static uint64_t g_rng_bot_all[SRV_LOBBIES];  // wander targets + respawn spots
#define g_rng_bot (g_rng_bot_all[g_lobby])

static uint32_t rng_next(uint64_t *s) {
  uint64_t old = *s;
  *s = old * 6364136223846793005ULL + 1442695040888963407ULL;
  uint32_t xs = (uint32_t)(((old >> 18) ^ old) >> 27);
  uint32_t rot = (uint32_t)(old >> 59);
  return (xs >> rot) | (xs << ((32 - rot) & 31));
}

static uint32_t rng_u32(void) { return rng_next(&g_rng); }

static float rng_rangef(uint64_t *s, float lo, float hi) {
  return lo + (hi - lo) * ((float)(rng_next(s) >> 8) / 16777216.0f);
}

static float rng_range(float lo, float hi) { return rng_rangef(&g_rng, lo, hi); }

