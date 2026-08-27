// =============================================================================
// platform/linux_native.inc  —  the playable path's device layer: X11
// window/XI2 (nat_window, nat_xi2, nat_key/button/motion, nat_pump), evdev
// pad pump (nat_pad_pump, PAD_NODES), ALSA (audio_thread), net_udp_*.
// Proof: padscan (device only). One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: a machine has more than one pad and no static ordering says
// which one the player is holding — every node is held and the LAST
// TOUCHED one drives g_pad; draining inactive nodes is mandatory.
// =============================================================================
// ===========================================================================
// Linux native backend — X11 window, EGL 3.3 core, ALSA. The PLAYABLE path.
// ===========================================================================
// Same relationship to the shared core as the Windows backend: it owns a
// window, a clock, a keyboard and a swap chain, and hands `app_frame` a dt and
// an input snapshot. The two modes of this binary are mutually exclusive at
// `main` — a session either has a window or it has the script harness, never
// both, which is what keeps every proof command's determinism intact without a
// single lock.
//
// Everything below is dlopen'd or plain Xlib on purpose. The deliverable is
// one file that is scp'd to a handheld: the only hard link-time dependencies
// are libX11/libEGL/libGL, all of which SteamOS ships, and the two things that
// might be missing (XInput2 for raw mouse, ALSA for sound) degrade to a
// working game rather than to a failure to start.
//
// The headers are included HERE rather than at the top of the file for the
// reason `unistd.h` already is: Xlib defines a pile of unprefixed macros
// (`None`, `Success`, `Above`, `Complex`, ...) and there is no reason to expose
// the entire shared core to them.

#include <X11/XKBlib.h>    // XkbKeycodeToKeysym, XkbSetDetectableAutoRepeat
#include <X11/Xatom.h>     // XA_ATOM / XA_CARDINAL for the EWMH hints
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>

// --- XInput2, hand-declared ------------------------------------------------
// Raw mouse deltas are the exact analogue of the Windows backend's WM_INPUT:
// they bypass the X server's pointer acceleration, which a warped-pointer
// implementation cannot. libXi is reached by dlopen so the build needs no
// headers for it and a machine without it still plays — and the layout below
// is ABI, reproduced from XInput2.h and asserted rather than trusted.
typedef struct { int mask_len; unsigned char *mask; double *values; } xi_valuators_t;
typedef struct { int deviceid; int mask_len; unsigned char *mask; }   xi_evmask_t;
typedef struct {
  int            type;         // GenericEvent
  unsigned long  serial;
  Bool           send_event;
  Display       *display;
  int            extension;    // XI major opcode
  int            evtype;       // XI_RawMotion, ...
  Time           time;
  int            deviceid;
  int            sourceid;
  int            detail;
  int            flags;
  xi_valuators_t valuators;
  double        *raw_values;   // PACKED: one per SET bit in valuators.mask
} xi_raw_event_t;

// Device query, for the one thing a raw event does not tell you: whether its
// source reports RELATIVE motion. wndproc rejects absolute devices explicitly
// (`!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)`) and the X11 side has to
// do the same by asking — a touchscreen or tablet puts an absolute COORDINATE
// in raw_values, and this handheld has a touchscreen.
typedef struct { int type; int sourceid; } xi_anyclass_t;
typedef struct {
  int    type;        // XI_VALUATOR_CLASS
  int    sourceid;
  int    number;      // valuator index: 0 and 1 are x and y on a pointer
  Atom   label;
  double min, max, value;
  int    resolution;
  int    mode;        // XI_MODE_RELATIVE / XI_MODE_ABSOLUTE
} xi_valclass_t;
typedef struct {
  int             deviceid;
  char           *name;
  int             use;
  int             attachment;
  Bool            enabled;
  int             num_classes;
  xi_anyclass_t **classes;
} xi_devinfo_t;

#define XI_ALL_DEVICES        0
#define XI_ALL_MASTER_DEVICES 1
#define XI_RAW_MOTION         17
#define XI_HIERARCHY_CHANGED  11
#define XI_MASK_BYTES         4     // covers evtype 0..31
#define XI_VALUATOR_CLASS     2
#define XI_MODE_RELATIVE      0
#define XI_MODE_ABSOLUTE      1

// If any of these ever fires, the struct above has drifted from the libXi that will
// actually fill it in and every delta read out of it is garbage.
#if defined(__LP64__)
static_assert(sizeof(xi_valuators_t) == 24, "XIValuatorState ABI");
static_assert(offsetof(xi_valuators_t, values) == 16, "XIValuatorState.values");
static_assert(sizeof(xi_evmask_t) == 16, "XIEventMask ABI");
static_assert(sizeof(xi_raw_event_t) == 96, "XIRawEvent ABI");
static_assert(offsetof(xi_raw_event_t, evtype)     == 36, "XIRawEvent.evtype");
static_assert(offsetof(xi_raw_event_t, detail)     == 56, "XIRawEvent.detail");
static_assert(offsetof(xi_raw_event_t, valuators)  == 64, "XIRawEvent.valuators");
static_assert(offsetof(xi_raw_event_t, raw_values) == 88, "XIRawEvent.raw_values");
static_assert(sizeof(xi_anyclass_t) == 8, "XIAnyClassInfo ABI");
static_assert(sizeof(xi_valclass_t) == 56, "XIValuatorClassInfo ABI");
static_assert(offsetof(xi_valclass_t, number) == 8, "XIValuatorClassInfo.number");
static_assert(offsetof(xi_valclass_t, mode) == 52, "XIValuatorClassInfo.mode");
static_assert(sizeof(xi_devinfo_t) == 40, "XIDeviceInfo ABI");
static_assert(offsetof(xi_devinfo_t, num_classes) == 28, "XIDeviceInfo.num_classes");
static_assert(offsetof(xi_devinfo_t, classes) == 32, "XIDeviceInfo.classes");
#endif

typedef Status (*pfn_XIQueryVersion)(Display *, int *, int *);
typedef int    (*pfn_XISelectEvents)(Display *, Window, xi_evmask_t *, int);
typedef xi_devinfo_t *(*pfn_XIQueryDevice)(Display *, int, int *);
typedef void   (*pfn_XIFreeDeviceInfo)(xi_devinfo_t *);
typedef EGLDisplay (*pfn_eglGetPlatformDisplayEXT)(EGLenum, void *, const EGLint *);

#ifndef EGL_PLATFORM_X11_EXT
  #define EGL_PLATFORM_X11_EXT 0x31D5
#endif
// EGL 1.5 spells these without a suffix, EGL 1.4 + KHR_create_context with
// _KHR, and the numeric values are identical — so one attribute list serves
// both and a 1.4-era egl.h still builds.
#ifndef EGL_CONTEXT_MAJOR_VERSION
  #define EGL_CONTEXT_MAJOR_VERSION 0x3098
#endif
#ifndef EGL_CONTEXT_MINOR_VERSION
  #define EGL_CONTEXT_MINOR_VERSION 0x30FB
#endif
#ifndef EGL_CONTEXT_OPENGL_PROFILE_MASK
  #define EGL_CONTEXT_OPENGL_PROFILE_MASK 0x30FD
#endif
#ifndef EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT
  #define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT 0x00000001
#endif

static Display  *g_xdpy;
static Window    g_xwin, g_xroot;
static Cursor    g_xblank;
static Atom      A_WM_PROTOCOLS, A_WM_DELETE, A_NET_WM_STATE, A_NET_WM_FULLSCREEN,
                 A_NET_WM_BYPASS_COMP;
static EGLDisplay g_egl_dpy = EGL_NO_DISPLAY;
static EGLSurface g_egl_surf = EGL_NO_SURFACE;
static EGLContext g_egl_ctx = EGL_NO_CONTEXT;

static int   g_running = 1, g_active, g_mapped, g_grabbed, g_fullscreen = 1;
static float g_mouse_dx, g_mouse_dy;
static int   g_xerr;                 // X protocol errors seen, for diagnosis

static int  g_xi_op = -1, g_xi_on;
static pfn_XIQueryVersion    p_XIQueryVersion;
static pfn_XISelectEvents    p_XISelectEvents;
static pfn_XIQueryDevice     p_XIQueryDevice;
static pfn_XIFreeDeviceInfo  p_XIFreeDeviceInfo;
// Per-source verdict cache: 0 = not asked yet, 1 = relative (use it), -1 = absolute
// (ignore it).
#define XI_DEV_MAX 256
static signed char g_xi_dev[XI_DEV_MAX];
static long g_xi_used, g_xi_dropped;   // raw motions taken / ignored, see nat_pump
// Pointer-warp fallback, used only when libXi is missing. Deltas come from the
// pointer's own previous position, NOT from the window centre — see nat_motion.
static int    g_warp_cx, g_warp_cy, g_warp_x, g_warp_y, g_warp_sync;
static double g_grab_fail;    // last refused XGrabPointer, for the retry throttle
static double g_focus_t;      // last real FocusIn, for the activation click
static int    g_esc_held, g_detect_repeat;

