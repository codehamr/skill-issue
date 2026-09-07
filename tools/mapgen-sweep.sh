#!/bin/bash
# mapgen-sweep.sh [BIN] [START_SEED] [N] prints the real restart seed chain.
# This diagnostic census is not the geometry release gate. A game failure,
# incomplete row, invalid artifact or dropped decoration makes the run fail.
# Columns: row theme sun_elev solids world_verts world_decor scat_sites plan_ms.
# MOOD="R G S" applies the same three mood latents to each map.
set -eu
[ "$#" -le 3 ] || { echo "usage: mapgen-sweep.sh [BIN] [START_SEED] [N]" >&2; exit 2; }
BIN=${1:-build/game}
SEED=${2:-1337}
N=${3:-200}
[ -x "$BIN" ] || { echo "mapgen-sweep: not executable: $BIN" >&2; exit 2; }
[[ "$SEED" =~ ^[0-9]{1,10}$ ]] && ((10#$SEED <= 4294967295)) || {
  echo "mapgen-sweep: seed must be an unsigned 32-bit integer" >&2; exit 2;
}
[[ "$N" =~ ^[0-9]{1,6}$ ]] && ((10#$N >= 1 && 10#$N <= 100000)) || {
  echo "mapgen-sweep: count must be in 1..100000" >&2; exit 2;
}
N=$((10#$N))
TMPD=$(mktemp -d)
trap 'rm -rf "$TMPD"' EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
SHOT="$TMPD/map.png"
SCRIPT="$TMPD/maps.script"
MOODCMD=""
if [ -n "${MOOD:-}" ]; then
  # Data cannot inject extra harness commands into this fixed fixture.
  if ! [[ "$MOOD" =~ ^[[:space:]]*[01]([.][0-9]+)?[[:space:]]+[01]([.][0-9]+)?[[:space:]]+[01]([.][0-9]+)?[[:space:]]*$ ]] ||
      ! awk -v mood="$MOOD" 'BEGIN { n=split(mood, v, " "); for(i=1;i<=n;i++) if(v[i]+0>1) exit 1 }'; then
    echo "mapgen-sweep: MOOD requires three numbers in 0..1" >&2; exit 2
  fi
  MOODCMD="mood $MOOD; "
fi
printf '%s\n' "$MOODCMD" > "$SCRIPT"
for ((i=0; i<N; i++)); do
  printf 'shot %s; budget; map; scat;\n' "$SHOT" >> "$SCRIPT"
  if ((i + 1 < N)); then printf 'restart; wait 2; %s\n' "$MOODCMD" >> "$SCRIPT"; fi
done
if "$BIN" --seed "$SEED" --config "$TMPD/config.cfg" --script "$SCRIPT" \
    >"$TMPD/game.log" 2>"$TMPD/game.stderr"; then status=0; else status=$?; fi
if [ "$status" -ne 0 ]; then
  tail -n 60 "$TMPD/game.log" >&2
  cat "$TMPD/game.stderr" >&2
  echo "mapgen-sweep: game exit=$status" >&2
  exit "$status"
fi
if [ ! -s "$SHOT" ] || \
    [ "$(od -An -tx1 -N8 "$SHOT" | tr -d ' \n')" != 89504e470d0a1a0a ] || \
    [ "$(tail -c 12 "$SHOT" | od -An -tx1 | tr -d ' \n')" != 0000000049454e44ae426082 ]; then
  echo "mapgen-sweep: missing or invalid final PNG" >&2; exit 1
fi
awk -v seed0="$SEED" -v expected="$N" '
  function fields(    i, kv, key) {
    for (key in field) delete field[key]
    for (i=1; i<=NF; i++) {
      if (split($i, kv, "=") == 2) {
        if (kv[1] in field) bad=1
        field[kv[1]]=kv[2]
      }
    }
  }
  function uint(v) { return v ~ /^[0-9]+$/ }
  function pair(v,    a) { return split(v,a,"/")==2 && uint(a[1]) && uint(a[2]) && a[1]+0<=a[2]+0 }
  function decimal(v) { return v ~ /^[0-9]+([.][0-9]+)?$/ }
  /^budget / {
    if (stage!=0) bad=1
    stage=1; fields()
    if (!uint(field["world_verts"]) || !pair(field["world_decor"]) ||
        !uint(field["world_drops"])) bad=1
    wv=field["world_verts"]; split(field["world_decor"],a,"/"); wd=a[1]
    drops+=field["world_drops"]
  }
  /^map / {
    if (stage!=1) bad=1
    stage=2; fields()
    th=field["theme"]; el=field["sun_elev"]
    if (th !~ /^(grit|wet|sand)$/ || !decimal(el)) bad=1
    ns=0
  }
  /^solid / { if (stage!=2 || $2 != ns) bad=1; ns++ }
  /^scat / {
    if (stage!=2 || ns==0) bad=1
    stage=0; fields()
    if (!pair(field["sites"]) || !decimal(field["plan_ms"])) bad=1
    split(field["sites"],a,"/"); sc=a[1]; pm=field["plan_ms"]
    row++
    printf "%4d %-5s %5.1f %3d %6d %6d %4d %7.3f\n", row,th,el,ns,wv,wd,sc,pm
    if (ns>mxs) { mxs=ns; mxs_r=row }
    if (wv>mxv) { mxv=wv; mxv_r=row }
    if (wd>mxd) { mxd=wd; mxd_r=row }
    if (sc>mxc) { mxc=sc; mxc_r=row }
    if (pm>mxp) { mxp=pm; mxp_r=row }
    tot_wd+=wd; cnt[th]++
  }
  END {
    printf "== %d maps from seed %s ==\n", row,seed0
    for (t in cnt) printf "  theme %-5s %d\n",t,cnt[t]
    printf "  max solids      %6d  (row %d)\n",mxs,mxs_r
    printf "  max world_verts %6d  (row %d)\n",mxv,mxv_r
    printf "  max world_decor %6d  (row %d)  mean %.0f\n",mxd,mxd_r,row ? tot_wd/row : 0
    printf "  max scat sites  %6d  (row %d)\n",mxc,mxc_r
    printf "  max plan_ms     %6.3f (row %d)\n",mxp,mxp_r
    printf "  world_drops total %d (MUST be 0)\n",drops
    if (row!=expected || stage!=0 || bad || drops!=0) {
      printf "mapgen-sweep: invalid census rows=%d expected=%d incomplete=%d malformed=%d drops=%d\n",row,expected,stage,bad,drops > "/dev/stderr"
      exit 1
    }
  }
' "$TMPD/game.log"
