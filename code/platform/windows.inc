// =============================================================================
// platform/windows.inc  —  WGL context creation, RawInput, WASAPI, WinHTTP,
// WinMain. Compile-checked only — this container cannot run it.
// Proof: the MinGW warning gate. One TU: included once by game.c, in
// definition order — moving a definition out is a semantic change.
// Contract: compiled with WIN_ARCH (-march=x86-64-v2, Makefile) — must not
// leak into the Linux build; -Wall -Wextra -Wshadow clean under
// x86_64-w64-mingw32-gcc.
// =============================================================================

#define WGL_CONTEXT_MAJOR_VERSION_ARB    0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB    0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB     0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x0001

typedef HGLRC (WINAPI *pfn_wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
typedef BOOL  (WINAPI *pfn_wglSwapIntervalEXT)(int);

// --- UDP socket backend, Winsock half(-lws2_32) ---
#include <winsock2.h>
#include <ws2tcpip.h>
// Winsock has to be up before ANY ws2_32 call, and getaddrinfo is one of them.
static int net_wsa_init(void) {
  static int inited = 0;
  if (!inited) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0; inited = 1; }
  return 1;
}
static int net_udp_open(int port) {
  if (!net_wsa_init()) return -1;
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) return -1;
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);                // non-blocking
  if (port > 0) {
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) { closesocket(s); return -1; }
  }
  return (int)s;
}
static void net_udp_close(int fd) { if (fd >= 0) closesocket((SOCKET)fd); }
static int net_udp_send(int fd, uint32_t ip, uint16_t port, const void *b, int n) {
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = ip;
  a.sin_port = htons(port);
  return sendto((SOCKET)fd, b, n, 0, (struct sockaddr *)&a, sizeof a);
}
static int net_udp_recv(int fd, uint32_t *ip, uint16_t *port, void *b, int cap) {
  struct sockaddr_in a = {0};
  int al = sizeof a;
  int r = recvfrom((SOCKET)fd, b, cap, 0, (struct sockaddr *)&a, &al);
  if (r == SOCKET_ERROR) return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1;
  if (ip) *ip = a.sin_addr.s_addr;
  if (port) *port = ntohs(a.sin_port);
  return r;
}
static uint32_t net_resolve(const char *host) {
  if (!net_wsa_init()) return 0;             // getaddrinfo IS a ws2_32 call
  struct addrinfo hints = {0}, *res = NULL;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return 0;
  uint32_t out = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
  freeaddrinfo(res);
  return out;
}
static uint32_t net_now_ms(void) { return (uint32_t)GetTickCount(); }

// g_keys/g_pressed are indexed by VK code, which IS the shared KEY_* space —
// see the key layer above the frame step. Nothing here maps anything.
static float g_mouse_dx, g_mouse_dy;
static int   g_active, g_running = 1;
static HWND  g_hwnd;

static void win_clip_cursor(void) {
  RECT r;
  GetWindowRect(g_hwnd, &r);
  ClipCursor(&r);
}

static void win_set_cursor_visible(int visible) {
  static int shown = 1;
  if (visible == shown) return;
  shown = visible;
  ShowCursor(visible);
}

static pfn_wglSwapIntervalEXT g_swap_interval;

static void platform_vsync_changed(void) {
  if (g_swap_interval) g_swap_interval(g_cfg.vsync);
}

// --- Gamepad via XInput + DirectInput8 -------------------------------------
// Two adapters, tried in that order; both feed the shared mappers and neither writes
// g_pad on a failed read, so stale state cannot linger.

// DirectInput 8, dynamically loaded.
typedef struct { LONG ax[PH_AXES]; DWORD pov; BYTE btn[32]; } win_di_state_t;
static_assert(sizeof(win_di_state_t) == 68, "DirectInput state layout");

// GUID_XAxis..GUID_Slider and GUID_POV by value (no dxguid import); the axis
// family differs only in Data1, and the array index IS the PH_ slot.
#define WIN_DI_GUID(d1) \
  {d1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}}