static double nat_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

// --- Gamepad via evdev ------------------------------------------------------
// No library at all: /dev/input/event* is the kernel's own interface, which is what a
// binary scp'd to a handheld wants.

#include <fcntl.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>    // UDP networking(POSIX half)
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/random.h>    // getrandom() for the cookie secret (net_secret_init)

// --- UDP socket backend, POSIX half ---
static int net_udp_open(int port) {
  // SOCK_CLOEXEC, for the same reason the evdev pump opens with O_CLOEXEC: this process
  // both FORKS (upd_fetch execs curl) and EXECVs ITSELF (the update swap).
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return -1;
  int fl = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, fl | O_NONBLOCK);         // non-blocking: the game polls
  if (port > 0) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
  }
  return fd;
}
static void net_udp_close(int fd) { if (fd >= 0) close(fd); }
static int net_udp_send(int fd, uint32_t ip, uint16_t port, const void *b, int n) {
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = ip;                      // already network-order
  a.sin_port = htons(port);
  return (int)sendto(fd, b, (size_t)n, 0, (struct sockaddr *)&a, sizeof a);
}
static int net_udp_recv(int fd, uint32_t *ip, uint16_t *port, void *b, int cap) {
  struct sockaddr_in a = {0};
  socklen_t al = sizeof a;
  ssize_t r = recvfrom(fd, b, (size_t)cap, 0, (struct sockaddr *)&a, &al);
  if (r < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
  if (ip) *ip = a.sin_addr.s_addr;
  if (port) *port = ntohs(a.sin_port);
  return (int)r;
}
static uint32_t net_resolve(const char *host) {
  struct in_addr ia;
  if (inet_aton(host, &ia)) return ia.s_addr;   // dotted quad
  struct addrinfo hints = {0}, *res = NULL;      // else a DNS name
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
  uint32_t out = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return out;
}
static uint32_t net_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

// The ONE seed for g_net_secret, and it has to be unguessable rather than merely
// different per process.
static void net_secret_init(void) {
  uint64_t s = 0;
  if (getrandom(&s, sizeof s, 0) == (ssize_t)sizeof s && s) { g_net_secret = s; return; }
  FILE *f = fopen("/dev/urandom", "rb");
  if (f) {
    size_t got = fread(&s, 1, sizeof s, f);
    fclose(f);
    if (got == sizeof s && s) { g_net_secret = s; return; }
  }
  fprintf(stderr, "server: no kernel entropy source — cookie secret is GUESSABLE\n");
  g_net_secret = (uint64_t)time(NULL) * 2654435761u ^ ((uint64_t)getpid() << 32);
}

typedef struct {           // struct input_event, 64-bit ABI (SteamOS and this
  long tv_sec, tv_usec;    // container are both LP64; a 32-bit build fails the
  unsigned short type, code;  // assert below rather than corrupting reads)
  int value;
} ev_event_t;
static_assert(sizeof(ev_event_t) == 24, "input_event ABI");
typedef struct { int value, minimum, maximum, fuzz, flat, resolution; } ev_absinfo_t;
static_assert(sizeof(ev_absinfo_t) == 24, "input_absinfo ABI");

#define EVIOCGBIT_(ev, len) _IOC(_IOC_READ, 'E', 0x20 + (ev), len)
#define EVIOCGKEY_(len)     _IOC(_IOC_READ, 'E', 0x18, len)
#define EVIOCGNAME_(len)    _IOC(_IOC_READ, 'E', 0x06, len)
#define EVIOCGABS_(abs)     _IOR('E', 0x40 + (abs), ev_absinfo_t)
#define EVIOCGRAB_          _IOW('E', 0x90, int)

// Prefixed re-declarations of <linux/input-event-codes.h> values (see above
// for why the header itself is off limits).
#define PEV_SYN 0x00
#define PEV_KEY 0x01
#define PEV_ABS 0x03
#define PSYN_DROPPED 3
#define PABS_X 0x00
#define PABS_Y 0x01
#define PABS_Z 0x02        // xpad: left trigger
#define PABS_RX 0x03
#define PABS_RY 0x04
#define PABS_RZ 0x05       // xpad: right trigger
#define PABS_HAT0X 0x10    // dpad
#define PABS_HAT0Y 0x11
#define PABS_MAX 0x3f
#define PKEY_MAX 0x2ff
// TRAP: the kernel's compass names do NOT match an Xbox pad's physical
// layout — BTN_X is aliased to BTN_NORTH (0x133) and BTN_Y to BTN_WEST
// (0x134), and xpad emits the ALIAS meaning. These are the alias values.
#define PBTN_A 0x130       // == BTN_GAMEPAD, the fingerprint bit
#define PBTN_B 0x131
#define PBTN_X 0x133
#define PBTN_Y 0x134
#define PBTN_TL 0x136
#define PBTN_TR 0x137
#define PBTN_TL2 0x138     // digital-trigger pads only; xpad uses ABS_Z/RZ
#define PBTN_TR2 0x139
#define PBTN_SELECT 0x13a
#define PBTN_START 0x13b
#define PBTN_THUMBL 0x13d
#define PBTN_THUMBR 0x13e
#define PBTN_DPAD_UP 0x220
#define PBTN_DPAD_DOWN 0x221
#define PBTN_DPAD_LEFT 0x222
#define PBTN_DPAD_RIGHT 0x223

// A HANDHELD HAS MORE THAN ONE PAD, AND NO STATIC ORDERING SAYS WHICH ONE THE PLAYER IS
// HOLDING — slot order is Steam's, minor order is the kernel's, and neither knows about
// hands. "Keep the last match" assumed Steam's virtual pad is created after the physical
// nodes; true, and useless the moment there are TWO virtual pads. Measured on the deploy
// handheld with an 8BitDo on its dongle, the 8BitDo was event18 and the BUILT-IN pad
// event22, so the game played the built-in while the pad in the player's hands did
// nothing — every ioctl succeeding, read() returning EAGAIN forever, nothing reporting
// an error. Over Bluetooth the same controller lands in the higher slot, which is why it
// read as "works on BT, not on the dongle": a coin toss dressed as a rule.
//
// So stop guessing: hold EVERY candidate node open and let the one last TOUCHED drive
// the game. Draining all of them is mandatory, not a cost we chose — an unread evdev fd
// overflows its kernel ring, which is the defect nat_pad_resync exists for.
#define PAD_NODES 8
// A button going DOWN, or half a stick's travel: comfortably past any drift a
// worn stick has, so a pad face-down on a table can never steal the session.
// ...AND the incumbent has to have gone quiet first.
#define PAD_WAKE_QUIET 1.0

typedef struct {
  int    fd;                                 // -1 = free slot
  int    idx;                                // /dev/input/eventIDX = identity
  double touched;                            // last significant input (nat_now)
  char   name[64];                           // EVIOCGNAME, for the stderr log
  struct { int min, max; } absr[PABS_RZ + 1];
} pad_node_t;

// SPELLED OUT, because a static array is zero-initialised and fd 0 IS A VALID
// DESCRIPTOR — it is stdin.
#define PAD_NODE_FREE {.fd = -1, .idx = -1}
static pad_node_t g_pad_node[PAD_NODES] = {
  PAD_NODE_FREE, PAD_NODE_FREE, PAD_NODE_FREE, PAD_NODE_FREE,
  PAD_NODE_FREE, PAD_NODE_FREE, PAD_NODE_FREE, PAD_NODE_FREE,
};
static_assert(PAD_NODES == 8, "the initializer list above is written out");
static int    g_pad_act = -1;                     // the node that drives g_pad
static double g_pad_rescan;                       // next hotplug scan
static double g_pad_reprobe;                      // next ownership probe

#define PAD_TESTBIT(a, b) ((a)[(b) >> 3] & (1u << ((b) & 7)))

static int nat_pad_btn(unsigned short code) {
  switch (code) {
    case PBTN_A: return PB_A;            case PBTN_B: return PB_B;
    case PBTN_X: return PB_X;            case PBTN_Y: return PB_Y;
    case PBTN_TL: return PB_LB;          case PBTN_TR: return PB_RB;
    case PBTN_SELECT: return PB_SELECT;  case PBTN_START: return PB_START;
    case PBTN_THUMBL: return PB_L3;      case PBTN_THUMBR: return PB_R3;
    case PBTN_DPAD_UP: return PB_UP;     case PBTN_DPAD_DOWN: return PB_DOWN;
    case PBTN_DPAD_LEFT: return PB_LEFT; case PBTN_DPAD_RIGHT: return PB_RIGHT;
    default: return -1;
  }
}

// Normalisation against the device's OWN reported range — a Deck stick is -32768..32767
// but nothing in the gamepad spec promises that.
static float nat_pad_axis(const pad_node_t *n, int code, int v, int centred) {
  int mn = n->absr[code].min, mx = n->absr[code].max;
  if (mx <= mn) return 0;
  float u = (float)(v - mn) / (float)(mx - mn);
  return centred ? f_clamp(u * 2.0f - 1.0f, -1.0f, 1.0f) : f_clamp(u, 0.0f, 1.0f);
}

static void nat_pad_abs(const pad_node_t *n, int code, int v) {
  switch (code) {
    case PABS_X:  g_pad.lx =  nat_pad_axis(n, PABS_X, v, 1); break;
    case PABS_Y:  g_pad.ly = -nat_pad_axis(n, PABS_Y, v, 1); break;  // evdev +y is DOWN
    case PABS_RX: g_pad.rx =  nat_pad_axis(n, PABS_RX, v, 1); break;
    case PABS_RY: g_pad.ry = -nat_pad_axis(n, PABS_RY, v, 1); break;
    case PABS_Z:  g_pad.lt =  nat_pad_axis(n, PABS_Z, v, 0); break;
    case PABS_RZ: g_pad.rt =  nat_pad_axis(n, PABS_RZ, v, 0); break;
    case PABS_HAT0X: g_pad.btn[PB_LEFT] = v < 0; g_pad.btn[PB_RIGHT] = v > 0; break;
    case PABS_HAT0Y: g_pad.btn[PB_UP]   = v < 0; g_pad.btn[PB_DOWN]  = v > 0; break;
    default: break;
  }
}

// "The player TOUCHED this pad", the one question the hand-over turns on.
static int nat_pad_woke(const pad_node_t *n, const ev_event_t *e) {
  if (e->type == PEV_KEY)
    return e->value != 0 && (nat_pad_btn(e->code) >= 0 ||
                             e->code == PBTN_TL2 || e->code == PBTN_TR2);
  if (e->type != PEV_ABS) return 0;
  switch (e->code) {
    case PABS_X: case PABS_Y: case PABS_RX: case PABS_RY:
      return fabsf(nat_pad_axis(n, e->code, e->value, 1)) > PAD_WAKE_DEFL;
    case PABS_Z: case PABS_RZ:
      return nat_pad_axis(n, e->code, e->value, 0) > PAD_WAKE_DEFL;
    case PABS_HAT0X: case PABS_HAT0Y: return e->value != 0;
    default: return 0;
  }
}

// Drop ONE node: unplugged, or Steam has taken ownership of it.
static void nat_pad_drop(int slot) {
  if (g_pad_node[slot].fd >= 0) close(g_pad_node[slot].fd);
  g_pad_node[slot].fd = -1;
  g_pad_node[slot].idx = -1;
  if (g_pad_act == slot) {
    g_pad_act = -1;
    memset(&g_pad, 0, sizeof g_pad);
  }
}

// The kernel's per-client evdev buffer is a RING: stall long enough (focus loss pauses
// the loop) and it overflows, the kernel emits SYN_DROPPED, and every level tracked
// from events is untrustworthy — a release may be gone.
static void nat_pad_resync(const pad_node_t *n) {
  unsigned char keys[(PKEY_MAX + 8) / 8] = {0};
  if (ioctl(n->fd, EVIOCGKEY_(sizeof keys), keys) >= 0) {
    for (int c = PBTN_A; c <= PBTN_THUMBR; c++) {
      int b = nat_pad_btn((unsigned short)c);
      if (b >= 0) g_pad.btn[b] = PAD_TESTBIT(keys, c) != 0;
    }
    for (int c = PBTN_DPAD_UP; c <= PBTN_DPAD_RIGHT; c++) {
      int b = nat_pad_btn((unsigned short)c);
      if (b >= 0) g_pad.btn[b] = PAD_TESTBIT(keys, c) != 0;
    }
    // Digital-trigger fallback lives outside nat_pad_btn (TL2/TR2 map to no
    // PB_ button), so without this a drop storm leaves lt/rt stuck on pads
    // that have no analog trigger axis — same gate as the event pump's.
    if (n->absr[PABS_Z].max <= n->absr[PABS_Z].min)
      g_pad.lt = PAD_TESTBIT(keys, PBTN_TL2) ? 1.0f : 0.0f;
    if (n->absr[PABS_RZ].max <= n->absr[PABS_RZ].min)
      g_pad.rt = PAD_TESTBIT(keys, PBTN_TR2) ? 1.0f : 0.0f;
  }
  for (int a = 0; a <= PABS_RZ; a++) {
    ev_absinfo_t ai;
    if (n->absr[a].max > n->absr[a].min &&
        ioctl(n->fd, EVIOCGABS_(a), &ai) >= 0)
      nat_pad_abs(n, a, ai.value);
  }
  // The dpad is an ABS hat on xpad and friends — outside both loops above, so
  // a resync that skips it leaves a dpad direction stuck through the exact
  // event storm that caused the drop. Hats need no range table (values -1/0/1).
  for (int a = PABS_HAT0X; a <= PABS_HAT0Y; a++) {
    ev_absinfo_t ai;
    if (ioctl(n->fd, EVIOCGABS_(a), &ai) >= 0) nat_pad_abs(n, a, ai.value);
  }
}

// Make `slot` the node that drives the game.
static void nat_pad_activate(int slot) {
  g_pad_act = slot;
  memset(&g_pad, 0, sizeof g_pad);
  nat_pad_resync(&g_pad_node[slot]);
  fprintf(stderr, "pad: active -> %s (event%d)\n",
          g_pad_node[slot].name, g_pad_node[slot].idx);
}

// The gamepad fingerprint is BTN_GAMEPAD + ABS_X: the kernel gamepad spec requires both
// of every pad, and keyboards, mice and touchscreens have neither together.
static int nat_pad_sysfs_is_pad(int idx) {
  char p[80];
  snprintf(p, sizeof p, "/sys/class/input/event%d/device/capabilities/key", idx);
  FILE *f = fopen(p, "r");
  if (!f) return 0;
  unsigned long long w[16];
  int n = 0;
  while (n < 16 && fscanf(f, "%llx", &w[n]) == 1) n++;
  fclose(f);
  int word = 304 / 64, bit = 304 % 64;
  if (n <= word) return 0;
  return (int)((w[n - 1 - word] >> bit) & 1ull);
}

// Fills `out`'s ranges and name on success and returns the fd; the caller owns both.
static int nat_pad_try(const char *path, int idx, pad_node_t *out) {
  int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    // EACCES on a node whose sysfs says "gamepad" = Steam is hiding the pad it manages
    // (ACL mask ---).
    if (errno == EACCES && nat_pad_sysfs_is_pad(idx)) g_pad_denied = 1;
    return -1;
  }
  unsigned char kbits[(PKEY_MAX + 8) / 8] = {0};
  unsigned char abits[(PABS_MAX + 8) / 8] = {0};
  if (ioctl(fd, EVIOCGBIT_(PEV_KEY, sizeof kbits), kbits) < 0 ||
      ioctl(fd, EVIOCGBIT_(PEV_ABS, sizeof abits), abits) < 0 ||
      !PAD_TESTBIT(kbits, PBTN_A) || !PAD_TESTBIT(abits, PABS_X)) {
    close(fd);
    return -1;
  }
  if (ioctl(fd, EVIOCGRAB_, 1) < 0) {  // Steam owns it (exclusive grab)
    g_pad_denied = 1;
    close(fd);
    return -1;
  }
  ioctl(fd, EVIOCGRAB_, 0);
  for (int a = 0; a <= PABS_RZ; a++) {
    ev_absinfo_t ai;
    out->absr[a].min = out->absr[a].max = 0;
    if (PAD_TESTBIT(abits, a) && ioctl(fd, EVIOCGABS_(a), &ai) >= 0) {
      out->absr[a].min = ai.minimum;
      out->absr[a].max = ai.maximum;
    }
  }
  snprintf(out->name, sizeof out->name, "?");
  ioctl(fd, EVIOCGNAME_(sizeof out->name), out->name);
  out->name[sizeof out->name - 1] = '\0';  // the kernel does not terminate on truncation
  fprintf(stderr, "pad: %s (%s)\n", out->name, path);
  return fd;
}

