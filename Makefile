# Tactical FPS prototype — one C23 file, three artifacts from the same source:
#   build/game.exe : Windows 11 deliverable (native window via WGL),
#                    cross-compiled with MinGW-w64, fully static.
#   build/game-x86_64 : the SHIPPABLE Linux binary — guaranteed x86_64
#                    whatever the container is (see X86_CC below); what
#                    `make deploy`, `make server-deploy` and CI publish.
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
# Dedicated server on the VPS — four commands, no systemd. These comments are
# the ops authority; there is no separate server document.
#   server-deploy      build + ship + (re)start ~/skill-issue/game, prove loopback
#   server-logs        render game.log into last-24h / all-time usage stats
#   server-logs-reset  truncate game.log IN PLACE (never rm — see the target)
#   server-delete      kill the process group and remove ~/skill-issue entirely
#
# These targets are the maintainer's own deploy path — the game itself needs
# none of them: any Linux copy runs a server with `./game --server`. `.env` in
# the repo root is the SINGLE source of truth and it is gitignored; make is now
# its ONLY parser (systemd and its EnvironmentFile are gone). No secret exists
# anywhere — the ssh identity is the SERVER_HOST Host entry in the mounted
# ~/.ssh/config, exactly like DEPLOY_HOST above. That is also why the server
# deploy is NOT a GitHub job: a runner would need host and key as repo secrets,
# i.e. a second copy of .env. release.yml ships the CLIENT, .env + this
# Makefile ship the SERVER, and .git/hooks/post-commit fires the second one.
#
# Process model: setsid + a sh respawn loop, pid in game.pid (== the process
# GROUP id, since setsid makes pid=pgid=sid — one negative kill takes wrapper
# and game down together). The wrapper (root) opens game.log BEFORE setpriv,
# so the game runs as `nobody` (it parses hostile UDP from the internet) yet
# writes a root-owned log through the inherited fd. Crash => restart in 2 s.
# ~/skill-issue lives under /root (0700) and that does NOT block nobody:
# setpriv execs ./game through the inherited CWD dentry — path resolution
# never walks /root. The dir itself is chmod 755 explicitly (umask-proof)
# and the game opens no other path (--config /dev/null). Do not "fix" this
# to /opt — the isolation-in-one-folder is the design.
# CONSCIOUS TRADE vs systemd: a VPS reboot needs a `make server-deploy`.
# game.log is APPEND across deploys (the stats history) — only server-delete
# removes it. The wrapper shell also drops its own job-status words into
# game.log (`Killed`, `Terminated`); that is harmless because
# tools/server-report.awk ignores every line that does not start with a unix
# timestamp.
-include .env
SERVER_HOST ?= vps
SERVER_DIR  ?= skill-issue
SERVER_PORT ?= 31415
GAME_LOBBIES    ?= 16
GAME_CAP_BOTS   ?= 1
GAME_CAP_PUBLIC ?= 4
GAME_SKILL      ?= 1
GAME_FRAGLIMIT  ?= 12
SERVER_ARGS = --server --config /dev/null --port $(SERVER_PORT) \
  --cap-bots $(GAME_CAP_BOTS) --cap-public $(GAME_CAP_PUBLIC) \
  --lobbies $(GAME_LOBBIES) --skill $(GAME_SKILL) --fraglimit $(GAME_FRAGLIMIT)
# Recursive on purpose: the lookup runs only in the targets that print or probe
# the address, not on every make invocation.
SERVER_ADDR = $(shell ssh -G $(SERVER_HOST) 2>/dev/null | awk '/^hostname /{print $$2}')

