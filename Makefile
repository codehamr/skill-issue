# One C23 translation unit, three artifacts from the same source:
#   build/game.exe    Windows x86_64 release, cross-compiled with MinGW-w64.
#   build/game-x86_64 Linux x86_64 release for publication.
#   build/game        host-native Linux play and headless harness.
# Linux play uses X11/EGL; --do/--script uses a surfaceless EGL framebuffer.
# Query the actual renderer before interpreting timings. XInput2, RandR and
# ALSA are loaded dynamically; missing optional libraries use bounded fallbacks.
# The Makefile's library variables specify linked dependencies.
#
# CPU work includes simulation, procedural geometry and render submission;
# GPU passes have separate timing owners. LTO reaches the single compile/link
# command. Treat optimization flags as artifact identity: compare matched
# flags for pose/pixel A/B and re-run relevant proofs when changing a profile.

# Clear an interactive terminal once, before any recipe (also under make -j).
# $(shell ...) captures stdout, so use the inherited terminal on stderr.
# Nested Make calls preserve the outer command's output.
ifeq ($(MAKELEVEL),0)
ifeq ($(MAKE_RESTARTS),)
MAKE_CLEAR := $(shell if [ -t 2 ]; then clear >&2 2>/dev/null || :; fi)
endif
endif
BUILD_STARTED := $(shell python3 -c 'import time; print(time.monotonic())')

OPT      := -O3 -ffast-math -funroll-loops -flto=auto
# The Windows release targets x86-64-v2, without requiring AVX2 or FMA.
# Keep this target-specific ISA choice out of the native Linux flags.
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

# Linux disables tree vectorization to avoid introducing a libmvec dependency
# through fast-math vector calls. Inspect actual ELF dependencies and symbol
# versions after toolchain changes; this flag does not set a universal glibc floor.
LIN_CC     := gcc
LIN_CFLAGS := -std=c23 $(OPT) -fno-tree-vectorize -Wall -Wextra -Wshadow
LIN_LIBS   := -lEGL -lGL -lX11 -lm

# The Linux release is x86_64. Use native GCC on that host architecture,
# otherwise the x86_64 cross-compiler and matching GL/X11 development libraries.
# Debian ARM64 setup: dpkg --add-architecture amd64 && apt-get install
# gcc-x86-64-linux-gnu libx11-dev:amd64 libegl-dev:amd64 libgl-dev:amd64
# qemu-user-static (for foreign-architecture harness checks).
ifeq ($(shell uname -m),x86_64)
X86_CC := gcc
else
X86_CC := x86_64-linux-gnu-gcc
endif

# build/game follows the host; build/game-x86_64 has a fixed release target.
# They remain separate artifacts even on x86_64. Use make build/game to iterate
# without a Linux cross-compiler on another host architecture.
all: build/game.exe build/game build/game-x86_64
	@python3 -c 'import time; elapsed = time.monotonic() - $(BUILD_STARTED); hours, remainder = divmod(int(elapsed), 3600); minutes, seconds = divmod(remainder, 60); print("build: elapsed %02d:%02d:%02d (%.1f s)" % (hours, minutes, seconds, elapsed))'

# Application icon + VERSIONINFO (Windows only): windres compiles game.rc
# (which embeds icon.ico) into an object file linked alongside the single C
# translation unit. VERFLAG is passed so the rc's version strings carry the
# same BUILD_VERSION the C build gets.
# Every request enters one host transaction, even when timestamps are in the
# future. All requested artifacts consume one immutable tuning snapshot. The
# host tool serializes competing invocations and publishes successful outputs.
GAME_ARTIFACTS := build/game build/game.exe build/game-x86_64 build/game-asan build/game.res.o build/game-warning-linux.o build/game-warning-windows.o
GAME_REQUESTS = $(filter $(GAME_ARTIFACTS),$(MAKECMDGOALS))
ifeq ($(strip $(MAKECMDGOALS)),)
GAME_REQUESTS += build/game build/game.exe build/game-x86_64
endif
ifneq ($(filter all rebuild,$(MAKECMDGOALS)),)
GAME_REQUESTS += build/game build/game.exe build/game-x86_64
endif
ifneq ($(filter asan-gate,$(MAKECMDGOALS)),)
GAME_REQUESTS += build/game-asan
endif
ifneq ($(filter warning-gate,$(MAKECMDGOALS)),)
GAME_REQUESTS += build/game-warning-linux.o build/game-warning-windows.o
endif
export LIN_CC X86_CC WIN_CC WIN_RES LIN_CFLAGS WIN_CFLAGS LIN_LIBS WIN_LIBS BUILD_VERSION BUILD_COMMIT
ifneq ($(origin TUNING_SOURCE),undefined)
export TUNING_SOURCE
endif
ifneq ($(origin CPPFLAGS),undefined)
export CPPFLAGS
endif
ifneq ($(origin SAN_CFLAGS),undefined)
export SAN_CFLAGS
endif
ifneq ($(origin WARN_CFLAGS),undefined)
export WARN_CFLAGS
endif
.PHONY: game-build warning-gate defaults-gate
# --always-make is an explicit compile request; ordinary checks preserve mtimes.
game-build:
	python3 tools/build-game.py $(if $(filter rebuild,$(MAKECMDGOALS)),--rebuild) $(if $(findstring B,$(firstword $(MAKEFLAGS))),--force) $(sort $(GAME_REQUESTS))
$(GAME_ARTIFACTS): game-build
	@:
