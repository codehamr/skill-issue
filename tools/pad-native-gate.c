// Exercise the actual evdev pump with pipe-delivered kernel packets and ioctl
// snapshots. The fixture never opens, grabs or injects into a real input device.
#define _GNU_SOURCE
#include <sys/ioctl.h>
#include <stdarg.h>
static int pad_gate_ioctl(int fd, unsigned long request, ...);
#undef _DEFAULT_SOURCE
#define ioctl pad_gate_ioctl
#define main pad_gate_game_main
#include "../code/game.c"
#undef main
#undef ioctl

typedef struct {
  int fd, writer;
  unsigned char keys[(PKEY_MAX + 8) / 8];
  int axes[PABS_RZ + 1];
} pad_gate_node_t;
static pad_gate_node_t fixtures[2];
static int grabs, checks, failures;

static int pad_gate_ioctl(int fd, unsigned long request, ...) {
  if (request == _IOW('E', 0x90, int)) { grabs++; errno = EBUSY; return -1; }
  va_list args; va_start(args, request);
  void *out = va_arg(args, void *); va_end(args);
  for (int i = 0; i < 2; i++) if (fd == fixtures[i].fd) {
    if (request == EVIOCGKEY_(sizeof fixtures[i].keys)) {
      memcpy(out, fixtures[i].keys, sizeof fixtures[i].keys); return 0;
    }
    for (int axis = 0; axis <= PABS_RZ; axis++) if (request == EVIOCGABS_(axis)) {
      ev_absinfo_t value = {.value = fixtures[i].axes[axis],
        .minimum = g_pad_node[i].absr[axis].min, .maximum = g_pad_node[i].absr[axis].max};
      memcpy(out, &value, sizeof value); return 0;
    }
    errno = EINVAL; return -1;
  }
  errno = ENOTTY; return -1;
}
static void expect(int good, const char *name) {
  checks++;
  if (!good) { failures++; printf("pad-native %s FAIL\n", name); }
}
static void packet(int node, int type, int code, int value) {
  ev_event_t e = {.type = (unsigned short)type, .code = (unsigned short)code, .value = value};
  if (write(fixtures[node].writer, &e, sizeof e) != sizeof e) abort();
}
static void key(int node, int code, int value, int deliver) {
  unsigned char *b = &fixtures[node].keys[code >> 3];
  if (value) *b |= 1u << (code & 7); else *b &= ~(1u << (code & 7));
  if (deliver) packet(node, PEV_KEY, code, value);
}
static void axis(int node, int code, int value) {
  fixtures[node].axes[code] = value;
  packet(node, PEV_ABS, code, value);
}
int main(void) {
  g_pad_watch = -1; g_pad_rescan = 1e9;
  for (int i = 0; i < 2; i++) {
    int pair[2]; if (pipe(pair)) abort();
    fcntl(pair[0], F_SETFL, O_NONBLOCK);
    fixtures[i].fd = pair[0]; fixtures[i].writer = pair[1];
    g_pad_node[i] = (pad_node_t){.fd = pair[0], .idx = i};
    snprintf(g_pad_node[i].name, sizeof g_pad_node[i].name, "fixture%d", i);
    for (int a = 0; a <= PABS_RZ; a++) {
      int trigger = a == PABS_Z || a == PABS_RZ;
      g_pad_node[i].absr[a].min = trigger ? 0 : -32768;
      g_pad_node[i].absr[a].max = trigger ? 255 : 32767;
    }
  }
  axis(0, PABS_X, 9000); axis(0, PABS_RX, 24000); key(0, PBTN_A, 1, 1);
  nat_pad_pump(1.0);
  expect(g_pad_act == 0 && g_pad.lx > .27f && g_pad.rx > .73f && g_pad.btn[PB_A], "initial-complete-state");
  axis(1, PABS_X, -24000); axis(1, PABS_RY, 21000);
  nat_pad_pump(1.2);
  expect(g_pad_act == 0 && g_pad_node[1].state.lx < -.73f, "inactive-axes-retained");
  key(1, PBTN_B, 1, 1); nat_pad_pump(2.2);
  expect(g_pad_act == 1 && g_pad.lx < -.73f && g_pad.ry < -.64f && g_pad.btn[PB_B], "handover-unchanged-sticks");
  axis(0, PABS_X, 1000); axis(0, PABS_RX, 1000); key(0, PBTN_A, 0, 1);
  nat_pad_pump(3.3);
  expect(g_pad_act == 1, "drift-does-not-steal");
  key(1, PBTN_B, 0, 0); // Kernel level changes behind an exclusive owner.
  nat_pad_pump(3.6);
  expect(g_pad_node[1].seen && g_pad.btn[PB_B], "ownership-race-confirmation");
  nat_pad_pump(3.9);
  expect(!g_pad_node[1].seen && !g_pad_node[1].state.btn[PB_B] && g_pad_act != 1, "silent-owner-clears-stale-levels");
  key(1, PBTN_A, 1, 1); nat_pad_pump(5.0);
  expect(g_pad_act == 1 && g_pad.btn[PB_A] && !g_pad.btn[PB_B], "fresh-stream-reclaims");
  packet(1, PEV_SYN, PSYN_DROPPED, 0);
  packet(1, PEV_KEY, PBTN_B, 1); // An invalid buffered event after SYN_DROPPED.
  nat_pad_pump(5.1);
  expect(!g_pad.btn[PB_B] && g_pad_node[1].dropped, "overflow-discards-until-report");
  packet(1, PEV_SYN, PSYN_REPORT, 0); nat_pad_pump(5.2);
  expect(!g_pad.btn[PB_B] && g_pad.btn[PB_A] && !g_pad_node[1].dropped, "overflow-snapshot-recovery");
  close(fixtures[1].writer); nat_pad_pump(5.3);
  expect(g_pad_node[1].fd < 0 && !g_pad.btn[PB_A], "unplug-releases");
  expect(grabs == 0, "no-exclusive-probes");
  close(fixtures[0].writer); nat_pad_drop(0);
  printf("pad-native checks=%d grabs=%d failures=%d %s\n", checks, grabs, failures, failures ? "FAIL" : "ok");
  return failures != 0;
}
