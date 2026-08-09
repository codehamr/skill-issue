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

# Dedicated server on the VPS (docs/OPS.md, design §3).
#
# `.env` in the repo root is the SINGLE source of truth, and it is gitignored:
# where the server runs, on which ports, with which gameplay knobs. THREE
# parsers read that one file without conversion — make (the -include below),
# systemd on the VPS (EnvironmentFile=, so every GAME_* key reaches ExecStart)
# and /bin/sh. That only holds while every value is a single unquoted token,
# which is why the knobs are one key each instead of one SERVER_OPTS string:
# "--fighters 8" is one word to make, two to sh and a third thing to systemd.
#
# Nothing is placed on the VPS by hand and no secret exists anywhere:
# `server-start` ships binary + env + unit, `server-kill` removes all of them
# again, and the ssh identity is the `plaxtoris` Host entry in the mounted
# ~/.ssh/config — exactly like DEPLOY_HOST above. That is also why the server
# deploy is NOT a GitHub job: a runner would need host and key as repo secrets,
# i.e. a second copy of this file. release.yml ships the CLIENT, .env + this
# Makefile ship the SERVER, and .git/hooks/post-commit fires the second one.
#
# systemd owns the process (tools/game@.service): restart on crash, reboot
# survival, DynamicUser privilege drop, journald. These targets only install
# and (re)start it — which is why they no longer carry a PID scan for the happy
# path, a useradd, a setpriv line, a chown/chmod ladder or a log file. The one
# thing that survived is SERVER_PIDS, and only for server-kill (see there).
-include .env
SERVER_HOST  ?= plaxtoris
SERVER_DIR   ?= /opt/skill-issue
SERVER_ETC   ?= /etc/skill-issue
SERVER_PORTS ?= 27015
# One systemd instance per port. The comma keeps the .env value single-token;
# the FIRST port is what server-check probes and what the join line prints.
COMMA            := ,
SERVER_PORT_LIST := $(subst $(COMMA), ,$(SERVER_PORTS))
SERVER_PORT      := $(firstword $(SERVER_PORT_LIST))
SERVER_UNITS     := $(patsubst %,game@%,$(SERVER_PORT_LIST))
# Recursive on purpose: the lookup runs only in the targets that print or probe
# the address, not on every make invocation.
SERVER_ADDR       = $(shell ssh -G $(SERVER_HOST) 2>/dev/null | awk '/^hostname /{print $$2}')