static const GUID WIN_DI_AXIS_GUID[PH_AXES] = {
  WIN_DI_GUID(0xA36D02E0), WIN_DI_GUID(0xA36D02E1),   // X   Y
  WIN_DI_GUID(0xA36D02E2), WIN_DI_GUID(0xA36D02F4),   // Z   Rx
  WIN_DI_GUID(0xA36D02F5), WIN_DI_GUID(0xA36D02E3),   // Ry  Rz
  WIN_DI_GUID(0xA36D02E4), WIN_DI_GUID(0xA36D02E4),   // Slider x2
};
static const GUID WIN_DI_POV_GUID = WIN_DI_GUID(0xA36D02F2);
static const IID WIN_DI_IID_8W =
  {0xBF798031, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
typedef HRESULT (WINAPI *pfn_DirectInput8Create)(HINSTANCE, DWORD, REFIID,
                                                 LPVOID *, LPUNKNOWN);

static HMODULE g_dinput_module;
static LPDIRECTINPUT8W g_dinput;
static LPDIRECTINPUTDEVICE8W g_dinput_device;
static DIOBJECTDATAFORMAT g_di_odf[PH_AXES + 1 + 32];
static uint32_t g_di_axes;      // presence mask over PH_, from EnumObjects
static int      g_di_has_pov;
static uint16_t g_di_vid;       // keys the shared button-layout table
static int      g_di_live;      // a real input report has been seen
static int      g_di_fail;      // consecutive dead reads -> re-enumerate
static double   g_di_rescan;    // next EnumDevices while no device is open

static void win_di_format_init(void) {
  int n = 0;
  for (int i = 0; i < PH_AXES; i++)
    g_di_odf[n++] = (DIOBJECTDATAFORMAT){
      &WIN_DI_AXIS_GUID[i], (DWORD)offsetof(win_di_state_t, ax[i]),
      DIDFT_AXIS | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, DIDOI_ASPECTPOSITION};
  g_di_odf[n++] = (DIOBJECTDATAFORMAT){
    &WIN_DI_POV_GUID, (DWORD)offsetof(win_di_state_t, pov),
    DIDFT_POV | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0};
  for (int i = 0; i < 32; i++)
    g_di_odf[n++] = (DIOBJECTDATAFORMAT){
      NULL, (DWORD)offsetof(win_di_state_t, btn[i]),
      DIDFT_BUTTON | DIDFT_ANYINSTANCE | DIDFT_OPTIONAL, 0};
}

// Present axes get an explicit symmetric DIPROP_RANGE so the pump's /32767 is exact by
// construction — DirectInput's default 0..65535 scale plus a first-sample "calibration"
// is what turned a late first Bluetooth report into a permanently pinned look axis.
static BOOL CALLBACK win_dinput_object(LPCDIDEVICEOBJECTINSTANCEW o, LPVOID ctx) {
  (void)ctx;
  if (o->dwType & DIDFT_POV) { g_di_has_pov = 1; return DIENUM_CONTINUE; }
  if (!(o->dwType & DIDFT_AXIS)) return DIENUM_CONTINUE;
  int ph = -1;
  for (int i = 0; i < PH_AXES; i++)
    if (!memcmp(&o->guidType, &WIN_DI_AXIS_GUID[i], sizeof(GUID))) { ph = i; break; }
  if (ph == PH_S0 && (g_di_axes & 1u << PH_S0)) ph = PH_S1;  // second slider
  if (ph < 0 || (g_di_axes & 1u << ph)) return DIENUM_CONTINUE;
  DIPROPRANGE range = {
    .diph = {sizeof range, sizeof range.diph, o->dwType, DIPH_BYID},
    .lMin = -32767, .lMax = 32767,
  };
  if (FAILED(g_dinput_device->lpVtbl->SetProperty(g_dinput_device, DIPROP_RANGE,
                                                  &range.diph)))
    return DIENUM_CONTINUE;   // axis refuses the range: treat as absent
  DIPROPDWORD dz = {
    .diph = {sizeof dz, sizeof dz.diph, o->dwType, DIPH_BYID}, .dwData = 0,
  };
  g_dinput_device->lpVtbl->SetProperty(g_dinput_device, DIPROP_DEADZONE, &dz.diph);
  g_di_axes |= 1u << ph;
  return DIENUM_CONTINUE;
}

static BOOL CALLBACK win_dinput_pick(LPCDIDEVICEINSTANCEW inst, LPVOID ctx) {
  (void)ctx;
  LPDIRECTINPUTDEVICE8W dev = NULL;
  if (FAILED(g_dinput->lpVtbl->CreateDevice(g_dinput, &inst->guidInstance,
                                            &dev, NULL)))
    return DIENUM_CONTINUE;
  g_dinput_device = dev;
  // A HID product GUID carries MAKELONG(vid, pid) in Data1 and literally
  // spells "PIDVID" in its tail; anything else gets the generic layout.
  g_di_vid = memcmp(&inst->guidProduct.Data4[2], "PIDVID", 6) == 0
           ? (uint16_t)inst->guidProduct.Data1 : 0;
  return DIENUM_STOP;
}

static void win_dinput_close(double now) {
  if (g_dinput_device) {
    g_dinput_device->lpVtbl->Unacquire(g_dinput_device);
    g_dinput_device->lpVtbl->Release(g_dinput_device);
  }
  g_dinput_device = NULL;
  g_di_axes = 0;
  g_di_has_pov = g_di_live = g_di_fail = 0;
  g_di_vid = 0;
  g_di_rescan = now + 2.0;
}

static void win_dinput_scan(double now) {
  if (!g_dinput || g_dinput_device) return;
  if (FAILED(g_dinput->lpVtbl->EnumDevices(g_dinput, DI8DEVCLASS_GAMECTRL,
                                           win_dinput_pick, NULL,
                                           DIEDFL_ATTACHEDONLY)) ||
      !g_dinput_device)
    return;
  DIDATAFORMAT fmt = {
    sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_ABSAXIS,
    sizeof(win_di_state_t), sizeof g_di_odf / sizeof g_di_odf[0], g_di_odf,
  };
  // Properties only apply while unacquired, hence the order; a device that
  // fails any step is closed and the 2 s rescan finds the next one.
  if (FAILED(g_dinput_device->lpVtbl->SetDataFormat(g_dinput_device, &fmt)) ||
      FAILED(g_dinput_device->lpVtbl->SetCooperativeLevel(
        g_dinput_device, g_hwnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)) ||
      FAILED(g_dinput_device->lpVtbl->EnumObjects(g_dinput_device,
        win_dinput_object, NULL, DIDFT_AXIS | DIDFT_POV)) ||
      FAILED(g_dinput_device->lpVtbl->Acquire(g_dinput_device)))
    win_dinput_close(now);
}

static int win_dinput_pump(pad_state_t *out, double now) {
  if (!g_dinput) return 0;
  if (!g_dinput_device) {
    // A Bluetooth pad usually connects AFTER launch; enumerate on the same
    // 2 s cadence as the XInput slot scan and the evdev node scan.
    if (now < g_di_rescan) return 0;
    g_di_rescan = now + 2.0;
    win_dinput_scan(now);
    if (!g_dinput_device) return 0;
  }
  win_di_state_t st;
  g_dinput_device->lpVtbl->Poll(g_dinput_device);  // DI_NOEFFECT on most pads
  HRESULT hr = g_dinput_device->lpVtbl->GetDeviceState(g_dinput_device,
                                                       sizeof st, &st);
  if (FAILED(hr)) {
    if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED)
      g_dinput_device->lpVtbl->Acquire(g_dinput_device);
    // A pad that stays dead through the re-Acquire was turned off; it
    // re-pairs as a NEW HID instance, so drop this one and re-enumerate
    // instead of retrying a stale instance GUID forever.
    if (++g_di_fail > 240) win_dinput_close(now);
    return 0;
  }
  g_di_fail = 0;
  // A freshly acquired Bluetooth device answers DI_OK with an all-zero struct until its
  // first input report lands (SDL #3816); latching that as real state was the old
  // first-sample-calibration bug.
  if (!g_di_live) {
    if (!g_di_has_pov) g_di_live = 1;   // povless: all-zero IS neutral
    const unsigned char *p = (const unsigned char *)&st;
    for (size_t i = 0; i < sizeof st && !g_di_live; i++) g_di_live = p[i] != 0;
    if (!g_di_live) return 0;
  }
  pad_hid_raw_t raw = {
    .connected = 1, .axes = g_di_axes, .vid = g_di_vid,
    .pov = g_di_has_pov && (st.pov & 0xFFFFu) != 0xFFFFu
         ? (int)(st.pov & 0xFFFFu) : -1,
  };
  for (int i = 0; i < PH_AXES; i++)   // absent slots stay 0 for the mapper
    if (g_di_axes & 1u << i)
      raw.ax[i] = f_clamp((float)st.ax[i] / 32767.0f, -1.0f, 1.0f);
  for (int i = 0; i < 32; i++)
    if (st.btn[i] & 0x80) raw.buttons |= 1u << i;
  pad_apply_hid(out, &raw);
  return 1;
}