# EVERY destructive line below is `$$HOME/$(SERVER_DIR)`-relative and pastes
# that value UNQUOTED into a root shell on the VPS (`mkdir -p`, the scp
# destination, `D=`), so this guard runs FIRST in every server target — and
# once it has passed, every later line may interpolate the value freely.
# TWO clauses, and the ORDER matters:
#  1. anything outside [A-Za-z0-9._/-]. Whitespace and every shell
#     metacharacter (`;` `&` `|` backtick `$` `(` `)` `<` `>` `*` `?` `[` `]`
#     `\` and both quote characters) fall outside that set, so ONE whitelist
#     clause covers the whole class and stays auditable in a single bracket
#     expression — a blacklist of those characters, inside a `case` pattern,
#     inside a make variable, is exactly the nested-quoting construct this file
#     keeps getting bitten by. It runs FIRST so that clause 2 is allowed to
#     echo the offending value back at all.
#  2. empty, absolute, or containing `..`. An EMPTY-BUT-DEFINED key in .env —
#     a stray `SERVER_DIR=` — BEATS the `?=` above (make falls back only when a
#     variable is UNDEFINED, not when it is empty), D becomes "$$HOME/", and
#     server-delete's `rm -rf "$$D"` is then `rm -rf "$$HOME/"` as root.
# The value is read from the ENVIRONMENT (`export SERVER_DIR` + `$$SERVER_DIR`)
# and never interpolated into the recipe TEXT: with `case "$(SERVER_DIR)"` a
# value containing a backtick executes IN THE GUARD, before being rejected
# (measured — the guard printed the output of `id`). Shell parameter expansion
# does not re-scan its result, so the environment form is inert.
export SERVER_DIR
SERVER_DIR_GUARD = case "$$SERVER_DIR" in \
  *[!A-Za-z0-9._/-]*) \
    echo "SERVER_DIR may only contain A-Za-z0-9 and . _ / - — it is interpolated"; \
    echo "unquoted into a root shell on the VPS. Rejected: it holds whitespace or"; \
    echo "a shell metacharacter (deliberately not echoed back)."; exit 1;; \
  ""|/*|*..*) \
    echo "SERVER_DIR must be a non-empty relative path without '..' (got: '$$SERVER_DIR')"; \
    exit 1;; esac;

# ONE predicate, THREE callers (the deploy sweep, the delete sweep, the delete
# proof-of-clean re-scan): is pid $$1 one of ours? Kill by IDENTITY, never a
# pgrep pattern — a pattern matches the ssh command line carrying it, i.e. it
# kills its own shell. /proc/PID/exe finds the game but NEVER the respawn
# WRAPPER (its exe is /bin/sh -> /usr/bin/dash), and an unkilled wrapper is an
# immortal respawner (measured in rehearsal), so /proc/PID/cwd is the second
# half of the test: the wrapper sits IN the dir. The trailing `*` on the exe
# pattern and the explicit " (deleted)" cwd form absorb what the kernel appends
# once the binary or the dir has been removed under a still-running process.
# Sharing the predicate is the point: the purge's re-scan used to be exe-only,
# so it printed a clean line while the wrapper was alive — the one thing that
# proof exists to catch. It cannot drift from what the sweep kills any more.
# MUST be called with cwd OUTSIDE the dir, or the sweeping shell matches itself.
# The `|| e=""` / `|| c=""` are defence in depth, and they stay: every CURRENT
# call site is an `if` CONDITION, where POSIX suspends `set -e` for the whole
# dynamic extent of the function — but the moment srv_is_ours is called
# anywhere else, an assignment from a command substitution takes that command's
# status, and `readlink /proc/N/exe` returns 1 for every KERNEL THREAD (/proc/2
# = kthreadd on any real VM) and for any pid that exits between the glob and
# the readlink. This container's PID namespace has no kernel threads, which is
# exactly why a naive local rehearsal cannot see it; rehearse against a fake
# /proc entry with no exe symlink instead.
SERVER_MATCH = srv_is_ours() { \
    e="$$(readlink "/proc/$$1/exe" 2>/dev/null)" || e=""; \
    c="$$(readlink "/proc/$$1/cwd" 2>/dev/null)" || c=""; \
    case "$$e" in "$$D/game"*) return 0;; esac; \
    case "$$c" in "$$D"|"$$D (deleted)") return 0;; esac; \
    return 1; };
# The kill goes to the PGID (ps -o pgid=), which equals the wrapper's pid
# because setsid made it a session leader — one negative kill takes wrapper and
# game down together.
# (`\#\#` is correct HERE — make eats `#` in variable ASSIGNMENTS; in a
# recipe line the same expansion must be written `$${p##*/}` unescaped.)
SERVER_SWEEP = for p in /proc/[0-9]*; do q="$${p\#\#*/}"; \
  if srv_is_ours "$$q"; then \
    pg="$$(ps -o pgid= -p "$$q" 2>/dev/null | tr -d " ")"; \
    [ -n "$$pg" ] && kill -s KILL -- "-$$pg" 2>/dev/null || true; fi; done;
# game.pid is a SHORTCUT, never the authority. A stale pidfile whose pgid has
# been recycled sends TERM to an unrelated process group and `|| true` hides
# it — so the pid is put through the same predicate first, and anything that
# fails it simply falls through to the sweep, which needs no pidfile at all.
# The empty-string case pattern is written `""`, never `''`: this whole body
# travels inside a SINGLE-quoted ssh argument, where a `''` closes that quote
# and silently re-opens it around the rest of the script (measured — dash died
# with "Syntax error: word unexpected"). Same reason `\#` never appears here.
# `|| pid=""` is NOT redundant and must not be stripped: unlike srv_is_ours's
# guards this one sits in an `if` BODY, not a condition, so `set -e` is live —
# a game.pid deleted between the `-f` test and the `cat` would kill the deploy
# shell outright.
SERVER_PIDKILL = if [ -f "$$D/game.pid" ]; then \
    pid="$$(cat "$$D/game.pid" 2>/dev/null)" || pid=""; \
    case "$$pid" in ""|*[!0-9]*) pid="";; esac; \
    if [ -n "$$pid" ] && srv_is_ours "$$pid"; then \
      kill -s TERM -- "-$$pid" 2>/dev/null || true; fi; fi;

