# Tactical FPS prototype — one C23 file, two targets from the same source:
#   build/game.exe : Windows 11 deliverable (native window via WGL),
#                    cross-compiled with MinGW-w64, fully static.
#   build/game     : Linux deliverable AND test harness in one binary. No
#                    arguments plays it in an X11 window (EGL core 3.3, raw
#                    mouse via XInput2, sound via ALSA); --do/--script runs the
#                    headless script harness (EGL surfaceless + Mesa llvmpipe)
#                    for fast in-container iteration and screenshot checks.
#                    `make deploy` ships this to the handheld.
#
# The Linux link line is deliberately short: libX11/libEGL/libGL are the only
# hard dependencies, and SteamOS ships all three. XInput2 (libXi) and ALSA
# (libasound) are dlopen'd at runtime instead of linked, so a machine missing
# either still starts — it loses raw-mouse precision or sound, not the game.

# Heavy rendering runs on the GPU (shaders); the CPU only does sim math.
# WIN_ARCH is Windows-only and must not leak into the Linux build: build/game
# is both the deployed handheld build and the in-container harness, and the
# devcontainer IS aarch64 today — the x86_64 deliverables come from the
# build/game-x86_64 cross-target below.
#
# -flto=auto is a BUILD-TIME flag here, not an optimization one: the source is
# already a single translation unit, so there is no cross-TU inlining left to
# win. What it buys is that gcc partitions that one TU and optimizes the parts
# in parallel — measured 4.78 s -> 1.78 s (Linux) and 5.68 s -> 2.41 s (MinGW),
# i.e. `make all` from ~11 s to ~4 s. Since compile and link happen in one
# command below, the flag reaches both, which LTO requires.
# It is safe to the last bit and that was checked rather than assumed: the proof
# battery (figcheck / vmcheck / mapcheck / kine / lean / budget / tacstat /
# match / trace) is byte-identical and a rendered PNG is md5-identical against
# the non-LTO build. That check is the acceptance gate if these flags ever move
# — CLAUDE.md's depth-tie note explains why comparing across differing
# optimization flags otherwise reports pure compiler noise as a finding.
# Cost: +3.9% binary size (958 KB -> 996 KB on the .exe).
OPT      := -O3 -ffast-math -funroll-loops -flto=auto
# -march=x86-64-v2 (SSE4.2/POPCNT), NOT -mavx2 -mfma: the .exe is a public
# download now, and AVX2 code is a #UD *before main()* on every Windows 11
# machine without it — an instant, message-less crash. x86-64-v2 is exactly
# Windows 11's own CPU floor, and the handheld loses nothing measurable
# (design §13.4; the CPU cost of this game is the scalar figure build).
WIN_ARCH := -march=x86-64-v2

# The version the auto-updater compares against (design §13.3). Without an
# explicit BUILD_VERSION the build is a developer build and NEVER updates —
# that is the default and not the special case, so a local build can never
# clobber itself with a release. BUILD_COMMIT is the only re-cut confirmation
# a player has (two different binaries can ship under the same date tag).
BUILD_VERSION ?= dev
BUILD_COMMIT  ?= unknown
VERFLAG       := -DBUILD_VERSION='"$(BUILD_VERSION)"' -DBUILD_COMMIT='"$(BUILD_COMMIT)"'

WIN_CC     := x86_64-w64-mingw32-gcc
WIN_RES    := x86_64-w64-mingw32-windres
WIN_CFLAGS := -std=c23 $(OPT) $(WIN_ARCH) -Wall -Wextra -mwindows
WIN_LIBS   := -static -lgdi32 -luser32 -lopengl32 -lwinmm -lole32 -lwinhttp -lws2_32 -lm