static void win_dinput_load(double now) {
  HMODULE m = LoadLibraryA("dinput8.dll");
  if (!m) return;
  pfn_DirectInput8Create create = (pfn_DirectInput8Create)(void *)
    GetProcAddress(m, "DirectInput8Create");
  if (!create ||
      FAILED(create(GetModuleHandleA(NULL), DIRECTINPUT_VERSION, &WIN_DI_IID_8W,
                    (LPVOID *)&g_dinput, NULL)) || !g_dinput) {
    g_dinput = NULL;
    FreeLibrary(m);
    return;
  }
  g_dinput_module = m;
  win_di_format_init();
  win_dinput_scan(now);
}

static void win_dinput_shutdown(void) {
  win_dinput_close(0.0);
  if (g_dinput) g_dinput->lpVtbl->Release(g_dinput);
  if (g_dinput_module) FreeLibrary(g_dinput_module);
  g_dinput = NULL;
  g_dinput_module = NULL;
}

// XInput is reached by LoadLibrary so the build keeps zero new link-time dependencies
// and a machine without either controller DLL simply has no pad.
typedef struct {
  WORD  wButtons;
  BYTE  bLeftTrigger, bRightTrigger;
  SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} win_xi_gamepad_t;
typedef struct { DWORD dwPacketNumber; win_xi_gamepad_t Gamepad; } win_xi_state_t;
static_assert(sizeof(win_xi_state_t) == 16, "XINPUT_STATE ABI");
static_assert(sizeof(win_xi_gamepad_t) == sizeof(pad_xinput_raw_t), "XINPUT raw sample ABI");
typedef DWORD (WINAPI *pfn_XInputGetState)(DWORD, win_xi_state_t *);
static pfn_XInputGetState g_xi_get_state;
static int      g_xi_slot = -1;
static double   g_xi_rescan;
static unsigned g_xi_live;      // bit s = slot s answered on the last scan

static void win_pad_load(void) {
  win_dinput_load(0.0);
  HMODULE m = LoadLibraryA("xinput1_4.dll");           // Win8+
  if (!m) m = LoadLibraryA("xinput9_1_0.dll");         // the OS-shipped fallback
  if (m) g_xi_get_state = (pfn_XInputGetState)(void *)GetProcAddress(m, "XInputGetState");
}

// IS ANYTHING BEING PRESSED ON THIS PAD. Levels, not edges — which is all XInput offers
// and all the hand-over below needs, since it only ever asks "is the pad I am reading
// idle while another one is not".
static int win_xi_busy(const win_xi_gamepad_t *g) {
  const SHORT DZ = 16384;
  return g->wButtons != 0 || g->bLeftTrigger > 128 || g->bRightTrigger > 128 ||
         g->sThumbLX > DZ || g->sThumbLX < -DZ ||
         g->sThumbLY > DZ || g->sThumbLY < -DZ ||
         g->sThumbRX > DZ || g->sThumbRX < -DZ ||
         g->sThumbRY > DZ || g->sThumbRY < -DZ;
}

// THE SAME DEFECT THE EVDEV PUMP HAD, MIRRORED: this used to latch the FIRST slot that
// answered and keep it for the session, so a machine with two pads connected — a
// handheld's built-in controls plus an 8BitDo on its 2.4 GHz dongle, the exact case
// measured on the deploy device — played whichever one Windows happened to enumerate
// first and ignored the one in the player's hands. "First slot" and "last event minor"
// are the same guess wearing different clothes, and neither knows anything about hands.
static int win_xinput_pump(pad_state_t *out, double now) {
  if (!g_xi_get_state) return 0;
  win_xi_state_t st;
  // Empty XInput slots are expensive to poll (a device enumeration inside the call), so
  // which slots EXIST is refreshed on a 2 s timer rather than every frame.
  if (now >= g_xi_rescan) {
    g_xi_rescan = now + 2.0;
    g_xi_live = 0;
    for (DWORD i = 0; i < 4; i++)
      if (g_xi_get_state(i, &st) == 0 /* ERROR_SUCCESS */) g_xi_live |= 1u << i;
    if (g_xi_slot >= 0 && !(g_xi_live & (1u << g_xi_slot))) g_xi_slot = -1;
    if (g_xi_slot < 0)
      for (int i = 0; i < 4; i++)
        if (g_xi_live & (1u << i)) { g_xi_slot = i; break; }
  }
  if (g_xi_slot < 0) return 0;
  if (g_xi_get_state((DWORD)g_xi_slot, &st) != 0) {
    g_xi_slot = -1;
    g_xi_live = 0;
    g_xi_rescan = now + 2.0;
    return 0;
  }
  // HAND-OVER: only while the active pad is idle, and only to a slot that is not.
  if (!win_xi_busy(&st.Gamepad)) {
    for (int i = 0; i < 4; i++) {
      if (i == g_xi_slot || !(g_xi_live & (1u << i))) continue;
      win_xi_state_t o;
      if (g_xi_get_state((DWORD)i, &o) != 0) { g_xi_live &= ~(1u << i); continue; }
      if (win_xi_busy(&o.Gamepad)) { g_xi_slot = i; st = o; break; }
    }
  }
  const win_xi_gamepad_t *g = &st.Gamepad;
  // SHORT is -32768..32767; pad_apply_xinput clamps the one-step negative
  // overshoot. XInput Y is already up-positive, matching the shared contract.
  pad_apply_xinput(out, &(pad_xinput_raw_t){
    .buttons = g->wButtons, .lt = g->bLeftTrigger, .rt = g->bRightTrigger,
    .lx = g->sThumbLX, .ly = g->sThumbLY,
    .rx = g->sThumbRX, .ry = g->sThumbRY,
  });
  return 1;
}

