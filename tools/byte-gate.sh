#!/bin/sh
# byte-gate.sh BIN OUTDIR — die Abnahmebatterie fuer einen Refactor-Schritt.
#
# Der Gate ist ein TEXTVERGLEICH, kein Binaervergleich: der mechanische Split
# ist byte-identisch, jede Funktionszerlegung ist es nicht (refactor.md,
# "Befund"). Verglichen wird, was der Spieler und die Proofs sehen.
# Laufzeit ~3-5 min (mapcheck 2000 + 12x tacstat 12000 + netfuzz).
#
# Drei Blindstellen, gegen die die Faelle unten bewusst gebaut sind:
#   - Eine Batterie, die nur 20 Bots hard faehrt, sieht die Divergenz nicht,
#     die 1 Bot / skill normal zeigt (2026-08-23). Beide sind drin.
#   - Ein fester --config-Pfad kontaminiert den naechsten Lauf. Jeder Fall
#     bekommt einen frischen mktemp -u.
#   - sun/mood/scat sind seit 2026-08-25 Map-Zustand; eine Batterie ohne sie
#     ist fuer world/map, world/scatter und world/decor blind.
set -eu
BIN=$1; OUT=$2
[ -x "$BIN" ] || { echo "byte-gate: $BIN ist nicht ausfuehrbar" >&2; exit 2; }
mkdir -p "$OUT"; : > "$OUT/manifest.txt"
shot_fail=0

run() {  # run NAME SEED SCRIPT...
  name=$1; seed=$2; shift 2
  cfg=$(mktemp -u); dir=$(mktemp -d)
  set +e
  "$BIN" --seed "$seed" --config "$cfg" --do "$*" >"$dir/o" 2>"$dir/e"
  rc=$?
  set -e
  # Pfade UND nicht-deterministische Felder normalisieren: mktemp-Namen sind
  # pro Lauf anders, `scat` druckt eine WANDUHR-Zeit (plan_ms), `updinfo` den
  # sha des EIGENEN Binaries (der sich mit jedem Codestand aendert — er wuerde
  # jeden A/B-Vergleich zweier Builds konstruktiv rot machen). Die
  # Positivkontrolle (Task 1, Schritt 4) ist die Autoritaet: was dort zwischen
  # zwei Laeufen desselben Binaries differiert, wird HIER normalisiert.
  sed -e "s#$dir#TMP#g" -e "s#$cfg#CFG#g" \
      -e 's/plan_ms=[0-9.]*/plan_ms=X/' \
      -e '/^sha [0-9a-f]\{64\}$/d' "$dir/o" > "$OUT/$name.txt"
  printf '%s rc=%s %s\n' "$name" "$rc" \
    "$(md5sum < "$OUT/$name.txt" | cut -d' ' -f1)" >> "$OUT/manifest.txt"
  rm -rf "$dir"; rm -f "$cfg"
}

shot() {  # shot NAME SEED SCRIPT... (SCRIPT muss %s fuer den PNG-Pfad enthalten)
  name=$1; seed=$2; shift 2
  cfg=$(mktemp -u); dir=$(mktemp -d)
  # shellcheck disable=SC2059
  scr=$(printf "$*" "$dir/s.png")
  "$BIN" --seed "$seed" --config "$cfg" --do "$scr" >"$dir/o" 2>&1 || true
  # DEVIATION vom Plan-Wortlaut (Fix round 1, Controller-Ruling): eine leere
  # $dir/s.png durfte frueher trotzdem eine wohlgeformte manifest-Zeile
  # erzeugen (md5sum auf eine fehlende Datei bricht `set -e` nicht, weil
  # `cut` als letztes Pipeline-Glied 0 exitet) — zwei Laeufe, die BEIDE
  # denselben Shot verfehlen, diffen dann gleich. Das ist genau die stille
  # Leerausgabe, die der Plan selbst als Gate-Defekt einstuft: MISSING wird
  # jetzt explizit ins Manifest geschrieben und der ganze Lauf am Ende ungueltig gemacht.
  if [ -s "$dir/s.png" ]; then
    printf '%s png %s\n' "$name" "$(md5sum < "$dir/s.png" | cut -d' ' -f1)" \
      >> "$OUT/manifest.txt"
  else
    echo "byte-gate: SHOT LEER: $name" >&2
    printf '%s png MISSING\n' "$name" >> "$OUT/manifest.txt"
    shot_fail=1
  fi
  rm -rf "$dir"; rm -f "$cfg"
}