# -fno-tree-vectorize is a DEPENDENCY decision, not an optimization one
# (design §13.4, measured 2026-08-08): under -ffast-math the auto-vectorizer
# pulls libmvec.so.1 into NEEDED for zero measured win. With the flag, one
# dependency fewer and the floor is the bare -std=c23 __isoc23_* GLIBC_2.38
# (which no flag can lower); trace/match/kine are byte-identical against the
# vectorized build on aarch64 AND x86_64, and the 20-bot frame cost moved
# within noise (109.4 -> 112.3 ms on the llvmpipe proxy, ~3%). On aarch64 —
# this devcontainer — libmvec would additionally raise the floor to 2.40,
# which is why the flag predates the arm64 release's removal and outlives it.
# Linux only — Windows has no libmvec and keeps WIN_ARCH above.
LIN_CC     := gcc
LIN_CFLAGS := -std=c23 $(OPT) -fno-tree-vectorize -Wall -Wextra
LIN_LIBS   := -lEGL -lGL -lX11 -lm

# The SHIPPABLE Linux binary: this devcontainer is aarch64, the deploy target
# (and the release amd64 asset) are x86_64, so the cross-target is what
# `make deploy` ships. Toolchain: dpkg --add-architecture amd64 + apt-get
# install gcc-x86-64-linux-gnu libx11-dev:amd64 libegl-dev:amd64 libgl-dev:amd64
# qemu-user-static (qemu runs the result for cross-arch sim comparisons).
X86_CC := x86_64-linux-gnu-gcc

all: build/game.exe build/game

# Application icon (Windows only): windres compiles game.rc (which embeds
# icon.ico) into an object file linked alongside the single C translation unit.
build/game.res.o: code/game.rc code/icon.ico
	mkdir -p build
	$(WIN_RES) $< -o $@

build/game.exe: code/game.c build/game.res.o
	mkdir -p build
	$(WIN_CC) $(WIN_CFLAGS) $(VERFLAG) code/game.c build/game.res.o -o $@ $(WIN_LIBS)

build/game: code/game.c
	mkdir -p build
	$(LIN_CC) $(LIN_CFLAGS) $(VERFLAG) $< -o $@ $(LIN_LIBS)

build/game-x86_64: code/game.c
	mkdir -p build
	$(X86_CC) $(LIN_CFLAGS) $(VERFLAG) $< -o $@ $(LIN_LIBS)

# ASan/UBSan build for hunting memory bugs in the sim code (Linux only).
# float-cast-overflow and float-divide-by-zero are NOT in -fsanitize=undefined
# and are exactly the classes -ffast-math makes most likely (a NaN reaching an
# (int) cast, a zero-length normalize), so they are named explicitly.
build/game-asan: code/game.c
	mkdir -p build
	$(LIN_CC) -std=c23 -O1 -g -fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero -Wall -Wextra $< -o $@ $(LIN_LIBS)

# Copy the SHIPPABLE Linux build to the handheld's home directory and make it
# runnable there. `handheld` is an ssh config Host entry, which comes from the
# Windows host's ~/.ssh mounted by .devcontainer/devcontainer.json — so the key
# and the address live in one place instead of being repeated here.
#
# The guard reads the ELF machine field (0x3e00 LE = x86_64) before shipping:
# shipping a container-native build from this aarch64 devcontainer once put an
# AArch64 ELF on the handheld, which "crashes instantly" with an exec format
# error before a single line of ours runs. The guard STAYS hard-coded to 3e00
# — parametrizing it would reactivate exactly the failure it exists to stop;
# deploy targets ONE device, and that device has one answer. It now just gets
# handed a binary that passes it (the cross-target) instead of failing on the
# native aarch64 build.
DEPLOY_HOST ?= handheld
DEPLOY_BIN  ?= build/game-x86_64

deploy: $(DEPLOY_BIN)
	@test "$$(od -An -tx1 -j18 -N2 $(DEPLOY_BIN) | tr -d ' ')" = "3e00" || \
	  { echo "$(DEPLOY_BIN) is not x86_64 — refusing to deploy"; exit 1; }
	scp $(DEPLOY_BIN) $(DEPLOY_HOST):game
	ssh $(DEPLOY_HOST) chmod +x game

# build/ carries a Dropbox-ignore NTFS attribute on the folder itself —
# delete its contents, never the folder, or the attribute is lost.
clean:
	rm -f build/game build/game.exe build/game-asan build/game-x86_64 build/game.res.o

init:
	clear
	git reset $$(git commit-tree -S HEAD^{tree} -m "init")
	git push --force origin main
	@echo "Git history reset to single 'init' commit"

.PHONY: all clean deploy init
