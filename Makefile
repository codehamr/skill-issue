# Tactical FPS prototype — one C23 file, three artifacts from the same source:
#   build/game.exe : Windows 11 deliverable (native window via WGL),
#                    cross-compiled with MinGW-w64, fully static.
#   build/game-x86_64 : the SHIPPABLE Linux binary — guaranteed x86_64
#                    whatever the container is (see X86_CC below); what
#                    `make deploy` and CI publish (the server bootstraps it
#                    from the release feed itself — see the server block).
#   build/game     : Linux deliverable AND test harness in one binary. No
#                    arguments plays it in an X11 window (EGL core 3.3, raw
#                    mouse via XInput2, sound via ALSA); --do/--script runs the
#                    headless script harness (EGL surfaceless + Mesa llvmpipe)
#                    for fast in-container iteration and screenshot checks.
#                    Never shipped: DEPLOY_BIN is the x86_64 target above.
#
# The Linux link line is deliberately short: libX11/libEGL/libGL are the only
# hard dependencies, and SteamOS ships all three. XInput2 (libXi) and ALSA
# (libasound) are dlopen'd at runtime instead of linked, so a machine missing
# either still starts — it loses raw-mouse precision or sound, not the game.

# Heavy rendering runs on the GPU (shaders); the CPU only does sim math.
# WIN_ARCH is Windows-only and must not leak into the Linux build: build/game
# is both the in-container harness and (on an x86_64 host) the same compile the
# shippable binary gets, while build/game-x86_64 below is the one that is
# guaranteed x86_64 whatever the container is.
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
# — an A/B across differing optimization flags otherwise reports pure compiler
# noise as a finding: the sim is flag-stable, but the chaotic anim solver
# amplifies 1 ULP into centimetres, so a POSE may only be compared between two
# builds with IDENTICAL flags.
# Cost: +3.9% binary size (958 KB -> 996 KB on the .exe).
OPT      := -O3 -ffast-math -funroll-loops -flto=auto
# -march=x86-64-v2 (SSE4.2/POPCNT), NOT -mavx2 -mfma: the .exe is a public
# download now, and AVX2 code is a #UD *before main()* on every Windows 11
# machine without it — an instant, message-less crash. x86-64-v2 is exactly
# Windows 11's own CPU floor, and the handheld loses nothing measurable
# (the CPU cost of this game is the scalar figure build).
WIN_ARCH := -march=x86-64-v2

# The version the auto-updater compares against. Without an
# explicit BUILD_VERSION the build is a developer build and NEVER updates —
# that is the default and not the special case, so a local build can never
# clobber itself with a release. BUILD_COMMIT is the only re-cut confirmation
# a player has (two different binaries can ship under the same date tag).
BUILD_VERSION ?= dev
BUILD_COMMIT  ?= unknown
VERFLAG       := -DBUILD_VERSION='"$(BUILD_VERSION)"' -DBUILD_COMMIT='"$(BUILD_COMMIT)"'

WIN_CC     := x86_64-w64-mingw32-gcc
WIN_RES    := x86_64-w64-mingw32-windres
WIN_CFLAGS := -std=c23 $(OPT) $(WIN_ARCH) -Wall -Wextra -Wshadow -mwindows
WIN_LIBS   := -static -lgdi32 -luser32 -lopengl32 -lwinmm -lole32 -lwinhttp -lws2_32 -lm

# -fno-tree-vectorize is a DEPENDENCY decision, not an optimization one
# (measured 2026-08-08): under -ffast-math the auto-vectorizer
# pulls libmvec.so.1 into NEEDED for zero measured win. With the flag, one
# dependency fewer and the floor is the bare -std=c23 __isoc23_* GLIBC_2.38
# (which no flag can lower); trace/match/kine are byte-identical against the
# vectorized build on aarch64 AND x86_64, and the 20-bot frame cost moved
# within noise (109.4 -> 112.3 ms on the llvmpipe proxy, ~3%). On aarch64 —
# the older devcontainer — libmvec would additionally raise the floor to 2.40,
# which is why the flag predates the arm64 release's removal and outlives it.
# Linux only — Windows has no libmvec and keeps WIN_ARCH above.
LIN_CC     := gcc
LIN_CFLAGS := -std=c23 $(OPT) -fno-tree-vectorize -Wall -Wextra -Wshadow
LIN_LIBS   := -lEGL -lGL -lX11 -lm