# --- Sim: beide Enden des Spektrums, mehrere Seeds ------------------------
for s in 1337 7 99 2; do
  run "sim-default-$s"  "$s" "trace 600; pos; wpn; bot; match"
  run "sim-1bot-$s"     "$s" "bots 1; skill normal; fraglimit 1000; wait 240; tacstat 12000; match"
  run "sim-20bot-$s"    "$s" "bots 20; skill hard; fraglimit 1000; wait 240; tacstat 12000; match"
done
run "locomotion" 1337 "botfreeze on; +forward; trace 240; tap jump; wait 90; pos; lean; skel 120; kine 120"
run "ragdoll"    1337 "bots 4; skill hard; fraglimit 1000; wait 600; rag 300; ragx 300; ragsoak 600; flinch"

# --- Figur und Waffe: die Null-Invarianten und die Masse ------------------
run "figcheck"  1337 "figcheck 120; figcheck 120 6; figcheck 120 12; figcheck 120 25"
run "figv"      1337 "figv; figcheck 120; figv"
run "figm"      1337 "figm; puppet on; puppet ads 1; wait 60; figm; puppet crouch 1; wait 60; figm"
run "figbury"   1337 "figbury; figbury head; lean"
run "viewmodel" 1337 "vmcheck; vmsight; vmframe; vmtrig; vmhand"

# --- Welt, Licht, Mood, Scatter, Parity, Budget ---------------------------
run "map"        1337 "map; sun; mood; scat; mapcheck 2000"
run "sun-low"    1337 "sun 8 120; sun; map"
run "mood-force" 1337 "mood 0; map; scat"
run "parity"     1337 "parity"
run "budget"     1337 "bots 20; skill hard; fraglimit 1000; wait 240; shot /dev/null; budget"

# --- Bot- und Pad-Proofs (Exit-Code ist die Messung, nicht die Ziffern) ---
run "bothear"          1337 "bothear"
run "botweapon"        1337 "botweapon"
run "botflank"         1337 "botflank"
run "botmemoryobserve" 1337 "botmemoryobserve"
run "padbackend"       1337 "padbackend"
run "padcurve"         1337 "padcurve; padlook 120"
# botmemory ist auf den Defaults (HARD) bekannt rot — dieselbe Lage wie HEAD.
run "botmemory-normal" 1337 "skill normal; wait 120; botmemory"

# --- Netz (inkl. netleave: Teardown-Zaehler muessen 0 sein) ---------------
run "net" 1337 "netpack 2000; netpredict 600; netlagcomp; netstall; netanim 600; netfill; netloop 600; netdeath; netleave; netfuzz 5000"

# --- UI und HOME (appframe, nicht wait — wait bewegt g_ui_time nicht) -----
run "ui-menu" 1337 "menu; uiframe; uistat; nav down; uiframe; nav ok; uiframe; uistat; cfg"
run "ui-home" 1337 "home on; appframe 240; uistat; appframe 240; uistat"

# --- Audio ----------------------------------------------------------------
run "sfxlog"  1337 "bots 20; skill hard; fraglimit 1000; sfxlog 1200"
run "updinfo" 1337 "updinfo"

# --- Pixel: der Renderer, aus mehreren Blickrichtungen, beide Waffen ------
shot "shot-overview" 1337 "cam 0 17 26 0 -0.55; shot %s"
shot "shot-eye"      1337 "wait 120; shot %s"
shot "shot-ads-ar"   1337 "+ads; wait 60; shot %s"
shot "shot-ads-sr"   1337 "weapon sr; +ads; wait 90; shot %s"
shot "shot-sunlow"   1337 "sun 8 120; wait 10; shot %s"
shot "shot-home"     1337 "home on; appframe 300; shot %s"
shot "shot-third"    1337 "tap view; wait 60; shot %s"

sort -o "$OUT/manifest.txt" "$OUT/manifest.txt"
echo "byte-gate: $(wc -l < "$OUT/manifest.txt") Faelle -> $OUT/manifest.txt"
if [ "$shot_fail" -ne 0 ]; then
  echo "byte-gate: N.B. fehlende Shots — Lauf ungueltig" >&2
  exit 3
fi
