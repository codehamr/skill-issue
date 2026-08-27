#!/bin/sh
# split-check.sh — die Struktur-Invarianten der einen Translation Unit.
#
# 1. Jede .inc unter code/ wird von code/game.c GENAU EINMAL eingebunden.
# 2. Es gibt keine .inc, die niemand einbindet (toter Code, der still veraltet).
# 3. Jede .inc ist fuer sich #if/#endif-balanciert — sonst kann ein Editor,
#    ein Agent oder ein Reviewer die Datei nicht allein lesen.
# 4. Keine .inc bindet eine andere .inc ein: die Reihenfolge steht an EINER
#    Stelle, in game.c, und nirgends sonst.
# 5. Jede .inc liegt genau eine Ebene unter code/ (code/<dir>/<name>.inc) —
#    das ist die Ebene, die $(wildcard code/*/*.inc) im Makefile sieht.
set -eu
cd "$(dirname "$0")/.."
fail=0
inc_list=$(find code -name '*.inc' | sed 's#^code/##' | sort)

for f in $inc_list; do
  n=$(grep -c "^#include \"$f\"$" code/game.c || true)
  [ "$n" = 1 ] || { echo "FAIL: $f ist ${n}x eingebunden (erwartet 1)"; fail=1; }
done

# Hardening (fix round 1, Finding 3): $(wildcard code/*/*.inc) in the Makefile
# only ever sees ONE level under code/. A bare code/x.inc or a nested
# code/world/sub/x.inc would pass every other check here yet be invisible to
# make's prerequisites — the exact stale-rebuild defect Schritt 1 reproduced,
# reintroduced one directory deeper (or shallower) where nobody is looking.
for f in $inc_list; do
  slashes=$(printf '%s' "$f" | tr -cd '/' | wc -c)
  [ "$slashes" = 1 ] || { echo "FAIL: code/$f liegt nicht in code/<dir>/ — \$(wildcard code/*/*.inc) sieht es nicht"; fail=1; }
done

tmp_inc=$(mktemp); tmp_pre=$(mktemp)   # feste /tmp-Namen kollidieren mit parallelen Sessions
# Hardening (fix round 1, Finding 2): the brief's extraction pattern
# ('[a-z_]+/[a-z_]+\.inc') is stricter than the per-fragment exact-count check
# above, which accepts ANY filename ending in .inc. A fragment name with a
# digit (or any character outside [a-z_]) would count as included above but
# fail extraction here, misreporting a real, correctly-included fragment as
# an orphan ("weichen ab"). Both checks must share one notion of an include
# line, so this pattern is only as strict as the count check: any quoted
# "<something>.inc" on its own #include line.
grep -oE '^#include "[^"]+\.inc"$' code/game.c |
  sed -e 's/^#include "//' -e 's/"$//' | sort > "$tmp_inc"
printf '%s\n' "$inc_list" > "$tmp_pre"
if ! diff -q "$tmp_pre" "$tmp_inc" >/dev/null; then
  echo "FAIL: vorhandene und eingebundene Fragmente weichen ab:"
  diff "$tmp_pre" "$tmp_inc" || true
  fail=1
fi
rm -f "$tmp_inc" "$tmp_pre"

for f in $inc_list; do
  o=$(grep -cE '^[ \t]*#[ \t]*(if|ifdef|ifndef)\b' "code/$f" || true)
  c=$(grep -cE '^[ \t]*#[ \t]*endif\b' "code/$f" || true)
  [ "$o" = "$c" ] || { echo "FAIL: code/$f unbalanciert (#if=$o #endif=$c)"; fail=1; }
  # Hardening (fix round 1, Finding 1): the brief's self-include guard
  # ('^#include "[a-z_]+/') only catches a path-qualified include and misses
  # a same-directory include ("config.inc") or a "../"-relative one — both
  # still let a fragment smuggle in the ordering that is supposed to live in
  # exactly one place, game.c. Match any quoted #include of a .inc file,
  # wherever it points.
  if grep -qE '^[ \t]*#[ \t]*include[ \t]+"[^"]*\.inc"' "code/$f"; then
    echo "FAIL: code/$f bindet selbst ein Fragment ein — die Reihenfolge gehoert in game.c"
    fail=1
  fi
done

[ "$fail" = 0 ] && echo "split-check: ok ($(printf '%s\n' "$inc_list" | wc -l) Fragmente)"
exit $fail