static void nat_pad_pump(double now) {
  // HOTPLUG runs on the same 2 s cadence whether or not a pad is already held, which
  // the single-fd version could not afford to do.
  if (now >= g_pad_rescan) {
    g_pad_rescan = now + 2.0;
    g_pad_denied = 0;  // re-judged per scan: the layout switch can happen live
    for (int i = 0; i < 64; i++) {
      int slot = -1, held = 0;
      for (int s = 0; s < PAD_NODES; s++) {
        if (g_pad_node[s].fd >= 0) {
          if (g_pad_node[s].idx == i) { held = 1; break; }
        } else if (slot < 0) {
          slot = s;
        }
      }
      if (held) continue;
      if (slot < 0) break;              // table full — nothing left to add to
      char path[32];
      snprintf(path, sizeof path, "/dev/input/event%d", i);
      pad_node_t n = {.fd = -1, .idx = i, .touched = 0};
      int fd = nat_pad_try(path, i, &n);
      if (fd < 0) continue;
      n.fd = fd;
      g_pad_node[slot] = n;
    }
  }
  // Ownership can be TAKEN mid-session: when Steam Input starts managing a pad it grabs
  // the node the game holds, and from then on read() returns EAGAIN forever with no
  // error — that node freezes at its last levels and nothing reaches the rescan.
  if (now >= g_pad_reprobe) {
    g_pad_reprobe = now + 2.0;
    for (int s = 0; s < PAD_NODES; s++) {
      if (g_pad_node[s].fd < 0) continue;
      if (ioctl(g_pad_node[s].fd, EVIOCGRAB_, 1) < 0) {
        if (errno == EBUSY) { g_pad_denied = 1; nat_pad_drop(s); }
      } else {
        ioctl(g_pad_node[s].fd, EVIOCGRAB_, 0);
      }
    }
  }
  // EVERY node is drained every frame; only the ACTIVE one writes g_pad.
  for (int s = 0; s < PAD_NODES; s++) {
    pad_node_t *n = &g_pad_node[s];
    if (n->fd < 0) continue;
    int active = (s == g_pad_act);
    ev_event_t ev;
    for (;;) {
      ssize_t r = read(n->fd, &ev, sizeof ev);
      if (r != (ssize_t)sizeof ev) {
        if (r < 0 && errno == EAGAIN) break;  // == EWOULDBLOCK on every Linux libc
        if (r < 0 && errno == EINTR) continue;  // a signal is not an unplug
        nat_pad_drop(s);  // unplugged (ENODEV) or a truncated read
        break;
      }
      if (nat_pad_woke(n, &ev)) n->touched = now;
      if (!active) continue;            // drained for the ring, not for state
      if (ev.type == PEV_KEY) {
        // value 2 is auto-repeat, which is still "held".
        if (ev.code == PBTN_TL2) {
          if (n->absr[PABS_Z].max <= n->absr[PABS_Z].min)
            g_pad.lt = ev.value ? 1.0f : 0.0f;
        } else if (ev.code == PBTN_TR2) {
          if (n->absr[PABS_RZ].max <= n->absr[PABS_RZ].min)
            g_pad.rt = ev.value ? 1.0f : 0.0f;
        } else {
          int b = nat_pad_btn(ev.code);
          if (b >= 0) g_pad.btn[b] = ev.value != 0;
        }
      } else if (ev.type == PEV_ABS) {
        nat_pad_abs(n, ev.code, ev.value);
      } else if (ev.type == PEV_SYN && ev.code == PSYN_DROPPED) {
        nat_pad_resync(n);
      }
    }
  }
  // HAND-OVER. Two conditions, and each one is load-bearing.
  int best = -1;
  for (int s = 0; s < PAD_NODES; s++)
    if (g_pad_node[s].fd >= 0 && s != g_pad_act && g_pad_node[s].touched > 0 &&
        (best < 0 || g_pad_node[s].touched > g_pad_node[best].touched))
      best = s;
  if (g_pad_act < 0) {
    // Nothing is driving: either the first scan just landed or the active pad was
    // unplugged.
    if (best < 0)
      for (int s = 0; s < PAD_NODES; s++)
        if (g_pad_node[s].fd >= 0) { best = s; break; }
    if (best >= 0) nat_pad_activate(best);
  } else if (best >= 0 &&
             g_pad_node[best].touched > g_pad_node[g_pad_act].touched &&
             now - g_pad_node[g_pad_act].touched >= PAD_WAKE_QUIET) {
    nat_pad_activate(best);
  }
  // One flag over the whole table: "is there a pad at all". nat_pad_drop and
  // nat_pad_activate both clear g_pad wholesale, so this is republished here
  // rather than maintained at every site that can change it.
  int any = 0;
  for (int s = 0; s < PAD_NODES; s++) if (g_pad_node[s].fd >= 0) any = 1;
  g_pad.connected = any;
}

