#!/bin/sh
# byte-gate.sh BIN OUTDIR compares deterministic text and pixels for a refactor.
# All 44 cases must exit zero and produce their expected artifacts. Equal failed
# cases on both sides of an A/B are invalid references. Compare successful output
# directories with diff -r; simulation and world text must remain identical.
# Each case owns a fresh config. Both 1 bot / normal and 20 bots / hard are covered.
set -eu
[ "$#" = 2 ] || { echo "usage: byte-gate.sh BIN OUTDIR" >&2; exit 2; }
BIN=$1; OUT=$2
[ -x "$BIN" ] || { echo "byte-gate: not executable: $BIN" >&2; exit 2; }
mkdir -p "$OUT"
[ ! -e "$OUT/manifest.txt" ] || {
  echo "byte-gate: preserve existing run; choose a new output directory: $OUT" >&2
  exit 2
}
: > "$OUT/manifest.txt"
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
case_fail=0
shot_fail=0
case_count=0

normalize() {
  # Temp paths, scatter wall time and the executable hash are not A/B witnesses.
  sed -e "s#$dir#TMP#g" \
      -e 's/plan_ms=[0-9.]*/plan_ms=X/' \
      -e '/^sha [0-9a-f]\{64\}$/d' "$1"
}

run() {  # run NAME SEED SCRIPT...
  name=$1; seed=$2; shift 2
  case_count=$((case_count + 1))
  dir="$TMPD/$name"; mkdir "$dir"
  if "$BIN" --seed "$seed" --config "$dir/config.cfg" --do "$*" \
      >"$dir/o" 2>"$dir/e"; then rc=0; else rc=$?; fi
  normalize "$dir/o" > "$OUT/$name.txt"
  printf '%s rc=%s %s\n' "$name" "$rc" \
    "$(md5sum < "$OUT/$name.txt" | cut -d' ' -f1)" >> "$OUT/manifest.txt"
  if [ "$rc" -ne 0 ] || [ ! -s "$OUT/$name.txt" ]; then
    echo "byte-gate: case $name exit=$rc (nonempty output required)" >&2
    normalize "$dir/e" > "$OUT/$name.stderr"
    case_fail=1
  fi
  rm -rf "$dir"
}

shot() {  # shot NAME SEED SCRIPT...; the script has one %s PNG placeholder.
  name=$1; seed=$2; shift 2
  case_count=$((case_count + 1))
  dir="$TMPD/$name"; mkdir "$dir"
  # shellcheck disable=SC2059
  scr=$(printf "$*" "$dir/s.png")
  if "$BIN" --seed "$seed" --config "$dir/config.cfg" --do "$scr" \
      >"$dir/o" 2>"$dir/e"; then rc=0; else rc=$?; fi
  if [ -s "$dir/s.png" ] && \
      [ "$(od -An -tx1 -N8 "$dir/s.png" | tr -d ' \n')" = 89504e470d0a1a0a ] && \
      [ "$(tail -c 12 "$dir/s.png" | od -An -tx1 | tr -d ' \n')" = 0000000049454e44ae426082 ]; then
    cp "$dir/s.png" "$OUT/$name.png"
    digest=$(md5sum < "$OUT/$name.png" | cut -d' ' -f1)
  else
    echo "byte-gate: missing or invalid PNG: $name" >&2
    digest=MISSING
    shot_fail=1
  fi
  printf '%s rc=%s png %s\n' "$name" "$rc" "$digest" >> "$OUT/manifest.txt"
  if [ "$rc" -ne 0 ] || [ "$digest" = MISSING ]; then
    echo "byte-gate: shot $name exit=$rc" >&2
    normalize "$dir/o" > "$OUT/$name.txt"
    normalize "$dir/e" > "$OUT/$name.stderr"
    case_fail=1
  fi
  rm -rf "$dir"
}