# game.new is scp'd INTO the target directory, never /tmp: the rename must be
# same-fs so it cannot depend on the kill/sweep having actually released the
# binary. /tmp may be a different filesystem, in which case `mv` degrades to a
# write into the destination inode — ETXTBSY against a still-running server.
# `kill -s TERM` (not `-TERM`) because dash rejects `kill -TERM -- -N`: "POSIX
# sh" has to mean dash-clean. `rc=$$?` is captured BEFORE the `$$(date …)`
# substitution, which clobbers `$$?` under bash-as-sh. The `systemctl stop
# "game@*"` line is a TRANSITION guard: the legacy systemd stack keeps port
# 31415 with Restart=always, so without it the new wrapper's game fails bind
# and respawn-loops forever while the loopback probe greps CONNECTED against
# the OLD server — green, and completely wrong. `disable` belongs beside it:
# `stop` alone leaves the units ENABLED, so the next VPS reboot resurrects the
# old server on 31415. And the explicit `rm -f` of the wants-symlink belongs
# beside THAT: `disable` takes unit NAMES and its glob expansion is not
# guaranteed, so if it refuses "game@*" the fix is a silent no-op — and the
# symlink in multi-user.target.wants is the thing that actually survives a
# reboot. server-delete has always removed it explicitly; the deploy now does
# too. The crash-loop gate after the probe is the second half of that same
# defence. The CRLF .env guard and the warning-level public-path probe are
# carried over from the systemd Makefile: the provider firewall is a SECOND
# firewall a loopback probe cannot see, and qemu-x86_64-static only exists on
# an aarch64 host (hence `command -v`).
# EVERY failure branch prints `tail -5 game.log`, because the log is two lines
# away from the answer and the operator cannot see it: a bad .env knob
# (`--cap-public 99` -> "server: need 1 <= --cap-public (99) <= 8") or a
# missing lib makes the wrapper start FINE and the game die instantly, so the
# loopback probe is what fails — and it used to exit before the crash-loop gate
# could print anything, leaving a 1.7 MB/day respawner on the box with no
# printed reason.
server-deploy: build/game-x86_64
	@$(SERVER_DIR_GUARD)
	@test "$$(od -An -tx1 -j18 -N2 $< | tr -d ' ')" = "3e00" || \
	  { echo "$< is not x86_64 — refusing to deploy"; exit 1; }
	@test -f .env || \
	  { echo "no .env in the repo root — refusing to deploy on built-in defaults."; \
	    echo "the keys are the SERVER_*/GAME_* block in this Makefile."; exit 1; }
	@grep -q "$$(printf '\r')" .env && \
	  { echo ".env has CRLF line endings — make mis-parses them and the CR"; \
	    echo "reaches the remote command line. fix: sed -i 's/\r$$//' .env"; exit 1; } || true
	@ssh -n $(SERVER_HOST) 'mkdir -p $(SERVER_DIR) && chmod 755 $(SERVER_DIR)'
	@scp -q $< $(SERVER_HOST):$(SERVER_DIR)/game.new
	@ssh -n $(SERVER_HOST) 'set -e; D="$$HOME/$(SERVER_DIR)"; $(SERVER_MATCH) \
	  systemctl stop "game@*" 2>/dev/null || true; \
	  systemctl disable "game@*" 2>/dev/null || true; \
	  rm -f /etc/systemd/system/multi-user.target.wants/game@*.service 2>/dev/null || true; \
	  $(SERVER_PIDKILL) \
	  sleep 1; $(SERVER_SWEEP) rm -f "$$D/game.pid"; \
	  mv -f "$$D/game.new" "$$D/game"; chmod 755 "$$D/game"; \
	  if ldd "$$D/game" | grep -q "not found"; then ldd "$$D/game" | grep "not found"; \
	    echo "fix: apt-get install -y libx11-6 libegl1 libgl1"; exit 1; fi; \
	  cd "$$D"; \
	  setsid sh -c '\''echo $$$$ > game.pid; while :; do \
	      setpriv --reuid nobody --regid nogroup --clear-groups --no-new-privs \
	        ./game $(SERVER_ARGS); \
	      rc=$$?; echo "$$(date +%s) exit rc=$$rc restart in 2 s"; sleep 2; \
	    done'\'' >> game.log 2>&1 < /dev/null & \
	  sleep 1; kill -0 "$$(cat game.pid)" 2>/dev/null && \
	    echo "running on :$(SERVER_PORT), pgid $$(cat game.pid)" || \
	    { echo "FAIL: wrapper did not start"; tail -5 game.log; exit 1; }'
	@ssh -n $(SERVER_HOST) '$$HOME/$(SERVER_DIR)/game --config /dev/null \
	    --do "netclient 127.0.0.1 $(SERVER_PORT) 120"' 2>/dev/null | grep CONNECTED || \
	  { echo "FAIL: server does not answer on loopback — last lines of game.log:"; \
	    ssh -n $(SERVER_HOST) 'tail -5 $(SERVER_DIR)/game.log' 2>/dev/null; exit 1; }
	@out=$$(ssh -n $(SERVER_HOST) 'echo LOGBEGIN; tail -5 $(SERVER_DIR)/game.log 2>/dev/null'); \
	  case "$$out" in LOGBEGIN*) : ;; \
	    *) echo "FAIL: crash-loop gate got no answer from $(SERVER_HOST) — an ssh"; \
	       echo "failure must not read as a healthy server. game.log unread."; exit 1;; esac; \
	  n=$$(printf '%s\n' "$$out" | grep -c "exit rc=") || n=0; \
	  [ "$$n" -lt 2 ] || { echo "FAIL: server is crash-looping — game.log tail:"; \
	    printf '%s\n' "$$out" | sed 1d; exit 1; }
	@{ command -v qemu-x86_64-static >/dev/null && Q=qemu-x86_64-static || Q=; \
	  $$Q $< --config /dev/null \
	    --do "netclient $(SERVER_ADDR) $(SERVER_PORT) 120" 2>/dev/null; } | grep -q CONNECTED && \
	  echo "public path ok" || \
	  echo "WARNING: $(SERVER_ADDR):$(SERVER_PORT)/udp unreachable from here — check the provider firewall"
	@echo "join with: ./build/game --connect $(SERVER_ADDR):$(SERVER_PORT)"