// `padscan` (harness).
static void nat_pad_report(int frames) {
  double t0 = nat_now();
  int prev_act = -2;
  for (int f = 0; f < frames; f++) {
    nat_pad_pump(nat_now());
    if (g_pad_act != prev_act) {
      printf("padscan t=%.2f active=%s\n", nat_now() - t0,
             g_pad_act < 0 ? "none" : g_pad_node[g_pad_act].name);
      prev_act = g_pad_act;
    }
    struct timespec ts = {0, 8333333L};   // ~120 Hz, the game's own frame rate
    nanosleep(&ts, NULL);
  }
  double now = nat_now();
  int n = 0;
  for (int i = 0; i < PAD_NODES; i++) {
    if (g_pad_node[i].fd < 0) continue;
    n++;
    printf("padscan node event%-2d %s touch=%s name=%s\n",
           g_pad_node[i].idx, i == g_pad_act ? "ACT" : "   ",
           g_pad_node[i].touched > 0 ? "yes" : "no ", g_pad_node[i].name);
    if (g_pad_node[i].touched > 0)
      printf("padscan   last touched %.2f s ago\n", now - g_pad_node[i].touched);
  }
  printf("padscan nodes=%d active=%s denied=%d conn=%d\n", n,
         g_pad_act < 0 ? "none" : g_pad_node[g_pad_act].name,
         g_pad_denied, g_pad.connected);
}

// --- refresh rate ----------------------------------------------------------
// **`eglSwapInterval` is a REQUEST, and on XWayland it is one the compositor is free to
// ignore.** MEASURED on the handheld, docked to a 120 Hz panel, with vsync ON and the
// shipped `fps_cap 0`: the game presented as fast as it could and pegged the GPU at
// 100% and a core at 95% drawing frames nobody sees.
typedef struct _XRRScreenConfiguration xrr_conf_t;
typedef xrr_conf_t *(*pfn_XRRGetScreenInfo)(Display *, Window);
typedef short (*pfn_XRRConfigCurrentRate)(xrr_conf_t *);
typedef void (*pfn_XRRFreeScreenConfigInfo)(xrr_conf_t *);

#define NAT_REFRESH_FALLBACK 60.0

static double nat_refresh_hz(void) {
  static pfn_XRRGetScreenInfo        p_get;
  static pfn_XRRConfigCurrentRate    p_rate;
  static pfn_XRRFreeScreenConfigInfo p_free;
  static int tried;
  if (!tried) {
    tried = 1;
    void *lib = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_LOCAL);
    if (lib) {
      p_get  = (pfn_XRRGetScreenInfo)(void *)dlsym(lib, "XRRGetScreenInfo");
      p_rate = (pfn_XRRConfigCurrentRate)(void *)dlsym(lib, "XRRConfigCurrentRate");
      p_free = (pfn_XRRFreeScreenConfigInfo)(void *)dlsym(lib, "XRRFreeScreenConfigInfo");
    }
  }
  if (!p_get || !p_rate || !p_free) return NAT_REFRESH_FALLBACK;
  xrr_conf_t *conf = p_get(g_xdpy, g_xroot);
  if (!conf) return NAT_REFRESH_FALLBACK;
  short hz = p_rate(conf);
  p_free(conf);
  // A rate outside this range is a server that does not know either; 20 covers
  // a slideshow and 400 covers anything a panel actually does.
  return (hz >= 20 && hz <= 400) ? (double)hz : NAT_REFRESH_FALLBACK;
}

static double g_refresh_hz = NAT_REFRESH_FALLBACK;   // re-read on every resize