// BOTH APIs ARE POLLED AND THE TOUCHED ONE WINS — the same rule the evdev pump applies
// across nodes and win_xinput_pump applies across slots, now applied across the two
// Windows APIs as well.
//
// This used to `return` on the first API that answered, and win_xinput_pump answers
// whenever any slot reads, whether or not it is being touched. So win_dinput_pump was
// unreachable for the process lifetime while any XInput pad was present — and
// `pad_apply_hid`, the whole 8BitDo/Sony/Switch mapping table, has exactly ONE shipping
// call site, inside it. On a handheld with built-in XInput controls plus a
// DirectInput-only pad on a dongle, the pad in the player's hands did nothing, every
// call succeeded and nothing reported an error: the defect measured on the deploy
// device, mirrored across APIs instead of across slots.
//
// The incumbent must go QUIET before anything takes the session from it, for the same
// reason the evdev pump's quiet rule exists: a controller and a virtual twin of it are
// both candidates reporting one thumb.
//
// COMPILE-CHECKED ONLY in the dev container. `padscan` on the device is the proof, and
// it is the only thing that can answer "is the pad in my hands the one driving".
static void win_pad_pump(double now) {
  static int src;                     // 0 = none, 1 = XInput, 2 = DirectInput
  pad_state_t xi = {0}, di = {0};
  int hx = win_xinput_pump(&xi, now);
  int hd = win_dinput_pump(&di, now);
  if (!hx && !hd) { memset(&g_pad, 0, sizeof g_pad); src = 0; return; }
  if ((src == 1 && !hx) || (src == 2 && !hd)) src = 0;   // the incumbent went away
  if (!src) src = hx ? 1 : 2;
  if (src == 1 && hd && !pad_touched(&xi) && pad_touched(&di)) src = 2;
  else if (src == 2 && hx && !pad_touched(&di) && pad_touched(&xi)) src = 1;
  g_pad = src == 1 ? xi : di;
}

// --- WASAPI output: shared mode, event-driven, ~20 ms buffer. -------------
// COM in plain C: GUIDs defined by value (no -luuid), vtbl calls throughout.
// Every failure path degrades to a silent game — audio must never crash it.