# Purge. No `cd` anywhere — srv_is_ours matches by cwd and would flag its own
# shell from inside the dir. `$${p##*/}` is UNESCAPED here (recipe context; the
# `\#\#` form belongs only in the variable assignment above).
#
# The LEGACY systemd block is ORDERED, and the order was paid for on the live
# box (2026-08-19). It used to open with `systemctl disable --now "game@*"` and
# then delete the unit file, and the purge printed a clean line while the old
# server was STILL RUNNING: pid 795, `exe -> /opt/skill-issue/game (deleted)`,
# cwd `/`, still bound to UDP 31415, with `systemctl list-units --all "game*"`
# reporting `game@31415.service  not-found  active running` — the unit FILE had
# been removed while the service ran on, so systemd kept the runtime unit and
# could no longer resolve its definition. Cause: `disable` takes unit NAMES and
# its glob expansion is NOT guaranteed, and the `2>/dev/null || true` swallowed
# the refusal. `stop` DOES match loaded units — which is why server-deploy's
# transition guard (stop first) cleaned the orphan up a minute later. So the
# order here is fixed and load-bearing: STOP, then disable, then remove the
# unit file and the wants-symlink, then daemon-reload, then remove the payload
# directories. (`--now` is gone from `disable` for the same reason: it only
# runs if the glob resolved, so it was never the thing doing the stopping.)
# These lines STAY now that the migration is done — the old Makefile is still
# in git history, so a future checkout can recreate that stack.
#
# The re-scan is TWO questions, because one predicate cannot answer both eras.
# `srv_is_ours` covers the NEW install and is the same predicate the sweep
# uses, so the proof cannot drift from what was killed (exe-only it was
# structurally blind to the wrapper, whose exe is /usr/bin/dash, and it printed
# a clean line while an immortal respawner was alive). It says NOTHING about a
# legacy survivor: that orphan's exe is /opt/skill-issue/game and its cwd is
# `/`, so neither half of the predicate matches $$HOME/$(SERVER_DIR) — exactly
# why the live purge read clean while a process held the port. The second,
# exe-only check on /opt/skill-issue/game* closes that gap, and costs one
# readlink on the pids the first question already rejected.
server-delete:
	@$(SERVER_DIR_GUARD)
	@ssh -n $(SERVER_HOST) 'D="$$HOME/$(SERVER_DIR)"; $(SERVER_MATCH) \
	  $(SERVER_PIDKILL) \
	  sleep 1; $(SERVER_SWEEP) \
	  rm -rf "$$D"; \
	  systemctl stop "game@*" 2>/dev/null || true; \
	  systemctl disable "game@*" 2>/dev/null || true; \
	  rm -f /etc/systemd/system/game@.service \
	        /etc/systemd/system/multi-user.target.wants/game@*.service; \
	  systemctl daemon-reload 2>/dev/null || true; \
	  rm -rf /opt/skill-issue /etc/skill-issue; \
	  left=""; leg=""; for p in /proc/[0-9]*; do q="$${p##*/}"; \
	    if srv_is_ours "$$q"; then left="$$left $$q"; \
	    else le="$$(readlink "$$p/exe" 2>/dev/null)" || le=""; \
	      case "$$le" in /opt/skill-issue/game*) leg="$$leg $$q";; esac; fi; done; \
	  echo "purged ~/$(SERVER_DIR) (+ legacy systemd) on $(SERVER_HOST)$${left:+ (STILL RUNNING:$$left)}$${leg:+ (LEGACY STILL RUNNING:$$leg)}"'

