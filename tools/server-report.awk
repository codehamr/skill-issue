# tools/server-report.awk — renders `make server-logs`. Input: optionally one
# lone byte-count line (the size header the Makefile prepends), then raw
# game.log lines per the grammar in server.md. Unknown lines are ignored by
# design — stderr noise must never break the report. -v now=<unix time>.
#
# THE REPORT IS PLAYERS AND PLAYTIME. It reads exactly one line shape,
# `tele id= [mode= dt=]`, and prints four rows. What used to be here and why it
# went:
#   - "mp connections" / "mp connection time" measured SOCKET time off the
#     join/leave lines: they counted telemetry opt-outs, counted every
#     `make server-deploy`'s own loopback probe, and a re-join split one
#     session into two. `mode=mp` beats answer the same question once.
#   - "peak players" / "peak arenas" came off the 10 s `stat` heartbeat, and
#     "app starts" / "unique players" / "windows / linux" off the boot ping.
#   Those four log lines no longer exist (see slog's comment in game.c), so
#   this file no longer parses them.
#
# UNIQUE PLAYERS CAME BACK, off the id the tele line already carries (2026-08-23)
# — and it is counted over EVERY tele line, boot ping or beat, not just the boot
# ping. The boot ping is one packet at start-up: a client that was already
# running when the log was reset, or whose first packet was lost, has no boot
# line all week and would be invisible while its beats are being counted into
# the playtime rows right above. Two honest limits, both worth knowing before
# quoting the number:
#   - `tele_id` identifies an INSTALL (rolled from the clock, persisted in
#     config.cfg — see the telemetry rules in CLAUDE.md), so one person on two
#     machines is two, and a wiped config is a new one. Hence the footnote the
#     report prints under the table.
#   - The id is attacker-controllable, like every other field in a UDP packet
#     nobody authenticates. This is a dashboard, not evidence.
#
# Portable: no gawk-isms (no strftime/systime), byte-identical on mawk+gawk.

BEGIN {
  if (now + 0 <= 0) { "date +%s" | getline now; close("date +%s") }
}

function kv(i,   a) { split($i, a, "="); return a[2] }

# SECONDS ARE PART OF THE READING, not decoration: a fresh server, a single
# test session or one `netclient` probe all live under a minute, and "0h 00m"
# is indistinguishable from "nothing was logged at all" — which is the exact
# question the first row of this report is asked after every deploy.
function pt(s,   h, m, sec) {
  s = int(s + 0)
  if (!(s >= 0)) s = 0   # negative AND NaN both fail ">=" -> clamp to 0
  h = int(s / 3600); m = int((s % 3600) / 60); sec = s % 60
  if (h > 999) { h = 999; m = 59; sec = 59 }  # cap so %3dh can't overflow
  return sprintf("%3dh %02dm %02ds", h, m, sec)
}

# normalize CRLF before anything else looks at $0/$1 — a \r left on the last
# field would otherwise make "sp\r" a mode this report has never heard of.
{ sub(/\r$/, "") }

# size-header line: the Makefile prepends a lone byte count before the log.
NR == 1 && NF == 1 && $1 ~ /^[0-9]+$/ { bytes = $1 + 0; hdr = 1; next }

# every real grammar line and every wrapper line starts with a unix
# timestamp — anything else (stderr, partial writes) is not ours to parse.
$1 !~ /^[0-9]+$/ { next }

{ ts = $1 + 0; day = (ts >= now - 86400) }

{ if (!first || ts < first) first = ts }

$2 == "tele" {
  mode = ""; dt = 0; id = ""; modeset = 0; dtset = 0; idset = 0
  for (i = 3; i <= NF; i++) {
    # first-match-wins via an explicit SEEN flag, not "value == \"\"" — the
    # latter is defeated by an injected duplicate whose own value is empty
    # (mode= with no value): that reads as "still unset" and lets a LATER
    # duplicate win, exactly the bug this guard exists to close.
    if ($i ~ /^id=/   && !idset)   { id = kv(i); idset = 1 }
    if ($i ~ /^mode=/ && !modeset) { mode = kv(i); modeset = 1 }
    if ($i ~ /^dt=/   && !dtset) {
      dt = kv(i) + 0
      if (!(dt >= 0)) dt = 0   # clamp AT THE SOURCE: a negative or NaN dt
                               # contributes 0 instead of corrupting the
                               # running t[mode]/t24[mode] sum
      dtset = 1
    }
  }
  # An id with no value (`id=`) is not an install — counting it would make one
  # malformed line into "1 player". Counted with explicit counters rather than
  # length(seen): length() on an array is a gawk-ism and this file runs on mawk.
  if (id != "") {
    if (!(id in seen))   { seen[id] = 1; nuniq++ }
    if (day && !(id in seen24)) { seen24[id] = 1; nuniq24++ }
  }
  # boot pings carry no mode and fall out here; so does anything else.
  if (mode == "sp" || mode == "mp" || mode == "ui") {
    t[mode] += dt; if (day) t24[mode] += dt
  }
}

END {
  # keyed on "the pipe carried no records", not on which counters are
  # nonzero — a log of only digest lines is still a real log. hdr (not bytes)
  # so a 0-byte log still reads as "no log yet".
  if (NR <= hdr) {
    print "  no game.log on the server yet — run `make server-deploy` first"
    exit
  }

  if (bytes >= 1048576) sz = sprintf("%.1f MB", bytes / 1048576.0)
  else                  sz = sprintf("%.1f KB", bytes / 1024.0)

  dspan = 0
  if (first) {
    dspan = (now - first) / 86400.0
    if (!(dspan >= 0)) dspan = 0   # a clock-skewed/future-dated log must
                                   # not print a negative day count
  }

  printf "\n  skill-issue · server stats   (log covers %.1f days, %s)\n", \
         dspan, sz
  printf "  ------------------------------------------------------------\n"
  printf "  %-26s %12s %14s\n", "", "last 24 h", "all time"
  printf "  %-26s %12d %14d\n", "unique players",        nuniq24 + 0, nuniq + 0
  printf "  %-26s %12s %14s\n", "playtime singleplayer", pt(t24["sp"]), pt(t["sp"])
  printf "  %-26s %12s %14s\n", "playtime multiplayer",  pt(t24["mp"]), pt(t["mp"])
  printf "  %-26s %12s %14s\n", "playtime menu/ui",      pt(t24["ui"]), pt(t["ui"])
  printf "\n  unique = distinct telemetry install ids, not people or devices\n"
  printf "\n"
}