warning-gate: build/game-warning-linux.o build/game-warning-windows.o
defaults-gate:
	python3 tools/defaults-gate.py

# Memory-safety checks under the sanitizers. Each invocation owns a fresh config:
#
#   run 1 — the self-contained net proofs. netfuzz is the fuzzer this codebase
#           already ships (garbage packets into every decoder); under ASan a
#           decoder overread that happens to land in mapped memory stops being
#           silent and becomes a hard failure.
#   run 2 — 10 s of 20-bot sim (the gameplay hot paths), parity (shared
#           player/bot code), figcheck 1 + mapcheck 20 (one pass through every
#           figure/arena BUILDER — one tick is enough for memory errors, and
#           figcheck's full O(n^2) surface sweep costs minutes under ASan
#           while its geometry verdict is already gated in ci-proofs.sh).
# Leak detection is disabled for this Mesa-backed gate. ASan/UBSan findings
# still fail the command; passing this recipe is not a leak-safety certificate.
asan-gate: build/game-asan
	ASAN_OPTIONS=detect_leaks=0 ./build/game-asan --seed 1337 --config "$$(mktemp -u)" --do "netpack 2000; netfuzz 50000; netpredict 600; netlagcomp; netstall; netanim 600; netfill; netloop 600; netdeath; netleave" >/dev/null
	ASAN_OPTIONS=detect_leaks=0 ./build/game-asan --seed 1337 --config "$$(mktemp -u)" --do "fraglimit 1000; bots 20; skill hard; wait 1200; parity; figcheck 1; mapcheck 20" >/dev/null
	ASAN_OPTIONS=detect_leaks=0 ./build/game-asan --seed 42 --config "$$(mktemp -u)" --do "bottactics; netloss; defaultsproof; spstart; bots 50; skill hard; fraglimit off; wait 1200; appframe 12 12; budget" >/dev/null
	@echo "asan-gate: OK"

# ---------------------------------------------------------------------------
# Dedicated server operations: server-up syncs the deployment files in server/
# to SERVER_DIR on SERVER_HOST. The container bootstraps and updates the Linux
# release; .env overrides inline defaults and is copied only when present.
# The compose/entrypoint comments own runtime operations. server-delete removes
# the deployment directory, including accumulated logs and statistics.
-include server/.env
SERVER_HOST ?= vps
SERVER_DIR  ?= skill-issue
# Empty-but-set SERVER_DIR in server/.env beats the `?=` and would make
# server-delete's rm -rf into "$$HOME/" — refuse it first.
SERVER_DIR_OK = @test -n "$(SERVER_DIR)" || { echo "SERVER_DIR is empty — refusing"; exit 1; }

server-up:        # sync + (re)create; idempotent
	$(SERVER_DIR_OK)
	ssh -n $(SERVER_HOST) 'mkdir -p $(SERVER_DIR)'
	scp -q server/docker-compose.yaml server/Dockerfile server/.dockerignore \
	       server/entrypoint.sh server/report.awk $(SERVER_HOST):$(SERVER_DIR)/
	@if [ -f server/.env ]; then scp -q server/.env $(SERVER_HOST):$(SERVER_DIR)/; fi
	ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose up -d --build'
server-down:      # stop + remove the container; files stay, server-up resumes
	ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose down'
server-stats:     # the report the container re-renders every minute
	@ssh -n $(SERVER_HOST) 'cat $(SERVER_DIR)/stats.txt 2>/dev/null \
	  || echo "  no stats.txt yet — is the container up? (make server-logs)"'
server-logs:      # raw server.log tail: updater lines, lobby digests, tele
	ssh -n $(SERVER_HOST) 'tail -n 40 $(SERVER_DIR)/server.log 2>/dev/null; true'
# Clears all log/stat history, including Docker logs, and interrupts active games.
# Keep deployment files and data/; the fresh container renders stats within a minute.
server-reset:
	$(SERVER_DIR_OK)
	ssh -n $(SERVER_HOST) 'cd "$(SERVER_DIR)" && docker compose down && \
	  rm -f -- server.log stats.txt stats.txt.new && \
	  docker compose up -d --no-build'
# DESTRUCTIVE: container, image AND the whole folder — binary, logs, ALL-TIME
# stats. Fresh start afterwards: server-up.
server-delete:
	$(SERVER_DIR_OK)
	-ssh -n $(SERVER_HOST) 'cd $(SERVER_DIR) && docker compose down --rmi local'
	ssh -n $(SERVER_HOST) 'rm -rf --one-file-system "$$HOME/$(SERVER_DIR)"'

# Count source lines under code/ only; isolated copies elsewhere do not count.
loc:
	@rg --files code -g '*.c' -g '*.h' -g '*.inc' | sort | xargs wc -l

# Delete the entire build/ tree, including configs, caches and test evidence,
# then compile all three game binaries in one locked transaction.
rebuild: all

# Verify/rebuild the native binary, then capture all media from scratch.
# The driver includes the build check in elapsed time and preserves failures.
media:
	python3 -u media/build.py

# History-rewriting maintainer operation; never part of build or verification.
init:
	git reset $$(git commit-tree -S HEAD^{tree} -m "init")
	git push --force origin main
	@echo "Git history reset to single 'init' commit"

.PHONY: all rebuild media init loc server-up server-down server-stats server-logs server-reset server-delete asan-gate

# Pipe/ioctl fixtures exercise the Linux pump without opening real devices.
.PHONY: pad-native-gate
pad-native-gate:
	python3 tools/pad-native-gate.py