static const CLSID CLSID_MMDevEnum_ =
  {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const IID IID_IMMDevEnum_ =
  {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const IID IID_IAudioClient_ =
  {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const IID IID_IAudioRenderClient_ =
  {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};

// _Atomic, not volatile: the main thread writes it while the WASAPI thread
// reads it, and under ISO C volatile is no synchronization at all — the Linux
// half (and g_trig_w/r, and cfg.vol) already use the atomic idiom.
static _Atomic LONG g_audio_run = 1;
static HANDLE       g_audio_h;     // joined on quit, see WinMain's teardown

// A lost stream used to be permanent; AUDIO_RETRY_* is shared with the ALSA half.

static DWORD WINAPI audio_thread(LPVOID param) {
  (void)param;
  // The apartment is initialised ONCE and torn down once — it must not be
  // recycled per attempt.
  if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) return 0;
  int attempt = 0;
retry: ;
  IMMDeviceEnumerator *en = NULL;
  IMMDevice *dev = NULL;
  IAudioClient *ac = NULL;
  IAudioRenderClient *rc = NULL;
  WAVEFORMATEX *fmt = NULL;
  HANDLE ev = NULL;
  int started = 0;
  DWORD ran = 0;                  // when this stream reached the mix loop
  if (FAILED(CoCreateInstance(&CLSID_MMDevEnum_, NULL, CLSCTX_ALL,
                              &IID_IMMDevEnum_, (void **)&en))) goto done;
  if (FAILED(en->lpVtbl->GetDefaultAudioEndpoint(en, eRender, eConsole, &dev))) goto done;
  if (FAILED(dev->lpVtbl->Activate(dev, &IID_IAudioClient_, CLSCTX_ALL, NULL,
                                   (void **)&ac))) goto done;
  if (FAILED(ac->lpVtbl->GetMixFormat(ac, &fmt))) goto done;
  // The shared-mode mix format is 32-bit float on Win10/11; bail silently on
  // anything exotic rather than converting.
  int is_float = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                 (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                  ((const WAVEFORMATEXTENSIBLE *)fmt)->SubFormat.Data1 == 3);
  if (!is_float || fmt->nChannels < 1) goto done;
  g_dev_rate = (float)SFX_HZ / (float)fmt->nSamplesPerSec;
  if (FAILED(ac->lpVtbl->Initialize(ac, AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                    200000 /* 20 ms */, 0, fmt, NULL))) goto done;
  ev = CreateEventA(NULL, FALSE, FALSE, NULL);
  if (!ev || FAILED(ac->lpVtbl->SetEventHandle(ac, ev))) goto done;
  UINT32 bufsz = 0;
  if (FAILED(ac->lpVtbl->GetBufferSize(ac, &bufsz))) goto done;
  if (FAILED(ac->lpVtbl->GetService(ac, &IID_IAudioRenderClient_, (void **)&rc))) goto done;
  if (FAILED(ac->lpVtbl->Start(ac))) goto done;
  started = 1;
  ran = GetTickCount();
  while (atomic_load_explicit(&g_audio_run, memory_order_relaxed)) {
    // A TIMEOUT IS NOT BENIGN. An invalidated endpoint (device removed, audio service
    // restarted) stops signalling the event, and every call that would report
    // AUDCLNT_E_DEVICE_INVALIDATED sits BEHIND it — so a silently continued timeout is
    // a permanently dead stream with the rebuild machinery one line away. Probe the
    // client instead: on a healthy stream this succeeds and the loop is unchanged.
    DWORD w = WaitForSingleObject(ev, 200);
    UINT32 pad = 0;
    if (FAILED(ac->lpVtbl->GetCurrentPadding(ac, &pad))) break;
    if (w != WAIT_OBJECT_0) continue;
    UINT32 n = bufsz - pad;
    if (!n) continue;
    BYTE *out = NULL;
    if (FAILED(rc->lpVtbl->GetBuffer(rc, n, &out))) break;
    audio_mix((float *)out, n, fmt->nChannels);
    rc->lpVtbl->ReleaseBuffer(rc, n, 0);
  }
done:
  if (started) ac->lpVtbl->Stop(ac);
  if (rc) rc->lpVtbl->Release(rc);
  if (ac) ac->lpVtbl->Release(ac);
  if (dev) dev->lpVtbl->Release(dev);
  if (en) en->lpVtbl->Release(en);
  if (fmt) CoTaskMemFree(fmt);
  if (ev) CloseHandle(ev);
  // Rebuild only a stream that was running and broke — never a device that was never
  // there. And the count is a BURST budget, not a lifetime one: `attempt` never fell
  // back, so eight audio-service restarts spread over an evening ended sound for good.
  // A stream that ran half a minute is a working device.
  if (started && GetTickCount() - ran > AUDIO_RETRY_GOOD_MS) attempt = 0;  // wrap-safe
  if (atomic_load_explicit(&g_audio_run, memory_order_relaxed) && started &&
      ++attempt <= AUDIO_RETRY_MAX) {
    int ok = 1;
    for (int slept = 0; slept < AUDIO_RETRY_MS; slept += AUDIO_RETRY_SLICE_MS) {
      if (!atomic_load_explicit(&g_audio_run, memory_order_relaxed)) { ok = 0; break; }
      Sleep(AUDIO_RETRY_SLICE_MS);
    }
    if (ok && atomic_load_explicit(&g_audio_run, memory_order_relaxed)) {
      atomic_store(&g_trig_r, atomic_load(&g_trig_w));
      goto retry;
    }
  }
  CoUninitialize();
  return 0;
}

// Menu open: free the cursor (still hidden — the UI draws its own crosshair) and seed
// the UI cursor from the real one.
static void win_input_clear(void) {
  memset(g_keys, 0, sizeof g_keys);
  memset(g_pressed, 0, sizeof g_pressed);
  g_ui_mdown = 0;
  g_ui_drag = 0;
  g_mouse_dx = g_mouse_dy = 0;
}

static void platform_menu_changed(void) {
  upd_menu_recheck();   // the Windows backend has no harness path to gate
  if (g_menu_open) {
    ClipCursor(NULL);
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    g_ui_cx = (float)pt.x;
    g_ui_cy = (float)pt.y;
  } else if (g_active) {
    win_clip_cursor();
  }
  win_input_clear();
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_INPUT: {
    RAWINPUT raw;
    UINT size = sizeof raw;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
        raw.header.dwType == RIM_TYPEMOUSE && !(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
      g_mouse_dx += (float)raw.data.mouse.lLastX;
      g_mouse_dy += (float)raw.data.mouse.lLastY;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);  // MSDN: let the system clean up
  }
  case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
    WPARAM vk = wp;
    if (vk == VK_SHIFT)
      vk = MapVirtualKeyA(((UINT)lp >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
    else if (vk == VK_CONTROL)
      vk = (lp & (1 << 24)) ? VK_RCONTROL : VK_LCONTROL;
    int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
    // The PRESS EDGE only — lParam bit 30 is the key's PREVIOUS state, so it is set on
    // an auto-repeat.
    if (down && wp == VK_ESCAPE) { if (!(lp & (1 << 30))) ui_on_escape(); return 0; }
    if (down && g_ui_name_edit && msg == WM_KEYDOWN) {  // name field has focus
      if (vk == VK_BACK) ui_name_key("back");
      else if (vk == VK_RETURN) ui_name_key("enter");
      else {
        const char *tok = token_from_key((int)vk);
        if (tok && !tok[1]) ui_name_key(tok);
      }
      return 0;
    }
    if (down && g_ui_rebind >= 0 && msg == WM_KEYDOWN) {
      const char *tok = token_from_key((int)vk);
      if (tok) ui_assign_bind(tok);          // same cancel rule as the X11 half
      else if (vk == VK_RETURN || vk == VK_BACK) g_ui_rebind = -1;
      return 0;
    }
    // Arrow keys + Enter drive the menu like the pad — see the X11 backend's
    // nat_key for why this matters on the handheld. Repeats are wanted.
    if (down && g_menu_open) {
      // PgUp/PgDn switch tabs — same rationale as the X11 twin.
      int nav = vk == VK_UP    ? NAV_UP
              : vk == VK_DOWN  ? NAV_DOWN
              : vk == VK_LEFT  ? NAV_LEFT
              : vk == VK_RIGHT ? NAV_RIGHT
              : vk == VK_PRIOR ? NAV_TABL
              : vk == VK_NEXT  ? NAV_TABR
              : vk == VK_RETURN ? NAV_OK : -1;
      if (nav >= 0) { g_nav_kbd = 1; nav_push(nav); return 0; }
    }
    if (vk < 256) {
      g_keys[vk] = down;
      if (down && !g_menu_open) g_pressed[vk] = 1;
    }
    if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) return DefWindowProcA(hwnd, msg, wp, lp);
    return 0;
  }
  case WM_LBUTTONDOWN:
    if (g_menu_open) {
      if (g_ui_rebind >= 0) ui_assign_bind("mouse1");
      else g_ui_mdown = 1;
      return 0;
    }
    g_keys[KEY_MOUSE1] = 1; g_pressed[KEY_MOUSE1] = 1; return 0;
  case WM_LBUTTONUP:
    g_ui_mdown = 0; g_keys[KEY_MOUSE1] = 0; return 0;
  case WM_RBUTTONDOWN:
    if (g_menu_open) { if (g_ui_rebind >= 0) ui_assign_bind("mouse2"); return 0; }
    g_keys[KEY_MOUSE2] = 1; g_pressed[KEY_MOUSE2] = 1; return 0;
  case WM_RBUTTONUP:   g_keys[KEY_MOUSE2] = 0; return 0;
  case WM_MBUTTONDOWN:
    if (g_menu_open) { if (g_ui_rebind >= 0) ui_assign_bind("mouse3"); return 0; }
    g_keys[KEY_MOUSE3] = 1; g_pressed[KEY_MOUSE3] = 1; return 0;
  case WM_MBUTTONUP:   g_keys[KEY_MOUSE3] = 0; return 0;
  case WM_XBUTTONDOWN: {  // thumb buttons; MSDN wants TRUE returned when handled
    int k = HIWORD(wp) == XBUTTON1 ? KEY_MOUSE4 : KEY_MOUSE5;
    if (g_menu_open) {
      if (g_ui_rebind >= 0) ui_assign_bind(k == KEY_MOUSE4 ? "mouse4" : "mouse5");
      return TRUE;
    }
    g_keys[k] = 1; g_pressed[k] = 1; return TRUE;
  }
  case WM_XBUTTONUP:
    g_keys[HIWORD(wp) == XBUTTON1 ? KEY_MOUSE4 : KEY_MOUSE5] = 0; return TRUE;
  case WM_MOUSEWHEEL: {
    // The wheel cycles the weapon (see frame_input_keys).
    int notch = (short)HIWORD(wp) / WHEEL_DELTA;
    if (notch && g_active && !g_menu_open)
      g_pressed[notch > 0 ? KEY_MWUP : KEY_MWDN] = 1;
    return 0;
  }
  case WM_MOUSEMOVE:
    g_ui_cx = (float)(short)LOWORD(lp);
    g_ui_cy = (float)(short)HIWORD(lp);
    return 0;
  case WM_MOUSEACTIVATE:
    return MA_ACTIVATEANDEAT;  // the click that refocuses the game must not fire
  case WM_ACTIVATE:
    // HIWORD(wp) is the minimized flag: activation of a still-minimized
    // window must not clip the cursor to its off-screen placement rect.
    g_active = LOWORD(wp) != WA_INACTIVE && !HIWORD(wp);
    if (g_active) {
      if (!g_menu_open) win_clip_cursor();
      win_set_cursor_visible(0);
      // XInput is a level poll, so a button held across the alt-tab (or the
      // press that refocused the game) re-appears fully pressed on the first
      // active frame — eat everything still down, like MA_ACTIVATEANDEAT.
      win_pad_pump(0.0);  // refresh levels first; 0.0 skips only the rescan
      pad_eat_held();
    } else {
      // Fast, glitch-free alt-tab: release the cursor and clear all input so no key
      // sticks while the game is in the background.
      ClipCursor(NULL);
      win_set_cursor_visible(1);
      win_input_clear();
    }
    return 0;
  case WM_DISPLAYCHANGE: {
    // Desktop resolution changed while running: keep the borderless popup matched to
    // the desktop, or the "fullscreen" illusion breaks.
    int w = LOWORD(lp), h = HIWORD(lp) + 1;
    if (w > 0 && h > 0 && (w != R.width || h != R.height)) {
      SetWindowPos(hwnd, NULL, 0, 0, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
      R.width = w;
      R.height = h;
      glViewport(0, 0, w, h);
      if (g_active) win_clip_cursor();
    }
    return 0;
  }
  case WM_SETCURSOR:
    if (g_active && LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; }
    break;
  case WM_SYSCOMMAND:
    if ((wp & 0xFFF0) == SC_KEYMENU) return 0;  // no menu beep on Alt
    break;
  case WM_ERASEBKGND:
    return 1;
  case WM_CLOSE:
    if (g_menu_open) config_write(g_cfg_path, &g_cfg);  // Alt+F4 mid-edit: keep changes
    g_running = 0;
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcA(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Auto-update, Windows half. Fetch is WinHTTP (in the
// SDK, -lwinhttp); its default redirect policy already refuses https->http, which is
// the guarantee the curl line pins by flag.
// ---------------------------------------------------------------------------

static int upd_fetch(const char *url, char *dst, size_t cap, size_t *out_len) {
  *out_len = 0;
  wchar_t wurl[512];
  if (!MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 512)) {
    snprintf(g_upd_why, sizeof g_upd_why, "URL");
    return 0;
  }
  URL_COMPONENTS uc = {.dwStructSize = sizeof uc};
  wchar_t host[256], path[512];
  uc.lpszHostName = host; uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;  uc.dwUrlPathLength = 512;
  if (!WinHttpCrackUrl(wurl, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
    snprintf(g_upd_why, sizeof g_upd_why, "URL");   // https only, like --proto
    return 0;
  }
  int ok = 0;
  HINTERNET ses = WinHttpOpen(L"skill-issue", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  HINTERNET con = NULL, req = NULL;
  if (!ses) goto done;
  WinHttpSetTimeouts(ses, 5000, 5000, 30000, 30000);
  con = WinHttpConnect(ses, host, uc.nPort, 0);
  if (!con) goto done;
  req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!req) goto done;
  // Ask fresh: same reason as the curl side (one fixed URL, and a same-day re-cut
  // carries the same date inside).
  if (!WinHttpSendRequest(req, L"Cache-Control: no-cache\r\nPragma: no-cache",
                          (DWORD)-1L, NULL, 0, 0, 0) ||
      !WinHttpReceiveResponse(req, NULL))
    goto done;
  DWORD status = 0, sl = sizeof status;
  if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &sl,
                           WINHTTP_NO_HEADER_INDEX) || status != 200)
    goto done;
  size_t got = 0;
  for (;;) {
    DWORD n = 0;
    if (!WinHttpReadData(req, dst + got, (DWORD)(cap - got), &n)) goto done;
    if (n == 0) break;
    got += n;
    if (got == cap) {          // larger than our cap: reject, like curl's cap
      char sink[256]; DWORD s2 = 0;
      // A READ ERROR IS NOT END OF BODY — the same rule the curl half spells out.
      // Short-circuited together, a failed drain read reported a clean EOF and the
      // truncated body was accepted as the whole file.
      if (!WinHttpReadData(req, sink, sizeof sink, &s2)) goto done;
      if (s2 > 0) { snprintf(g_upd_why, sizeof g_upd_why, "TOO LARGE"); goto done; }
      break;
    }
  }
  *out_len = got;
  ok = 1;
done:
  if (!ok && !g_upd_why[0]) snprintf(g_upd_why, sizeof g_upd_why, "NET");
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  if (ses) WinHttpCloseHandle(ses);
  return ok;
}

static void upd_wpath(wchar_t *out, size_t n, const wchar_t *exe, const wchar_t *sfx) {
  _snwprintf(out, n, L"%s%s", exe, sfx);
  out[n - 1] = 0;
}

static void upd_probe_fs(void) {
  wchar_t exe[512], p[600];
  DWORD len = GetModuleFileNameW(NULL, exe, 512);
  if (!len || len >= 512) { g_upd_writable = 0; return; }
  _snwprintf(p, 600, L"%s.new.probe.%lu", exe, (unsigned long)GetCurrentProcessId());
  p[599] = 0;
  HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    DeleteFileW(p);
    g_upd_writable = 1;
  } else g_upd_writable = 0;
}

static DWORD WINAPI upd_thread(LPVOID arg) {
  (void)arg;
  upd_thread_body();
  return 0;
}

static void upd_check_start(void) {
  if (!upd_check_gate()) return;
  HANDLE th = CreateThread(NULL, 0, upd_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else upd_spawn_failed();
}

// The telemetry resolver's Windows half — same shape as upd_check_start above.
static DWORD WINAPI tele_thread(LPVOID arg) {
  (void)arg;
  tele_resolve_body();
  return 0;
}

static void tele_resolve_start(void) {
  HANDLE th = CreateThread(NULL, 0, tele_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else atomic_store(&g_tele_ip, 0xFFFFFFFFu);   // no thread, no telemetry
}

// The one "metadata done, become the new binary" step.
static int win_relaunch(const wchar_t *exe) {
  config_write(g_cfg_path, &g_cfg);
  STARTUPINFOW si = {.cb = sizeof si};
  PROCESS_INFORMATION pi;
  if (CreateProcessW(exe, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    atomic_store(&g_audio_run, 0);
    if (g_audio_h) { WaitForSingleObject(g_audio_h, 800); CloseHandle(g_audio_h); }
    exit(0);
  }
  snprintf(g_upd_why, sizeof g_upd_why, "EXEC");
  return 0;   // swapped file is in place; the next manual start runs it
}

// Download, verify, swap, re-exec. THERE IS NO ROLLBACK: the <exe>.old copy below is
// kept because it costs a link() and is the only recovery a bad build has, but nothing
// in the program reads it and there is no UI row for it — it is a file on disk for a
// human to rename.
static int upd_apply(void) {
  size_t len = 0;
  char *buf = upd_download_verified(&len);
  if (!buf) return 0;
  wchar_t exe[512], newp[600], old[600];
  DWORD elen = GetModuleFileNameW(NULL, exe, 512);
  if (!elen || elen >= 512) { free(buf); snprintf(g_upd_why, sizeof g_upd_why, "PATH"); return 0; }
  _snwprintf(newp, 600, L"%s.new.%lu", exe, (unsigned long)GetCurrentProcessId());
  newp[599] = 0;
  upd_wpath(old, 600, exe, L".old");
  HANDLE h = CreateFileW(newp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    free(buf);
    snprintf(g_upd_why, sizeof g_upd_why, "WRITE");
    return 0;
  }
  DWORD off = 0;
  int bad = 0;
  while (off < (DWORD)len) {
    DWORD w = 0;
    if (!WriteFile(h, buf + off, (DWORD)len - off, &w, NULL) || w == 0) { bad = 1; break; }
    off += w;
  }
  free(buf);
  if (!bad) bad = !FlushFileBuffers(h);
  CloseHandle(h);
  if (bad) {
    DeleteFileW(newp);
    snprintf(g_upd_why, sizeof g_upd_why, "WRITE");
    return 0;
  }
  // Metadata-only from here: rename -> rename, restore on failure.
  if (!MoveFileExW(exe, old, MOVEFILE_REPLACE_EXISTING)) {
    DeleteFileW(newp);
    snprintf(g_upd_why, sizeof g_upd_why, "RENAME");
    return 0;
  }
  if (!MoveFileExW(newp, exe, MOVEFILE_REPLACE_EXISTING)) {
    MoveFileExW(old, exe, MOVEFILE_REPLACE_EXISTING);   // restore while we still can
    DeleteFileW(newp);
    snprintf(g_upd_why, sizeof g_upd_why, "RENAME");
    return 0;
  }
  return win_relaunch(exe);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show) {
  (void)prev; (void)cmdline; (void)show;

  // Per-monitor DPI awareness so the popup covers real pixels (dynamic call:
  // present on Win10 1703+, and we must not fail to start without it).
  HMODULE user32 = GetModuleHandleA("user32.dll");
  typedef BOOL (WINAPI *pfn_dpi)(HANDLE);
  pfn_dpi set_dpi = (pfn_dpi)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
  if (set_dpi) set_dpi((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);

  char dir[480];
  exe_dir(dir, sizeof dir);
  snprintf(g_cfg_path, sizeof g_cfg_path, "%sconfig.cfg", dir);
  config_load_or_create(g_cfg_path, &g_cfg);

  WNDCLASSA wc = {
    .style = CS_OWNDC,
    .lpfnWndProc = wndproc,
    .hInstance = inst,
    .hCursor = LoadCursorA(NULL, IDC_ARROW),
    .hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
    .lpszClassName = "skill_issue",
  };
  RegisterClassA(&wc);

  // Borderless fullscreen: a plain popup at desktop resolution.
  int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN) + 1;
  g_hwnd = CreateWindowExA(0, wc.lpszClassName, "skill-issue", WS_POPUP,
                           0, 0, sw, sh, NULL, NULL, inst, NULL);
  // The Linux twin fatals on eight bring-up steps and this one on none. Only this
  // guard changes an OUTCOME rather than a message: without a window GetDC(NULL)
  // hands back the SCREEN dc, so bring-up can succeed, WM_ACTIVATE never arrives,
  // g_active stays 0 and the loop sleeps forever with no WM_QUIT possible — an
  // invisible process the player has to kill. Every other failure below already
  // terminates through gl_load_functions' own fatal.
  if (!g_hwnd) fatal("Win32: cannot create window");
  HDC dc = GetDC(g_hwnd);

  PIXELFORMATDESCRIPTOR pfd = {
    .nSize = sizeof pfd,
    .nVersion = 1,
    .dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
    .iPixelType = PFD_TYPE_RGBA,
    .cColorBits = 32,
    .cDepthBits = 24,
  };
  SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
  HGLRC dummy = wglCreateContext(dc);
  wglMakeCurrent(dc, dummy);
  pfn_wglCreateContextAttribsARB create_ctx =
      (pfn_wglCreateContextAttribsARB)(void *)wglGetProcAddress("wglCreateContextAttribsARB");
  HGLRC ctx = NULL;
  if (create_ctx) {
    int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                     WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    ctx = create_ctx(dc, NULL, attribs);
  }
  if (ctx) {
    wglMakeCurrent(dc, ctx);
    wglDeleteContext(dummy);
  } else {
    ctx = dummy;  // ancient driver: try the legacy context and hope for 3.3
  }
  gl_load_functions((gl_loader_fn)(void *)wglGetProcAddress);
  g_swap_interval = (pfn_wglSwapIntervalEXT)(void *)wglGetProcAddress("wglSwapIntervalEXT");
  if (g_swap_interval) g_swap_interval(g_cfg.vsync);
  timeBeginPeriod(1);  // 1 ms Sleep granularity for the frame limiter
  win_pad_load();

  // Singleplayer contract: every launch gets a fresh procedural arena.
  LARGE_INTEGER seed_qpc;
  QueryPerformanceCounter(&seed_qpc);
  player_t *player_s = &g_players[0];   // human 0 IS the local player
  app_start(player_s, sw, sh,
            (uint32_t)seed_qpc.QuadPart ^
            ((uint32_t)(seed_qpc.QuadPart >> 32) * 2654435761u) ^
            GetCurrentProcessId());
  g_audio_h = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
  upd_check_start();   // async; never blocks the start
  ui_open_menu();      // boot into the menu over the live arena, as on Linux
  home_enter(local_player());   // ...as the BOOT SCREEN, as on Linux
  boot_connect_kick();   // --connect, same as Linux

  RAWINPUTDEVICE rid = {.usUsagePage = 1, .usUsage = 2, .hwndTarget = g_hwnd};
  RegisterRawInputDevices(&rid, 1, sizeof rid);

  // First frame before the window appears: no black/white startup flash.
  render_frame(player_s, 1.0f, 0, NULL);
  SwapBuffers(dc);
  ShowWindow(g_hwnd, SW_SHOW);
  SetForegroundWindow(g_hwnd);
  SetFocus(g_hwnd);

  app_t app = {0};
  LARGE_INTEGER freq, now, last;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&last);

  while (g_running) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) g_running = 0;
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if (!g_active) {  // alt-tabbed away: no sim, no rendering, no GPU burn
      Sleep(5);
      QueryPerformanceCounter(&last);
      app.acc = 0;
      continue;
    }

    QueryPerformanceCounter(&now);
    double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
    last = now;

    win_pad_pump((double)now.QuadPart / (double)freq.QuadPart);
    if (app_pump(&app, player_s, dt, g_mouse_dx, g_mouse_dy)) g_running = 0;
    g_mouse_dx = g_mouse_dy = 0;

    SwapBuffers(dc);
    app_sync(&app);

    // Frame limiter: sleep the coarse part, spin the last 2 ms for accuracy.
    if (g_cfg.fps_cap > 0) {
      double target = 1.0 / g_cfg.fps_cap;
      LARGE_INTEGER t;
      for (;;) {
        QueryPerformanceCounter(&t);
        double el = (double)(t.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        if (el >= target) break;
        if (target - el > 0.002) Sleep(1);
        else YieldProcessor();
      }
    }
  }

  // GOODBYE FIRST, as in the Linux teardown: WM_CLOSE/Alt+F4 hands a joined slot back
  // to the server instead of parking it for the idle timeout. Gated like leave_match's
  // call — at boot g_cl is zeroed and its fd of 0 would otherwise be "closed".
  if (g_cl.state != NC_OFF) net_cl_disconnect();
  atomic_store(&g_audio_run, 0);  // the audio thread winds down while the window closes
  // The context stops owning a drawable BEFORE the drawable goes: destroying a window
  // under a current context is undefined, and nothing released the context at all.
  wglMakeCurrent(NULL, NULL);
  wglDeleteContext(ctx);
  DestroyWindow(g_hwnd);
  // JOIN, and only after DestroyWindow.
  if (g_audio_h) {
    WaitForSingleObject(g_audio_h, 800);
    CloseHandle(g_audio_h);
  }
  win_dinput_shutdown();
  return 0;
}