# Identity of "our server process" is the EXECUTABLE, read from /proc/PID/exe —
# not a pgrep -f pattern. Two reasons, both bugs a pattern actually has:
# `pgrep -f "game --server"` matches the ssh command line carrying that very
# pattern (it kills its own shell), and it would also match any unrelated
# process whose arguments happen to contain the string. The trailing * absorbs
# the " (deleted)" the kernel appends after the binary has been replaced under
# a still-running process. systemd owns the units, so this exists for exactly
# one job now: server-kill purging a leftover no unit knows about — started by
# hand, or by an older Makefile.
SERVER_PIDS = for p in /proc/[0-9]*; do case "$$(readlink $$p/exe 2>/dev/null)" in $(SERVER_DIR)/game*) echo $${p\#\#*/};; esac; done

# Install and (re)start; idempotent, and it works on a bare VPS. The stop +
# wants-symlink sweep BEFORE enabling is what makes SERVER_PORTS authoritative:
# drop a port from .env and its instance is gone, not orphaned on the box.
# The binary is installed as .new and renamed into place — rename(2) over a
# running executable is legal, a write into it is ETXTBSY.
server-start: build/game-x86_64
	@test "$$(od -An -tx1 -j18 -N2 $< | tr -d ' ')" = "3e00" || \
	  { echo "$< is not x86_64 — refusing to deploy"; exit 1; }
	@grep -q "$$(printf '\r')" .env && \
	  { echo ".env has CRLF line endings — make and systemd both mis-parse them."; \
	    echo "fix: sed -i 's/\r$$//' .env"; exit 1; } || true
	@sed -e 's|@DIR@|$(SERVER_DIR)|g' -e 's|@ETC@|$(SERVER_ETC)|g' \
	  tools/game@.service > build/game@.service
	@# The GAME_* block is copied out of the FILE, not out of make's variables:
	@# that way adding a knob means editing .env and the unit, and nothing here
	@# needs to learn the key names. Consequence worth knowing — a command-line
	@# `make server-start GAME_FIGHTERS=4` does nothing; edit .env instead.
	@grep '^GAME_' .env > build/server.env
	@scp -q $< build/server.env build/game@.service $(SERVER_HOST):/tmp/
	@ssh $(SERVER_HOST) 'set -e; \
	  systemctl stop "game@*" 2>/dev/null || true; \
	  rm -f /etc/systemd/system/multi-user.target.wants/game@*.service; \
	  install -d $(SERVER_DIR) $(SERVER_ETC); \
	  install -m755 /tmp/game-x86_64 $(SERVER_DIR)/game.new; \
	  mv -f $(SERVER_DIR)/game.new $(SERVER_DIR)/game; \
	  install -m644 /tmp/server.env $(SERVER_ETC)/server.env; \
	  install -m644 /tmp/game@.service /etc/systemd/system/game@.service; \
	  rm -f /tmp/game-x86_64 /tmp/server.env /tmp/game@.service; \
	  if ldd $(SERVER_DIR)/game | grep -q "not found"; then \
	    echo "$(SERVER_HOST) is missing the ELF NEEDED libs (the server touches"; \
	    echo "none of them, but the loader resolves them before main):"; \
	    ldd $(SERVER_DIR)/game | grep "not found"; \
	    echo "fix: apt-get install -y libx11-6 libegl1 libgl1"; exit 1; fi; \
	  systemctl daemon-reload; \
	  systemctl reset-failed $(SERVER_UNITS) 2>/dev/null || true; \
	  systemctl enable --now $(SERVER_UNITS) 2>&1 | grep -v "^Created symlink" || true; \
	  systemctl is-active $(SERVER_UNITS) | tr "\n" " "; echo "$(SERVER_UNITS)"'
	@$(MAKE) --no-print-directory server-check

# Two probes, deliberately different. The loopback one runs the server's OWN
# binary as a headless client and proves the SERVER — hard gate. The external
# one proves the PATH and only warns, because the provider firewall is a second
# firewall outside this Makefile's reach (measured: a UDP port without an
# explicit Hetzner Cloud rule never reaches the host at all, docs/OPS.md).
# netclient prints its verdict and exits 0 either way, so both are grep gates,
# never exit-code gates. The external client MUST be the x86_64 build under
# qemu: an aarch64 client generates a different arena and is refused with
# reject=5 NR_WORLD — the check would fail on a perfectly healthy server.
server-check: build/game-x86_64
	@ssh -n $(SERVER_HOST) '$(SERVER_DIR)/game --config /dev/null \
	    --do "netclient 127.0.0.1 $(SERVER_PORT) 120"' 2>/dev/null | grep CONNECTED || \
	  { echo "FAIL: game@$(SERVER_PORT) does not answer on loopback"; exit 1; }
	@qemu-x86_64-static $< --config /dev/null \
	    --do "netclient $(SERVER_ADDR) $(SERVER_PORT) 120" 2>/dev/null | grep -q CONNECTED && \
	  echo "public path ok" || \
	  echo "WARNING: $(SERVER_ADDR):$(SERVER_PORT)/udp unreachable from here — open the port in the provider firewall"
	@echo "join with: ./build/game --connect $(SERVER_ADDR):$(SERVER_PORT)"

# Stop without uninstalling — the counterpart to server-kill's purge.
server-stop:
	@ssh $(SERVER_HOST) 'systemctl stop "game@*" 2>/dev/null || true; \
	  echo "stopped $(SERVER_UNITS) on $(SERVER_HOST) (install kept)"'

# Purge: units, enable-symlinks, binary, env, state, any leftover process no
# unit owns, and the static `skill-issue` account the pre-systemd version of
# this Makefile created with useradd (DynamicUser needs none, so a persistent
# uid on the box is now by definition a leftover). Leaves the VPS as it was
# before the first server-start — and the kill runs BEFORE the rm, or
# /proc/PID/exe no longer matches anything.
server-kill:
	@ssh $(SERVER_HOST) 'systemctl stop "game@*" 2>/dev/null || true; \
	  systemctl disable "game@*" 2>/dev/null || true; \
	  systemctl reset-failed "game@*" 2>/dev/null || true; \
	  rm -f /etc/systemd/system/game@.service \
	        /etc/systemd/system/multi-user.target.wants/game@*.service; \
	  systemctl daemon-reload; \
	  pids=$$($(SERVER_PIDS)); [ -n "$$pids" ] && kill -9 $$pids 2>/dev/null; \
	  rm -rf $(SERVER_DIR) $(SERVER_ETC) /var/lib/skill-issue /var/lib/private/skill-issue; \
	  userdel skill-issue 2>/dev/null || true; \
	  left=$$($(SERVER_PIDS)); \
	  echo "purged unit+binary+env+state on $(SERVER_HOST)$${left:+ (STILL RUNNING: $$left)}"'

server-log:
	@ssh -t $(SERVER_HOST) 'journalctl -u "game@$(SERVER_PORT)" -n 40 -f'

# build/ carries a Dropbox-ignore NTFS attribute on the folder itself —
# delete its contents, never the folder, or the attribute is lost.
clean:
	rm -f build/game build/game.exe build/game-asan build/game-x86_64 build/game.res.o

init:
	clear
	git reset $$(git commit-tree -S HEAD^{tree} -m "init")
	git push --force origin main
	@echo "Git history reset to single 'init' commit"

.PHONY: all clean deploy init \
        server-start server-check server-stop server-kill server-log
