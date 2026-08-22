# tools/server-report.awk — renders `make server-logs`. Input: optionally one
# lone byte-count line (the size header the Makefile prepends), then raw
# game.log lines per the grammar in server.md. Unknown lines are ignored by
# design — stderr noise must never break the report. -v now=<unix time>.
#
# THE REPORT IS PLAYTIME, AND NOTHING ELSE (2026-08-19). It reads exactly one
# line shape, `tele ... mode= dt=`, and prints exactly three rows. What used to
# be here and why it went:
#   - "mp connections" / "mp connection time" measured SOCKET time off the
#     join/leave lines: they counted telemetry opt-outs, counted every
#     `make server-deploy`'s own loopback probe, and a re-join split one
#     session into two. `mode=mp` beats answer the same question once.
#   - "peak players" / "peak arenas" came off the 10 s `stat` heartbeat, and
#     "app starts" / "unique players" / "windows / linux" off the boot ping.
#   Those four log lines no longer exist (see slog's comment in game.c), so
#   this file no longer parses them.
#
# Portable: no gawk-isms (no strftime/systime), byte-identical on mawk+gawk.

BEGIN {
  if (now + 0 <= 0) { "date +%s" | getline now; close("date +%s") }
}

function kv(i,   a) { split($i, a, "="); return a[2] }

function pt(s,   h, m) {
  s = s + 0
  if (!(s >= 0)) s = 0   # negative AND NaN both fail ">=" -> clamp to 0
  h = int(s / 3600); m = int((s % 3600) / 60)
  if (h > 999) { h = 999; m = 59 }   # cap so %3dh can't overflow the column
  return sprintf("%3dh %02dm", h, m)
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
  mode = ""; dt = 0; modeset = 0; dtset = 0
  for (i = 3; i <= NF; i++) {
    # first-match-wins via an explicit SEEN flag, not "value == \"\"" — the
    # latter is defeated by an injected duplicate whose own value is empty
    # (mode= with no value): that reads as "still unset" and lets a LATER
    # duplicate win, exactly the bug this guard exists to close.
    if ($i ~ /^mode=/ && !modeset) { mode = kv(i); modeset = 1 }
    if ($i ~ /^dt=/   && !dtset) {
      dt = kv(i) + 0
      if (!(dt >= 0)) dt = 0   # clamp AT THE SOURCE: a negative or NaN dt
                               # contributes 0 instead of corrupting the
                               # running t[mode]/t24[mode] sum
      dtset = 1
    }
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
  printf "  %-26s %10s %14s\n", "", "last 24 h", "all time"
  printf "  %-26s %10s %14s\n", "playtime singleplayer", pt(t24["sp"]), pt(t["sp"])
  printf "  %-26s %10s %14s\n", "playtime multiplayer",  pt(t24["mp"]), pt(t["mp"])
  printf "  %-26s %10s %14s\n", "playtime menu/ui",      pt(t24["ui"]), pt(t["ui"])
  printf "\n"
}
