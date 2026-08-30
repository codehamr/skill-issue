// game.c — "skill-issue": a minimalist low-poly tactical FPS.
//
// ONE C23 translation unit. Two platform backends chosen at compile time
// (#ifdef _WIN32) and three mutually exclusive runtime modes chosen at main():
//
//   play      no arguments. Linux: X11 borderless fullscreen (EWMH hint), EGL
//             core 3.3, raw mouse via XInput2, ALSA sound — libXi and libasound
//             are dlopen'd, so a missing one degrades instead of failing to
//             start. Windows: borderless fullscreen popup + WGL core 3.3, no
//             display-mode change (no HDR mode-switch black screens, alt-tab is
//             a plain DWM switch), first frame rendered before the window shows.
//   harness   --do / --script. EGL surfaceless on llvmpipe rendering into an
//             FBO, driven by the script language usage() documents. Linux only.
//   server    --server. Sim + UDP only: no GL, no audio, no window. One process
//             serves up to SRV_LOBBIES arenas on ONE port.
//
// The modes fork at main(), which is what keeps every proof deterministic.
// The frame loop belongs to NEITHER backend: app_start/app_frame/app_sync sit
// just above the platform split and own everything between "here is dt and the
// input state" and "swap the buffers".
//
// Shared core: config (config.cfg beside the executable), seeded procedural map
// (AABB solids), fixed-timestep 120 Hz sim with render interpolation, GL 3.3
// renderer, and the "GRATICULE" UI — a procedural SDF typeface (Hershey-simplex
// skeletons baked to a distance atlas at GL init) plus panel primitives, all in
// one HUD batch.
//
// C23, both compilers: MinGW-w64 stddef.h lacks unreachable(); #embed needs
// GCC 15; constexpr/nullptr/typed enums work on both.
//
// The MAP OF THE FILE is the include list below — one #include per module, in
// definition order; each module's own header names its contracts.
//
// The machine-checked proof commands live in the harness; ./build/game --help
// is their command reference, and each one's own comment at its implementation
// names the defect it exists to catch.

#define _DEFAULT_SOURCE  // readlink/strdup despite -std=c23 strict mode

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>   // SPSC sound-trigger ring, game thread -> audio thread
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>       // update check timestamps ("CHECKED n MIN AGO")

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define DIRECTINPUT_VERSION 0x0800
  #include <dinput.h>
  #include <mmsystem.h>   // timeBeginPeriod for the frame limiter
  #include <mmreg.h>      // WAVEFORMATEXTENSIBLE
  #include <objbase.h>    // CoInitializeEx & friends (LEAN_AND_MEAN drops them)
  #include <mmdeviceapi.h>
  #include <audioclient.h>
  #include <winhttp.h>    // auto-update check; the ~/.curlrc class of attack
                          // does not exist here
  #include <GL/gl.h>
#else
  #include <sys/stat.h>
  #include <unistd.h>
  #include <GL/gl.h>
  #include <EGL/egl.h>
#endif

#ifndef APIENTRY
  #define APIENTRY
#endif

// Fatal errors must be visible even in the -mwindows build (no console).
static void fatal(const char *msg) {
#ifdef _WIN32
  MessageBoxA(NULL, msg, "skill-issue", MB_ICONERROR);
#endif
  fprintf(stderr, "%s\n", msg);
  exit(1);
}


// ---------------------------------------------------------------------------
// THE MAP OF THE FILE IS THIS INCLUDE LIST. One translation unit, textual
// inclusion, and the order is the old definition order — which C forces,
// since a helper is defined above its first use. Moving a definition between
// modules is therefore a SEMANTIC change, not a tidy-up. Every .inc is
// included exactly once; tools/split-check.sh proves it, and the fragment
// list below doubles as the subsystem index (docs/refactor.md has the map).
// ---------------------------------------------------------------------------
#include "core/base.inc"
#include "core/config.inc"
#include "core/pad.inc"
#include "core/update.inc"
#include "world/map.inc"
#include "core/events.inc"
#include "core/hitscan.inc"
#include "audio/synth.inc"
#include "audio/route.inc"
#include "sim/player.inc"
#include "net/wire.inc"
#include "sim/locomotion.inc"
#include "sim/skeleton.inc"
#include "sim/bots_mind.inc"
#include "sim/combat.inc"
#include "sim/bots_tick.inc"
#include "sim/weapon.inc"
#include "net/session.inc"
#include "render/shaders.inc"
#include "render/render.inc"
#include "world/scatter.inc"
#include "ui/batch.inc"
#include "ui/settings.inc"
#include "ui/menu.inc"
#include "render/vfx.inc"
#include "world/decor.inc"
#include "render/figure.inc"
#include "render/gun_build.inc"
#include "render/figure_pose.inc"
#include "render/figure_draw.inc"
#include "render/viewmodel.inc"
#include "ui/home.inc"
#include "render/frame_render.inc"
#include "core/frame.inc"
// ===========================================================================
// Linux backend — native window (X11 + EGL) OR the headless CLI harness.
// ===========================================================================
#ifndef _WIN32
#include "platform/harness_core.inc"
#include "harness/analysis.inc"
#include "harness/proofs_bot.inc"
#include "harness/proofs_fig.inc"
#include "harness/proofs_net.inc"
#include "harness/script.inc"
#include "platform/linux_native.inc"
#include "platform/linux_update.inc"
#include "platform/linux_main.inc"
// ===========================================================================
// Windows backend — borderless fullscreen popup + WGL 3.3 core, raw mouse
// input, config-driven binds, vsync via wglSwapIntervalEXT.
// ===========================================================================
#else  // _WIN32
#include "platform/windows.inc"
#endif  // _WIN32
