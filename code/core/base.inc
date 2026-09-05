// =============================================================================
// core/base.inc  —  GL 3.3 loader (GL_FUNC_LIST X-macro), math (v3/mat4,
// f_clamp), PCG32 (separate map/gun/bot/vfx/synth streams).
// Proof: baut (the build itself gates this layer; no runtime instrument).
// One TU: included once by game.c, in definition order — moving a definition
// out is a semantic change.
// Contract: f_is_nan/f_is_nonfinite are the ONLY nan/finite tests in this
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