// An X protocol error must be diagnosable, not an abort in the middle of a
// frame: a server that dislikes our XI2 selection has to fall back, not die.
static int nat_xerr(Display *d, XErrorEvent *e) {
  char txt[128];
  XGetErrorText(d, e->error_code, txt, sizeof txt);
  fprintf(stderr, "x11: %s (request %d)\n", txt, e->request_code);
  g_xerr++;
  return 0;
}

// --- keys ------------------------------------------------------------------
// Group 0 / shift level 0 is the layout's UNSHIFTED symbol, which is what makes this
// layout-dependent in exactly the same way the Windows VK path is — so a config.cfg
// written on one means the same physical keys on the other.
static int key_from_keysym(KeySym ks) {
  if (ks >= XK_a && ks <= XK_z) return (int)(ks - XK_a) + 'A';
  if (ks >= XK_A && ks <= XK_Z) return (int)(ks - XK_A) + 'A';
  if (ks >= XK_0 && ks <= XK_9) return (int)ks;          // XK_0 == '0'
  switch (ks) {
    case XK_space:        return KEY_SPACE;
    case XK_Tab:
    case XK_ISO_Left_Tab: return KEY_TAB;
    case XK_Shift_L:      return KEY_LSHIFT;
    case XK_Control_L:    return KEY_LCTRL;
  }
  return 0;
}

// X wire buttons are 1=left 2=MIDDLE 3=RIGHT 4..7=wheel 8=back 9=forward, and
// the game's tokens are mouse1=left mouse2=RIGHT mouse3=MIDDLE — 2 and 3 are
// SWAPPED. Mapping straight through silently binds ADS to the wheel click.
static int key_from_button(unsigned int b) {
  switch (b) {
    case 1: return KEY_MOUSE1;
    case 2: return KEY_MOUSE3;
    case 3: return KEY_MOUSE2;
    case 4: return KEY_MWUP;   // vertical wheel: X sends a press AND a release
    case 5: return KEY_MWDN;   //   per notch, so nat_button ignores the release
    case 8: return KEY_MOUSE4;
    case 9: return KEY_MOUSE5;
  }
  return 0;   // 6/7 are the HORIZONTAL wheel, which nothing reads
}

static void nat_input_clear(void) {   // focus loss must not leave a key stuck
  memset(g_keys, 0, sizeof g_keys);
  memset(g_pressed, 0, sizeof g_pressed);
  g_mouse_dx = g_mouse_dy = 0;
  // The UI mouse button too: alt-tab away with LMB held on a slider leaves
  // g_ui_mdown latched (the release goes to the other window), and the menu
  // then drags that slider by pure cursor motion until the next real click.
  g_ui_mdown = 0;
  g_ui_drag = 0;
  // Escape's own held flag too: an Esc held across the focus loss never sends
  // its release here, and a stale 1 swallows the first Esc after refocus.
  g_esc_held = 0;
  // Pad LEVELS deliberately stay: evdev never re-reports an unchanged key, so zeroing
  // g_pad.btn here would leave a physically-held button dead until it is re-pressed.
}

// --- pointer ---------------------------------------------------------------
// Re-base the warp fallback. Grabbing, gaining focus and resizing all leave
// MotionNotify events in flight carrying the pointer's OLD window-relative
// position; emitting a delta from the first of those snaps the view a full turn
// on frame one (measured: d=(1279,719) on a 1280x720 window). `g_warp_sync`
// makes the next motion event re-base the reference position and emit nothing,
// which costs one event and cannot produce a wrong number — unlike waiting for
// the pointer to be OBSERVED at the centre, where a pointer already sitting
// there generates no echo at all and the wait needs a timeout that then fires
// with the pointer somewhere else entirely.
static void nat_warp_arm(void) {
  g_warp_cx = R.width / 2;
  g_warp_cy = R.height / 2;
  g_warp_x = g_warp_cx;
  g_warp_y = g_warp_cy;
  g_warp_sync = 1;
  if (g_grabbed) {
    XWarpPointer(g_xdpy, None, g_xwin, 0, 0, 0, 0, g_warp_cx, g_warp_cy);
    XFlush(g_xdpy);
  }
}

// The hardware cursor is hidden whenever the game has focus — INCLUDING with the menu
// open, where the UI draws its own crosshair.
static void nat_cursor(int visible) {
  static int shown = 1;
  if (visible == shown) return;
  shown = visible;
  if (visible) XUndefineCursor(g_xdpy, g_xwin);
  else         XDefineCursor(g_xdpy, g_xwin, g_xblank);
}

static void nat_grab(int on) {
  if (on) {
    if (g_grabbed || !g_mapped || !g_active || g_menu_open) return;
    // A refused grab does not come back on its own — another client can hold the
    // pointer (a WM menu, a window drag, a screenshot tool) — so this is retried from
    // the frame loop.
    if (nat_now() - g_grab_fail < 0.5) return;
    if (XGrabPointer(g_xdpy, g_xwin, True,
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                     GrabModeAsync, GrabModeAsync, g_xwin /* confine_to */,
                     g_xblank, CurrentTime) == GrabSuccess) {
      g_grabbed = 1;
      nat_warp_arm();
    } else {
      g_grab_fail = nat_now();
    }
  } else if (g_grabbed) {
    XUngrabPointer(g_xdpy, CurrentTime);
    g_grabbed = 0;
  }
}

// --- audio: ALSA through dlopen -------------------------------------------
// "default" is the PipeWire ioplug on SteamOS (verified on the device: snd_pcm_type()
// reports IOPLUG and the node name comes back as a PipeWire node), so this is not a
// legacy path — it is the shortest route to the sound server every SteamOS session
// already runs.
typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long          snd_pcm_sframes_t;
typedef void (*snd_err_handler_t)(const char *, int, const char *, int, const char *, ...);

#define SND_PCM_STREAM_PLAYBACK       0
#define SND_PCM_FORMAT_FLOAT_LE       14
#define SND_PCM_ACCESS_RW_INTERLEAVED 3

// X(return, name, args, required) — an optional symbol only costs a diagnostic.
#define ALSA_FUNC_LIST                                                        \
  X(int, snd_pcm_open, (snd_pcm_t **, const char *, int, int), 1)             \
  X(int, snd_pcm_close, (snd_pcm_t *), 1)                                     \
  X(int, snd_pcm_set_params,                                                  \
    (snd_pcm_t *, int, int, unsigned, unsigned, int, unsigned), 1)            \
  X(snd_pcm_sframes_t, snd_pcm_writei,                                        \
    (snd_pcm_t *, const void *, snd_pcm_uframes_t), 1)                        \
  X(int, snd_pcm_recover, (snd_pcm_t *, int, int), 1)                         \
  X(int, snd_pcm_prepare, (snd_pcm_t *), 1)                                   \
  X(int, snd_pcm_drop, (snd_pcm_t *), 1)                                      \
  X(int, snd_pcm_get_params,                                                  \
    (snd_pcm_t *, snd_pcm_uframes_t *, snd_pcm_uframes_t *), 0)               \
  X(int, snd_lib_error_set_handler, (snd_err_handler_t), 0)

#define X(ret, name, args, req) static ret (*p_##name) args;
ALSA_FUNC_LIST
#undef X

#define AUDIO_LATENCY_US 20000u  // buffer time; alsa derives period = /4 (5 ms)
#define AUDIO_MAXFR      4096    // scratch ceiling; the period is 240 at 20 ms

// `pcm.!default` is PipeWire on SteamOS; the other two cover a desktop whose default
// landed on a busy hw: device while a sound server is running.
static const char *const AUDIO_DEVS[] = {"default", "pulse", "sysdefault"};

static _Atomic int g_audio_run = 1;
static _Atomic int g_audio_done;   // the thread's own "I have left" flag
static pthread_t   g_audio_th;
static int         g_audio_started;

// alsa-lib's own handler writes a paragraph of internals to stderr when there
// is no sound server. A silent game is silent everywhere.
static void alsa_quiet(const char *f, int l, const char *fn, int e, const char *fmt, ...) {
  (void)f; (void)l; (void)fn; (void)e; (void)fmt;
}