# Simulation: both population/difficulty extremes across multiple seeds.
for s in 1337 7 99 2; do
  run "sim-default-$s"  "$s" "trace 600; pos; wpn; bot; match"
  run "sim-1bot-$s"     "$s" "bots 1; skill normal; fraglimit 1000; wait 240; tacstat 12000; match"
  run "sim-20bot-$s"    "$s" "bots 20; skill hard; fraglimit 1000; wait 240; tacstat 12000; match"
done
run "locomotion" 1337 "botfreeze on; +forward; trace 240; tap jump; wait 90; pos; lean; skel 120; kine 120"
run "ragdoll"    1337 "bots 4; skill hard; fraglimit 1000; wait 600; rag 300; ragx 300; ragsoak 600; flinch"

# Figures and weapons: topology invariants and contact census.
run "figcheck"  1337 "figcheck 120; figcheck 120 6; figcheck 120 12; figcheck 120 25"
run "figv"      1337 "figv; figcheck 120; figv"
run "figm"      1337 "figm; puppet on; puppet ads 1; wait 60; figm; puppet crouch 1; wait 60; figm"
run "figbury"   1337 "figbury; figbury head; lean"
run "viewmodel" 1337 "vmcheck; vmsight; vmframe; vmtrig; vmhand"

# World, lighting, mood, scatter, parity and allocation budgets.
run "map"        1337 "map; sun; mood; scat; mapcheck 2000"
run "sun-low"    1337 "sun 8 120; sun; map"
run "mood-force" 1337 "mood 0; map; scat"
run "parity"     1337 "parity"
run "budget"     1337 "bots 20; skill hard; fraglimit 1000; wait 240; shot /dev/null; budget"

# Bot and pad proofs use command exit status as their verdict.
run "bothear"          1337 "bothear"
run "botweapon"        1337 "botweapon"
run "botflank"         1337 "botflank"
run "botmemoryobserve" 1337 "botmemoryobserve"
run "padbackend"       1337 "padbackend"
run "padcurve"         1337 "padcurve; padlook 120"
# The proof stages its declared normal-difficulty fixture explicitly.
run "botmemory-normal" 1337 "skill normal; wait 120; botmemory"
run "botmemory-hard"   1337 "skill hard; wait 120; botmemory"

# Networking includes netleave with zero teardown counters.
run "net" 1337 "netpack 2000; netpredict 600; netlagcomp; netstall; netanim 600; netfill; netloop 600; netdeath; netleave; netfuzz 5000"

# UI and HOME advance with appframe; wait does not advance g_ui_time.
run "ui-menu" 1337 "menu; uiframe; uistat; nav down; uiframe; nav ok; uiframe; uistat; cfg"
run "ui-home" 1337 "home on; appframe 240; uistat; appframe 240; uistat"

# Audio and update identity.
run "sfxlog"  1337 "bots 20; skill hard; fraglimit 1000; sfxlog 1200"
run "updinfo" 1337 "updinfo"

# Pixel output covers multiple viewpoints and both weapons.
shot "shot-overview" 1337 "cam 0 17 26 0 -0.55; shot %s"
shot "shot-eye"      1337 "wait 120; shot %s"
shot "shot-ads-ar"   1337 "+ads; wait 60; shot %s"
shot "shot-ads-sr"   1337 "weapon sr; +ads; wait 90; shot %s"
shot "shot-sunlow"   1337 "sun 8 120; wait 10; shot %s"
shot "shot-home"     1337 "home on; appframe 300; shot %s"
shot "shot-third"    1337 "tap view; wait 60; shot %s"

sort -o "$OUT/manifest.txt" "$OUT/manifest.txt"
if [ "$case_count" -ne 44 ] || ! awk '
  { if (seen[$1]++ || $2 != "rc=0") bad = 1 }
  END { exit (NR != 44 || bad) ? 1 : 0 }
' "$OUT/manifest.txt"; then
  echo "byte-gate: incomplete or failed 44-case manifest" >&2
  case_fail=1
fi
echo "byte-gate: $case_count cases -> $OUT/manifest.txt"
[ "$shot_fail" = 0 ] || exit 3
[ "$case_fail" = 0 ] || exit 1
