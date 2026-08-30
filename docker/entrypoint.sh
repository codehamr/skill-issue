#!/bin/bash
# Supervisor for the dedicated server: bootstrap the release binary, respawn on
# crash, self-update from the release feed, render stats.txt every minute.
# Everything we and the game print goes to BOTH docker logs and server.log; our
# own lines carry the unix-ts prefix with an `updater` keyword, which
# report.awk ignores by design (it parses only `tele` rows).
set -u

: "${PORT:=31415}"
: "${LOBBIES:=16}"
: "${CAP_PUBLIC:=4}"
: "${CAP_BOTS:=1}"
: "${SKILL:=1}"
: "${FRAGLIMIT:=12}"
: "${UPDATE_URL:=https://github.com/codehamr/skill-issue/releases/latest/download}"
: "${UPDATE_INTERVAL:=300}"    # seconds between release checks; 0 disables
: "${UPDATE_MAX_DEFER:=3600}"  # apply even with players after this many seconds

SRV=/srv                       # the host's skill-issue folder, bind-mounted
LOG="$SRV/server.log"
STATS="$SRV/stats.txt"
DATA="$SRV/data"
BIN="$DATA/game"
ASSET=skill-issue-linux

mkdir -p "$DATA"
exec > >(tee -a "$LOG") 2>&1
say() { echo "$(date +%s) updater $*"; }

# --- release feed ------------------------------------------------------------
# SHA256SUMS is the ONE authority: its "# version YYYY-MM-DD" header is the
# date the client updater orders by, and its rows verify the binary. VERSION
# is never fetched separately (could straddle a release cut).
fetch_sums()    { curl -fsSL --max-time 30 "$UPDATE_URL/SHA256SUMS" -o "$DATA/SHA256SUMS.new"; }
sums_version()  { awk '/^# version /{print $3; exit}' "$DATA/SHA256SUMS.new" 2>/dev/null; }
local_version() { cat "$DATA/VERSION" 2>/dev/null || true; }
sums_sha()      { awk -v a="$ASSET" '$2 == a {print $1; exit}' "$1" 2>/dev/null; }

# Newer date wins; a SAME-date feed whose asset row carries a different sha is
# a RECUT (several pushes on one day re-cut `latest` under the same date — the
# client updater has a verdict for exactly this) and counts as an update too.
# Downgrades (older date) stay refused.
feed_is_newer() {
  local new cur
  new=$(sums_version); cur=$(local_version)
  [ -n "$new" ] || return 1
  [[ "$new" > "$cur" ]] && return 0                # ISO dates order lexically
  [ "$new" = "$cur" ] &&
    [ "$(sums_sha "$DATA/SHA256SUMS.new")" != "$(sums_sha "$DATA/SHA256SUMS")" ]
}

install_release() {   # $1 = version parsed from the freshly fetched SHA256SUMS
  curl -fsSL --max-time 300 "$UPDATE_URL/$ASSET" -o "$BIN.new" || return 1
  local want got
  want=$(awk -v a="$ASSET" '$2 == a {print $1; exit}' "$DATA/SHA256SUMS.new")
  got=$(sha256sum "$BIN.new" | awk '{print $1}')
  if [ -z "$want" ] || [ "$want" != "$got" ]; then
    say "sha256 mismatch on $ASSET (want=${want:-none} got=$got) — refusing"
    rm -f "$BIN.new"; return 1
  fi
  chmod +x "$BIN.new"
  mv -f "$BIN.new" "$BIN"                      # same filesystem: atomic swap
  echo "$1" > "$DATA/VERSION"
  mv -f "$DATA/SHA256SUMS.new" "$DATA/SHA256SUMS"
  say "installed $ASSET version=$1"
}

# --- dev override ------------------------------------------------------------
if [ -x /local/game ]; then
  BIN=/local/game
  UPDATE_INTERVAL=0
  say "using bind-mounted /local/game — self-update disabled"
fi

# --- bootstrap ---------------------------------------------------------------
if [ ! -x "$BIN" ]; then
  say "no binary yet — bootstrapping from $UPDATE_URL"
  until fetch_sums && install_release "$(sums_version)"; do
    say "bootstrap failed, retrying in 30 s"; sleep 30
  done
fi

# ONLY flags every released binary knows. --bot-fill/--idle-timeout join here
# once a release contains them (docs/server-gameplay.md).
ARGS=(--server --config /dev/null --port "$PORT" --lobbies "$LOBBIES"
      --cap-public "$CAP_PUBLIC" --cap-bots "$CAP_BOTS"
      --skill "$SKILL" --fraglimit "$FRAGLIMIT")

GAME_PID=0
start_game() { "$BIN" "${ARGS[@]}" & GAME_PID=$!;
               say "game started pid=$GAME_PID version=$(local_version)"; }
stop_game()  { [ "$GAME_PID" -gt 0 ] && kill -TERM "$GAME_PID" 2>/dev/null;
               wait "$GAME_PID" 2>/dev/null; GAME_PID=0; }
trap 'say "stopping (signal)"; stop_game; exit 0' TERM INT

# stats.txt = the aggregated report, cat-able on the host. Same reading as the
# ssh-era `make server-logs` pipe: byte-count header + log through report.awk.
stats() {
  { wc -c < "$LOG"; cat "$LOG"; } \
    | awk -v now="$(date +%s)" -f /report.awk > "$STATS.new" \
    && mv -f "$STATS.new" "$STATS" \
    || { rm -f "$STATS.new"; say "stats render failed"; }
}

# Empty = no occupied-lobby digest in the last 25 s: the server prints one
# `lobby N digest … humans=…` line per OCCUPIED arena every 10 s and nothing
# for empty ones, so recent silence IS emptiness.
srv_empty() {
  local cutoff=$(( $(date +%s) - 25 ))
  ! tail -n 400 "$LOG" 2>/dev/null \
      | awk -v c="$cutoff" '$1 + 0 >= c && $2 == "lobby"' | grep -q .
}

start_game
now=$(date +%s)
next_check=$(( now + UPDATE_INTERVAL ))
next_stats=$(( now + 60 ))
pending_since=0
while :; do
  sleep 10 & wait $!              # interruptible: the TERM trap fires mid-wait
  if ! kill -0 "$GAME_PID" 2>/dev/null; then
    wait "$GAME_PID" 2>/dev/null; rc=$?
    say "game exited rc=$rc — restart in 2 s"
    sleep 2; start_game; continue
  fi
  now=$(date +%s)
  if [ "$now" -ge "$next_stats" ]; then next_stats=$(( now + 60 )); stats; fi
  [ "$UPDATE_INTERVAL" -gt 0 ] || continue
  if [ "$pending_since" -eq 0 ] && [ "$now" -ge "$next_check" ]; then
    next_check=$(( now + UPDATE_INTERVAL ))
    if fetch_sums; then
      if feed_is_newer; then
        cur=$(local_version)
        say "update available: ${cur:-none} -> $(sums_version)"
        pending_since=$now
      fi
    else
      say "update check failed (fetch)"
    fi
  fi
  if [ "$pending_since" -gt 0 ] && \
     { srv_empty || [ $(( now - pending_since )) -ge "$UPDATE_MAX_DEFER" ]; }; then
    say "applying update (deferred $(( now - pending_since )) s)"
    if install_release "$(sums_version)"; then
      stop_game; sleep 1; start_game
    fi
    pending_since=0                # failed install: re-detect on the next check
  fi
done