static void *audio_thread(void *param) {
  (void)param;
  static float buf[AUDIO_MAXFR * SFX_CH];
  int attempt = 0;
retry: ;
  snd_pcm_t *pcm = NULL;
  int started = 0;
  double ran = 0;                 // when this stream reached the mix loop
  snd_pcm_uframes_t bufsz = 0, persz = 0;

  // A device has to OPEN and then accept the format; a name that opens but refuses 48
  // kHz float is no more usable than one that does not open, so both failures fall
  // through to the next name. set_params returns -EINVAL rather than substituting a
  // near rate, so a success IS the proof that the stream runs at exactly SFX_HZ — which
  // is why g_dev_rate below is a literal instead of a division.
  for (size_t i = 0; i < sizeof AUDIO_DEVS / sizeof *AUDIO_DEVS; i++) {
    if (p_snd_pcm_open(&pcm, AUDIO_DEVS[i], SND_PCM_STREAM_PLAYBACK, 0) < 0) {
      pcm = NULL;
      continue;
    }
    if (p_snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             SFX_CH, SFX_HZ, 1 /*soft resample*/, AUDIO_LATENCY_US) >= 0)
      break;
    p_snd_pcm_close(pcm);
    pcm = NULL;
  }
  if (!pcm) goto done;

  if (!p_snd_pcm_get_params || p_snd_pcm_get_params(pcm, &bufsz, &persz) < 0 || !persz)
    persz = SFX_HZ / 200;                    // 5 ms, the value set_params picks
  if (persz > AUDIO_MAXFR) persz = AUDIO_MAXFR;

  // MUST precede the first audio_mix: audio_mix reads g_dev_rate unguarded when
  // it advances a voice, so leaving the zero-initialised global alone freezes
  // every voice at sample 0, no voice ever ends, the pool fills permanently and
  // the game goes silent for good.
  g_dev_rate = 1.0f;
  started = 1;
  ran = nat_now();

  int sick = 0;
  while (atomic_load_explicit(&g_audio_run, memory_order_relaxed)) {
    audio_mix(buf, (uint32_t)persz, SFX_CH);
    const float *p = buf;
    snd_pcm_uframes_t left = persz;
    int wrote = 0, zero_write = 0;
    while (left) {
      snd_pcm_sframes_t n = p_snd_pcm_writei(pcm, p, left);
      if (n < 0) {
        // Blocking writei self-paces off the device clock — this is the direct analogue
        // of waiting on the WASAPI event, not a poll.
        if (p_snd_pcm_recover(pcm, (int)n, 1) < 0 && p_snd_pcm_prepare(pcm) < 0) goto done;
        break;                               // drop the rest of this period
      }
      // A ZERO IS NOT PROGRESS. A blocking writei on a well-behaved driver cannot
      // return 0, but pcm.!default on the deploy handheld is the PipeWire ioplug, and a
      // plugin transfer callback can — and a 0 that falls through advances nothing,
      // decrements nothing and sets wrote, which is the one value that disarms the
      // `sick` escape below.
      if (n == 0) { zero_write = 1; break; }
      p += (size_t)n * SFX_CH;
      left -= (snd_pcm_uframes_t)n;
      wrote = 1;
    }
    // A stream that recovers and then immediately fails again is not paced by anything:
    // the loop would mix and fail as fast as the CPU allows, burning a core and never
    // reaching the rebuild below.
    sick = (wrote && !zero_write) ? 0 : sick + 1;
    if (sick > 32) goto done;
  }
done:
  if (pcm) {
    if (started) p_snd_pcm_drop(pcm);
    p_snd_pcm_close(pcm);
  }
  // Same distinction the WASAPI thread documents: rebuild a stream that STARTED and
  // broke (the sound server restarted, the sink went away), never a device that was
  // never there — that would re-enter the open path forever on a machine with no audio
  // at all.
  // AND THE COUNT IS A BURST BUDGET, NOT A LIFETIME ONE. `attempt` never fell back, so
  // eight sound-server restarts spread over an evening ended audio for good. A stream
  // that ran half a minute is a working device; anything flapping faster still walks
  // the eight attempts and stops, which is what AUDIO_RETRY_MAX is for.
  if (started && nat_now() - ran > AUDIO_RETRY_GOOD_MS / 1000.0) attempt = 0;
  if (atomic_load_explicit(&g_audio_run, memory_order_relaxed) &&
      started && ++attempt <= AUDIO_RETRY_MAX) {
    for (int slept = 0; slept < AUDIO_RETRY_MS; slept += AUDIO_RETRY_SLICE_MS) {
      if (!atomic_load_explicit(&g_audio_run, memory_order_relaxed)) goto quit;
      struct timespec ts = {0, AUDIO_RETRY_SLICE_MS * 1000000L};
      nanosleep(&ts, NULL);
    }
    if (!atomic_load_explicit(&g_audio_run, memory_order_relaxed)) goto quit;
    // Stale triggers carry delays measured against a stream that no longer
    // exists; replaying them opens the new one with a burst of ancient sounds.
    atomic_store(&g_trig_r, atomic_load(&g_trig_w));
    goto retry;
  }
quit:
  atomic_store(&g_audio_done, 1);   // the teardown's bounded wait reads this
  return NULL;
}