# The first line of the pipe is the byte size — tools/server-report.awk treats a
# lone-number first record as the size header, everything after it as log lines.
# No log yet => empty pipe => the awk prints its "no game.log yet" notice.
# The `2>/dev/null` PRECEDES `wc -c <` on purpose: redirections apply left to
# right, and with the stderr redirect last the shell's own "cannot open"
# message for a missing log escapes to the terminal before stderr is silenced.
# The guard runs here too. SERVER_DIR is interpolated unquoted into a remote shell
# on every one of these targets; this was the only one that skipped the check.
server-logs:
	@$(SERVER_DIR_GUARD)
	@ssh -n $(SERVER_HOST) '2>/dev/null wc -c < $(SERVER_DIR)/game.log; \
	    cat $(SERVER_DIR)/game.log 2>/dev/null' \
	  | awk -v now="$$(date +%s)" -f tools/server-report.awk

# TRUNCATE IN PLACE, NEVER `rm`. The respawn wrapper holds this exact file open
# with `>> game.log` (see server-deploy), so removing it leaves the server
# writing to a deleted inode: every line until the next server-deploy goes
# nowhere, and `make server-logs` then reports an empty log that is not empty
# but UNREACHABLE — the one failure mode a reset must not be able to cause.
# `: > file` keeps the inode and O_APPEND simply continues at 0.
#
# It prints the byte count it cleared, because a reset that says nothing is
# indistinguishable from an ssh that never landed; and it names what is lost,
# because the report's "all time" column is all-time OF THIS LOG — the unique
# player count starts over here. No prompt: an explicitly typed target IS the
# confirmation, and the same reasoning the destructive `server-delete` uses.
server-logs-reset:
	@$(SERVER_DIR_GUARD)
	@ssh -n $(SERVER_HOST) 'D="$$HOME/$(SERVER_DIR)"; \
	  if [ ! -f "$$D/game.log" ]; then \
	    echo "no game.log on $(SERVER_HOST) — nothing to reset"; exit 0; fi; \
	  n=$$(wc -c < "$$D/game.log"); : > "$$D/game.log"; \
	  echo "game.log reset on $(SERVER_HOST) ($$n bytes cleared; all-time stats start over)"'

# build/ carries a Dropbox-ignore NTFS attribute on the folder itself —
# delete its contents, never the folder, or the attribute is lost.
clean:
	rm -f build/game build/game.exe build/game-asan build/game-x86_64 build/game.res.o

init:
	clear
	git reset $$(git commit-tree -S HEAD^{tree} -m "init")
	git push --force origin main
	@echo "Git history reset to single 'init' commit"

.PHONY: all clean deploy init server-deploy server-delete server-logs server-logs-reset asan-gate