# The shippable Linux binary is x86_64. On an x86_64 host the native gcc IS
# the cross compiler; on the old aarch64 devcontainer it is the cross
# toolchain. Decide once, here. Setup on an aarch64 host (keep this line — it
# is the only record of it): dpkg --add-architecture amd64 && apt-get install
# gcc-x86-64-linux-gnu libx11-dev:amd64 libegl-dev:amd64 libgl-dev:amd64
# qemu-user-static  (qemu runs the result for cross-arch sim comparisons).
ifeq ($(shell uname -m),x86_64)
X86_CC := gcc
else
X86_CC := x86_64-linux-gnu-gcc
endif

# On an x86_64 host build/game and build/game-x86_64 are the identical ~2 s
# compile run twice — accepted on purpose: two names, one contract each ("the
# harness" and "the shippable"), and a `cp` shortcut would silently break the
# day the container is aarch64 again.
# The real cost of putting the shippable in `all` lands on a NON-x86_64 host,
# where `make all` now HARD-REQUIRES the $(X86_CC) cross toolchain that the
# block above installs — before this rewrite it did not. On such a host either
# install it (the apt line above) or iterate with `make build/game`, which is
# the only target that never needs a cross compiler.
all: build/game.exe build/game build/game-x86_64

# Every module of the ONE translation unit. game.c is the skeleton and the
# MAP; the .inc files are its body, included in order. Without them here,
# `make` does not rebuild after a subsystem edit and you photograph the
# old binary (the clock-skew trap, one directory deeper).
INC := $(wildcard code/*/*.inc)

# Application icon + VERSIONINFO (Windows only): windres compiles game.rc
# (which embeds icon.ico) into an object file linked alongside the single C
# translation unit. VERFLAG is passed so the rc's version strings carry the
# same BUILD_VERSION the C build gets.
build/game.res.o: code/game.rc code/icon.ico
	mkdir -p build
	$(WIN_RES) --use-temp-file $(VERFLAG) $< -o $@

build/game.exe: code/game.c build/game.res.o $(INC)
	mkdir -p build
	$(WIN_CC) $(WIN_CFLAGS) $(VERFLAG) code/game.c build/game.res.o -o $@ $(WIN_LIBS)

build/game: code/game.c $(INC)
	mkdir -p build
	$(LIN_CC) $(LIN_CFLAGS) $(VERFLAG) $< -o $@ $(LIN_LIBS)

build/game-x86_64: code/game.c $(INC)
	mkdir -p build
	$(X86_CC) $(LIN_CFLAGS) $(VERFLAG) $< -o $@ $(LIN_LIBS)

# ASan/UBSan build for hunting memory bugs in the sim code (Linux only).
# float-cast-overflow and float-divide-by-zero are NOT in -fsanitize=undefined
# and are exactly the classes -ffast-math makes most likely (a NaN reaching an
# (int) cast, a zero-length normalize), so they are named explicitly.
build/game-asan: code/game.c $(INC)
	mkdir -p build
	$(LIN_CC) -std=c23 -O1 -g -fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero -Wall -Wextra $< -o $@ $(LIN_LIBS)

# The memory-safety gate: the standard proof battery run UNDER the sanitizers,
# ~10 s once the binary exists. What each half buys:
#   run 1 — the self-contained net proofs. netfuzz is the fuzzer this codebase
#           already ships (garbage packets into every decoder); under ASan a
#           decoder overread that happens to land in mapped memory stops being
#           silent and becomes a hard failure.
#   run 2 — 10 s of 20-bot sim (the gameplay hot paths), parity (shared
#           player/bot code), figcheck 1 + mapcheck 20 (one pass through every
#           figure/arena BUILDER — one tick is enough for memory errors, and
#           figcheck's full O(n^2) surface sweep costs minutes under ASan
#           while its geometry verdict is already gated in ci-proofs.sh).
# detect_leaks=0 because LeakSanitizer fires spuriously from inside Mesa
# (llvmpipe worker threads racing at exit, no game.c frame in the stack);
# a flaky gate is worse than no leak check. ASan/UBSan errors still abort
# with a non-zero exit, which is what CI reads.
asan-gate: build/game-asan
	ASAN_OPTIONS=detect_leaks=0 ./build/game-asan --seed 1337 --config "$$(mktemp -u)" --do "netpack 2000; netfuzz 50000; netpredict 600; netlagcomp; netstall; netanim 600; netfill; netloop 600; netdeath; netleave" >/dev/null
	ASAN_OPTIONS=detect_leaks=0 ./build/game-asan --seed 1337 --config "$$(mktemp -u)" --do "fraglimit 1000; bots 20; skill hard; wait 1200; parity; figcheck 1; mapcheck 20" >/dev/null
	@echo "asan-gate: OK"

# Copy the SHIPPABLE Linux build to the handheld's home directory and make it
# runnable there. `handheld` is an ssh config Host entry, which comes from the
# Windows host's ~/.ssh mounted by .devcontainer/devcontainer.json — so the key
# and the address live in one place instead of being repeated here.
#
# The guard reads the ELF machine field (0x3e00 LE = x86_64) before shipping:
# shipping a container-native build from the aarch64 devcontainer once put an
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

# ---------------------------------------------------------------------------
# Dedicated server, dockerized: docker-compose.yaml + docker/ ARE the
# deployment, living in ~/skill-issue on the box (server-up syncs it). The
# container bootstraps the release binary itself and SELF-UPDATES from the
# GitHub release feed — no deploy target, no .env needed (every knob carries
# its default inline; a .env in the repo root overrides, server-up ships it
# along). server.log + stats.txt sit next to the compose file. Design:
# docs/docker-server.md (untracked); the compose/entrypoint comments are the
# ops authority. SERVER_HOST is an ssh Host entry, like DEPLOY_HOST above.
# A reset is server-delete + server-up (clears the all-time stats, knowingly).
-include .env
SERVER_HOST ?= vps
SERVER_DIR  ?= skill-issue
# Empty-but-set SERVER_DIR in .env beats the `?=` and would make
# server-delete's rm -rf into "$$HOME/" — refuse it first.
SERVER_DIR_OK = @test -n "$(SERVER_DIR)" || { echo "SERVER_DIR is empty — refusing"; exit 1; }

server-up:        # sync + (re)create; idempotent
	$(SERVER_DIR_OK)
	ssh -n $(SERVER_HOST) 'mkdir -p $(SERVER_DIR)/docker'
	scp -q docker-compose.yaml $(SERVER_HOST):$(SERVER_DIR)/
	scp -q docker/Dockerfile docker/entrypoint.sh docker/report.awk \
	       $(SERVER_HOST):$(SERVER_DIR)/docker/
	@if [ -f .env ]; then scp -q .env $(SERVER_HOST):$(SERVER_DIR)/; fi
	ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose up -d --build'
server-down:      # stop + remove the container; files stay, server-up resumes
	ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose down'
server-stats:     # the report the container re-renders every minute
	@ssh -n $(SERVER_HOST) 'cat $(SERVER_DIR)/stats.txt 2>/dev/null \
	  || echo "  no stats.txt yet — is the container up? (make server-logs)"'
server-logs:      # raw server.log tail: updater lines, lobby digests, tele
	ssh -n $(SERVER_HOST) 'tail -n 40 $(SERVER_DIR)/server.log 2>/dev/null; true'
# DESTRUCTIVE: container, image AND the whole folder — binary, logs, ALL-TIME
# stats. Fresh start afterwards: server-up.
server-delete:
	$(SERVER_DIR_OK)
	-ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose down --rmi local'
	ssh -n $(SERVER_HOST) 'rm -rf --one-file-system "$$HOME/$(SERVER_DIR)"'

# Lines of C code: game.c plus the *.inc splits under code/. Only code/ —
# .superpowers/ holds stale snapshot copies of game.c that must not count.
loc:
	@find code -name '*.c' -o -name '*.h' -o -name '*.inc' | sort | xargs wc -l

# build/ carries a Dropbox-ignore NTFS attribute on the folder itself —
# delete its contents, never the folder, or the attribute is lost.
clean:
	rm -f build/game build/game.exe build/game-asan build/game-x86_64 build/game.res.o

init:
	clear
	git reset $$(git commit-tree -S HEAD^{tree} -m "init")
	git push --force origin main
	@echo "Git history reset to single 'init' commit"

.PHONY: all clean deploy init loc server-up server-down server-stats server-logs server-delete asan-gate