static void audio_start(void) {
  void *lib = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return;                          // no ALSA: silent game
#define X(ret, name, args, req)                                               \
  p_##name = (ret (*) args)dlsym(lib, #name);                                 \
  if (req && !p_##name) return;
  ALSA_FUNC_LIST
#undef X
  if (p_snd_lib_error_set_handler) p_snd_lib_error_set_handler(alsa_quiet);
  g_audio_started = pthread_create(&g_audio_th, NULL, audio_thread, NULL) == 0;
  // Detached, because the teardown waits on g_audio_done with a bound instead of
  // joining — a wedged sound server may not hold the process open.
  if (g_audio_started) pthread_detach(g_audio_th);
}

// --- platform hooks --------------------------------------------------------
// ONE definition of each in the translation unit.
static void platform_menu_changed(void) {
  g_ui_mdown = 0;
  if (g_headless) return;
  upd_menu_recheck();   // native only, and past the harness gate on purpose
  if (g_menu_open) {
    nat_grab(0);
    // Seed the UI cursor from the real pointer, in WINDOW coordinates: g_ui_cx is real
    // framebuffer pixels with a top-left origin, which is exactly what X11 reports
    // here.
    Window r, c;
    int rx, ry, wx, wy;
    unsigned mask;
    if (XQueryPointer(g_xdpy, g_xwin, &r, &c, &rx, &ry, &wx, &wy, &mask)) {
      g_ui_cx = (float)wx;
      g_ui_cy = (float)wy;
    }
  } else if (g_active) {
    nat_grab(1);
  }
  nat_input_clear();
}

static void platform_vsync_changed(void) {
  if (!g_headless && g_egl_dpy != EGL_NO_DISPLAY)
    eglSwapInterval(g_egl_dpy, g_cfg.vsync ? 1 : 0);
}

// --- window + context ------------------------------------------------------
// The X window MUST use the visual EGL names in EGL_NATIVE_VISUAL_ID; anything else is
// a BadMatch out of XCreateWindow, or a surface eglCreateWindowSurface refuses.
static int nat_window(int w, int h) {
  pfn_eglGetPlatformDisplayEXT get_plat =
      (pfn_eglGetPlatformDisplayEXT)(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (get_plat) g_egl_dpy = get_plat(EGL_PLATFORM_X11_EXT, g_xdpy, NULL);
  if (g_egl_dpy == EGL_NO_DISPLAY) g_egl_dpy = eglGetDisplay((EGLNativeDisplayType)g_xdpy);
  if (g_egl_dpy == EGL_NO_DISPLAY || !eglInitialize(g_egl_dpy, NULL, NULL))
    fatal("EGL: no display");
  if (!eglBindAPI(EGL_OPENGL_API)) fatal("EGL: no desktop GL");

  const EGLint cfg_attr[] = {
    EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0,
    EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 0,
    EGL_NONE
  };
  EGLConfig cfgs[64], cfg = NULL;
  EGLint ncfg = 0;
  if (!eglChooseConfig(g_egl_dpy, cfg_attr, cfgs, 64, &ncfg) || ncfg <= 0)
    fatal("EGL: no matching config");

  XVisualInfo *vi = NULL;
  for (EGLint i = 0; i < ncfg && !vi; i++) {
    EGLint vid = 0;
    eglGetConfigAttrib(g_egl_dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid);
    if (!vid) continue;
    XVisualInfo tmpl = {.visualid = (VisualID)vid};
    int nvi = 0;
    XVisualInfo *got = XGetVisualInfo(g_xdpy, VisualIDMask, &tmpl, &nvi);
    if (got && nvi > 0) { vi = got; cfg = cfgs[i]; }
    else if (got) XFree(got);
  }
  if (!vi) fatal("EGL: no config with a usable X visual");

  // CWBorderPixel is mandatory whenever the visual's depth may differ from the
  // parent's — without it XCreateWindow is a BadMatch.
  XSetWindowAttributes swa = {
    .colormap = XCreateColormap(g_xdpy, g_xroot, vi->visual, AllocNone),
    .background_pixel = 0,
    .border_pixel = 0,
    .event_mask = StructureNotifyMask | FocusChangeMask | KeyPressMask |
                  KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                  PointerMotionMask | EnterWindowMask,
  };
  g_xwin = XCreateWindow(g_xdpy, g_xroot, 0, 0, (unsigned)w, (unsigned)h, 0,
                         vi->depth, InputOutput, vi->visual,
                         CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &swa);
  XFree(vi);
  if (!g_xwin) fatal("X11: cannot create window");

  XStoreName(g_xdpy, g_xwin, "skill-issue");
  XClassHint ch = {(char *)"skill-issue", (char *)"skill-issue"};
  XSetClassHint(g_xdpy, g_xwin, &ch);
  XSetWMProtocols(g_xdpy, g_xwin, &A_WM_DELETE, 1);
  long bypass = 1;   // "please turn the compositor off while I am visible"
  XChangeProperty(g_xdpy, g_xwin, A_NET_WM_BYPASS_COMP, XA_CARDINAL, 32,
                  PropModeReplace, (unsigned char *)&bypass, 1);
  // Borderless fullscreen with no display-mode change, the same design as the Windows
  // popup: the EWMH hint, set as a PROPERTY because the window is not mapped yet (after
  // mapping it would have to be a ClientMessage to the root).
  if (g_fullscreen)
    XChangeProperty(g_xdpy, g_xwin, A_NET_WM_STATE, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&A_NET_WM_FULLSCREEN, 1);

  g_egl_surf = eglCreateWindowSurface(g_egl_dpy, cfg, (EGLNativeWindowType)g_xwin, NULL);
  if (g_egl_surf == EGL_NO_SURFACE) fatal("EGL: cannot create window surface");
  const EGLint ctx_attr[] = {
    EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_NONE
  };
  g_egl_ctx = eglCreateContext(g_egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
  if (g_egl_ctx == EGL_NO_CONTEXT) fatal("EGL: no GL 3.3 core context");
  if (!eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_egl_ctx))
    fatal("EGL: cannot make the context current");
  gl_load_functions((gl_loader_fn)(void *)eglGetProcAddress);

  char zero[8] = {0};                        // invisible cursor, no Xfixes
  Pixmap pm = XCreateBitmapFromData(g_xdpy, g_xwin, zero, 1, 1);
  XColor black = {0};
  g_xblank = XCreatePixmapCursor(g_xdpy, pm, pm, &black, &black, 0, 0);
  XFreePixmap(g_xdpy, pm);
  return 1;
}

// Raw mouse via XInput2, or nothing.
static int nat_xi2(void) {
  int ev = 0, err = 0;
  if (!XQueryExtension(g_xdpy, "XInputExtension", &g_xi_op, &ev, &err)) return 0;
  void *lib = dlopen("libXi.so.6", RTLD_NOW | RTLD_LOCAL);
  if (!lib) return 0;
  p_XIQueryVersion = (pfn_XIQueryVersion)(void *)dlsym(lib, "XIQueryVersion");
  p_XISelectEvents = (pfn_XISelectEvents)(void *)dlsym(lib, "XISelectEvents");
  if (!p_XIQueryVersion || !p_XISelectEvents) return 0;
  // 2.0 is all raw motion needs; asking higher on an old server just returns the
  // server's own version.
  int major = 2, minor = 0;
  if (p_XIQueryVersion(g_xdpy, &major, &minor) != Success || major < 2) return 0;
  // Optional: without these the absolute-device filter cannot run and every
  // source is trusted, which is the behaviour before the filter existed.
  p_XIQueryDevice    = (pfn_XIQueryDevice)(void *)dlsym(lib, "XIQueryDevice");
  p_XIFreeDeviceInfo = (pfn_XIFreeDeviceInfo)(void *)dlsym(lib, "XIFreeDeviceInfo");
  unsigned char mask[XI_MASK_BYTES] = {0};
  mask[XI_RAW_MOTION >> 3] |= (unsigned char)(1u << (XI_RAW_MOTION & 7));
  // A second selection for HierarchyChanged: X device ids are server-recycled, so an
  // unplug/replug can hand a mouse the id of a cached-absolute touchscreen (every
  // motion dropped, no recovery) or vice versa — the event is what invalidates the
  // xi_relative cache.
  unsigned char hmask[XI_MASK_BYTES] = {0};
  hmask[XI_HIERARCHY_CHANGED >> 3] |= (unsigned char)(1u << (XI_HIERARCHY_CHANGED & 7));
  xi_evmask_t em[2] = {
    {.deviceid = XI_ALL_MASTER_DEVICES, .mask_len = XI_MASK_BYTES, .mask = mask},
    {.deviceid = XI_ALL_DEVICES, .mask_len = XI_MASK_BYTES, .mask = hmask},
  };
  int before = g_xerr;
  p_XISelectEvents(g_xdpy, g_xroot, em, 2);
  XSync(g_xdpy, False);
  return g_xerr == before;
}

// Does this raw event's source report RELATIVE motion? Asked once per device and
// cached.
static int xi_relative(int sourceid) {
  if (sourceid < 0 || sourceid >= XI_DEV_MAX || !p_XIQueryDevice) return 1;
  if (g_xi_dev[sourceid]) return g_xi_dev[sourceid] > 0;
  int n = 0;
  xi_devinfo_t *info = p_XIQueryDevice(g_xdpy, sourceid, &n);
  signed char verdict = 1;
  if (info && n > 0) {
    for (int c = 0; c < info->num_classes; c++) {
      const xi_anyclass_t *any = info->classes[c];
      if (!any || any->type != XI_VALUATOR_CLASS) continue;
      const xi_valclass_t *v = (const xi_valclass_t *)any;
      // Valuators 0 and 1 are x and y on every pointer; a device that reports
      // either of them as an absolute coordinate puts a POSITION in raw_values.
      if (v->number <= 1 && v->mode == XI_MODE_ABSOLUTE) verdict = -1;
    }
  }
  // Cache only a verdict the server actually answered: caching the fail-open default on
  // a failed query (an unplug race) would pin id-recycled hardware to a stale
  // classification.
  if (info) {
    if (p_XIFreeDeviceInfo) p_XIFreeDeviceInfo(info);
    g_xi_dev[sourceid] = verdict;
  }
  return verdict > 0;
}

// --- event pump ------------------------------------------------------------
// Mirrors wndproc's ordering exactly, and the order is the contract: escape
// first, then the name field, then a pending rebind, and only what survives all
// three reaches the key arrays.
static void nat_key(XKeyEvent *ke, int down) {
  KeySym ks = XkbKeycodeToKeysym(g_xdpy, (KeyCode)ke->keycode, 0, 0);
  if (ks == XK_Escape) {
    // The PRESS EDGE only.
    if (down && !g_esc_held) ui_on_escape();
    g_esc_held = down;
    return;
  }
  int k = key_from_keysym(ks);
  if (down && g_ui_name_edit) {
    if (ks == XK_BackSpace) ui_name_key("back");
    else if (ks == XK_Return || ks == XK_KP_Enter) ui_name_key("enter");
    else {
      const char *tok = token_from_key(k);
      if (tok && !tok[1]) ui_name_key(tok);
    }
    return;
  }
  if (down && g_ui_rebind >= 0) {
    const char *tok = token_from_key(k);
    // A CAPTURE MUST HAVE A WAY OUT ON EVERY DEVICE. This branch swallows every key
    // while it is armed, and key_from_keysym knows neither Return nor Backspace — so on
    // a handheld whose physical pad Steam has hidden (arrow keys, no pad, and therefore
    // no B) the only exit was the ESC key, which that machine may not have either.
    if (tok) ui_assign_bind(tok);
    else if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_BackSpace)
      g_ui_rebind = -1;
    return;
  }
  // Arrow keys + Enter drive the menu like the pad.
  if (down && g_menu_open) {
    // PgUp/PgDn switch tabs: without them the keyboard path could drive every
    // row but never reach the GAMEPAD tab — the one holding the PAD HELD BY
    // STEAM explanation, in exactly the state where only the keyboard works.
    int nav = ks == XK_Up     ? NAV_UP
            : ks == XK_Down   ? NAV_DOWN
            : ks == XK_Left   ? NAV_LEFT
            : ks == XK_Right  ? NAV_RIGHT
            : ks == XK_Prior  ? NAV_TABL
            : ks == XK_Next   ? NAV_TABR
            : ks == XK_Return || ks == XK_KP_Enter ? NAV_OK : -1;
    if (nav >= 0) { g_nav_kbd = 1; nav_push(nav); return; }
  }
  if (!k) return;
  if (down) {
    if (!g_active) return;   // a keystroke aimed at another window
    if (!g_menu_open) g_pressed[k] = 1;
    g_keys[k] = 1;
  } else {
    g_keys[k] = 0;           // a release always counts, or the key sticks down
  }
}

static void nat_button(unsigned int b, int down) {
  int k = key_from_button(b);
  if (!k) return;
  // A wheel notch has no LEVEL: X reports it as a press followed immediately by a
  // release, so writing g_keys would leave a key that is down and up inside the same
  // frame — the latch is the whole signal, and the release is dropped rather than
  // clearing it.
  if (k == KEY_MWUP || k == KEY_MWDN) {
    if (down && g_active && !g_menu_open) g_pressed[k] = 1;
    return;
  }
  if (g_menu_open) {
    if (!down) { if (k == KEY_MOUSE1) g_ui_mdown = 0; return; }
    if (g_ui_rebind >= 0) ui_assign_bind(token_from_key(k));
    else if (k == KEY_MOUSE1) g_ui_mdown = 1;
    return;
  }
  // THE CLICK THAT REFOCUSES THE GAME MUST NOT FIRE. That is what wndproc's `case
  // WM_MOUSEACTIVATE: return MA_ACTIVATEANDEAT` buys on Windows, and X11 has no
  // equivalent because the WM replays the activating click to the client as an ordinary
  // ButtonPress — so without this, clicking back into the game after an alt-tab spends
  // a round, with recoil and audio.
  if (!g_active) return;
  if (down && nat_now() - g_focus_t < 0.15) return;
  if (down) { g_pressed[k] = 1; g_keys[k] = 1; }
  else      g_keys[k] = 0;
}

static void nat_motion(int x, int y) {
  if (g_menu_open) {
    // g_ui_cx/cy are real framebuffer pixels, top-left origin — which is precisely X11
    // window coordinates.
    g_ui_cx = (float)x;
    g_ui_cy = (float)y;
    return;
  }
  if (g_xi_on || !g_grabbed) return;
  // Warp fallback (libXi missing).
  if (g_warp_sync) {
    g_warp_sync = 0;
    g_warp_x = x;
    g_warp_y = y;
    return;
  }
  int dx = x - g_warp_x, dy = y - g_warp_y;
  g_warp_x = x;
  g_warp_y = y;
  if (!dx && !dy) return;
  g_mouse_dx += (float)dx;
  g_mouse_dy += (float)dy;
  // Only warp near an edge: a warp per event is a round trip per event and generates
  // its own event storm.
  const int margin = 64;
  if (x < margin || y < margin || x > R.width - margin || y > R.height - margin)
    nat_warp_arm();
}

static void nat_pump(void) {
  while (XPending(g_xdpy)) {
    XEvent e;
    XNextEvent(g_xdpy, &e);

    // A GenericEvent cookie has to be fetched before anything else touches the queue
    // and freed afterwards.
    if (e.type == GenericEvent && g_xi_on && e.xcookie.extension == g_xi_op) {
      if (XGetEventData(g_xdpy, &e.xcookie)) {
        const xi_raw_event_t *re = (const xi_raw_event_t *)e.xcookie.data;
        if (e.xcookie.evtype == XI_HIERARCHY_CHANGED) {
          // Device ids are recycled across unplug/replug; drop every cached
          // relative/absolute verdict and let the next event re-classify.
          memset(g_xi_dev, 0, sizeof g_xi_dev);
        } else if (e.xcookie.evtype == XI_RAW_MOTION && g_active && !g_menu_open) {
          if (xi_relative(re->sourceid)) {
            // raw_values is PACKED: one double per SET bit of the valuator mask, in
            // valuator order, so the read pointer advances for bits we ignore.
            const double *v = re->raw_values;
            double d[2] = {0, 0};
            for (int i = 0; i < re->valuators.mask_len * 8; i++) {
              if (!(re->valuators.mask[i >> 3] & (1u << (i & 7)))) continue;
              if (i < 2) d[i] = *v;
              v++;
            }
            g_mouse_dx += (float)d[0];
            g_mouse_dy += (float)d[1];
            g_xi_used++;
          } else if (++g_xi_dropped >= 10 && !g_xi_used) {
            // THE FILTER MUST NEVER THROW AWAY THE LAST POINTER. Under XWayland —
            // gamescope and every Wayland desktop session — the ordinary pointer IS an
            // absolute device (measured: `xwayland-pointer` reports valuators 0/1 as
            // absolute) and a RELATIVE one only exists while the compositor honours a
            // pointer lock.
            g_xi_on = 0;
            nat_warp_arm();
          }
        }
        XFreeEventData(g_xdpy, &e.xcookie);
      }
      continue;
    }

    switch (e.type) {
    case ClientMessage:
      if (e.xclient.message_type == A_WM_PROTOCOLS &&
          (Atom)e.xclient.data.l[0] == A_WM_DELETE) {
        if (g_menu_open) config_write(g_cfg_path, &g_cfg);  // closed mid-edit
        g_running = 0;
      }
      break;
    case MapNotify:
      // Mapping is what makes the game live, not the focus event: gamescope in game
      // mode has exactly one window and need never send FocusIn, and a backend that
      // waits for one would sit in its paused branch forever showing a black screen.
      g_mapped = 1;
      g_active = 1;
      g_focus_t = nat_now();
      pad_eat_held();  // the press that refocused the game must not fire
      nat_cursor(0);
      nat_grab(1);
      break;
    case UnmapNotify:
      // Minimised or switched away by the WM. g_active has to go too, or the
      // game keeps simulating, rendering and swapping into a window nobody can
      // see — on a handheld that is battery burnt for nothing.
      g_mapped = 0;
      g_active = 0;
      nat_input_clear();
      nat_grab(0);
      break;
    case DestroyNotify:
      // The window went away without the connection dropping (a WM kill).
      g_running = 0;
      break;
    case ConfigureNotify:
      // gamescope hands a game whatever its nested output is, so this is the normal
      // path on the Deck rather than an edge case.
      if (e.xconfigure.width != R.width || e.xconfigure.height != R.height) {
        R.width = e.xconfigure.width;
        R.height = e.xconfigure.height;
        glViewport(0, 0, R.width, R.height);
        nat_warp_arm();
        // Docking the handheld changes the panel AND its refresh rate, and the
        // frame cap is derived from the latter.
        g_refresh_hz = nat_refresh_hz();
      }
      break;
    case FocusIn:
    case FocusOut:
      // NotifyGrab / NotifyUngrab are NOT focus changes.
      if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab) break;
      if (e.xfocus.detail == NotifyPointer || e.xfocus.detail == NotifyPointerRoot) break;
      if (e.type == FocusIn) {
        g_active = 1;
        g_focus_t = nat_now();   // see nat_button: the activating click
        pad_eat_held();          // pad analogue of the activating-click swallow
        nat_cursor(0);
        if (!g_menu_open) nat_grab(1);
        nat_warp_arm();
      } else {
        // Alt-tabbed away: give the cursor back, release the pointer and drop
        // all input so nothing sticks down while the game is in the background.
        g_active = 0;
        nat_input_clear();
        nat_grab(0);
        nat_cursor(1);
      }
      break;
    case EnterNotify:
      nat_warp_arm();
      break;
    case MappingNotify:
      // The layout can change WHILE the game runs.
      XRefreshKeyboardMapping(&e.xmapping);
      if (e.xmapping.request == MappingKeyboard) nat_input_clear();
      break;
    case KeyPress:   nat_key(&e.xkey, 1); break;
    case KeyRelease:
      // If the server refused detectable auto-repeat, filter the pairs by hand: a
      // repeat is a KeyRelease immediately followed by a KeyPress with the SAME keycode
      // and the SAME timestamp, and a genuine release never is.
      if (!g_detect_repeat && XEventsQueued(g_xdpy, QueuedAfterReading)) {
        XEvent nx;
        XPeekEvent(g_xdpy, &nx);
        if (nx.type == KeyPress && nx.xkey.keycode == e.xkey.keycode &&
            nx.xkey.time == e.xkey.time) {
          XNextEvent(g_xdpy, &nx);   // swallow the pair: the key never came up
          break;
        }
      }
      nat_key(&e.xkey, 0);
      break;
    case ButtonPress:   nat_button(e.xbutton.button, 1); break;
    case ButtonRelease: nat_button(e.xbutton.button, 0); break;
    case MotionNotify:  nat_motion(e.xmotion.x, e.xmotion.y); break;
    default: break;
    }
  }
}

