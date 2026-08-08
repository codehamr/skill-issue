#!/bin/sh
# The proof gate, self-contained (design §14.2). A build proves itself BEFORE
# it is tagged — that is the guard against a bad release, since there is no
# rollback history. Deliberately lean: the strong, arch- and config-independent
# invariants only. No recorded md5 baseline, no per-arch file, no qemu record —
# those caught geometry-tuning drift a developer already watches during that
# work, not what a release needs to gate.
#
#   tools/ci-proofs.sh <binary>
#
# Asserted (each must hold on every architecture):
#   figcheck: surface closed + wound + no coplanar overlap (open/flip/zfight/
#             degen = 0) at four profile tiers, and `near` at its documented
#             fresh-config baseline (14/14/26/21 — the config is part of the
#             number, so the script writes fresh defaults itself)
#   parity  : no MISMATCH in the shared-code rows
#   budget  : drops = 0 and ev_drops = 0 at 20 bots hard (nothing silently
#             dropped a triangle or an event)
# Also prints the glibc floor for the log — a rise excludes whole distros, so
# it is worth seeing even though it is not gated here.
set -eu

BIN="${1:?usage: ci-proofs.sh <binary>}"
BIN="$(realpath "$BIN")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$(mktemp -u)"                       # fresh defaults: part of the near number
RUN=""
# A foreign ELF (a cross build checked on its own arch's runner is native, but
# a local x86_64 check on this aarch64 box needs qemu).
ELF="$(od -An -tx1 -j18 -N2 "$BIN" | tr -d ' ')"
case "$ELF" in
  3e00) [ "$(uname -m)" = x86_64 ]  || RUN="qemu-x86_64-static" ;;
  b700) [ "$(uname -m)" = aarch64 ] || RUN="qemu-aarch64-static" ;;
esac

fail=0
say() { printf '%s\n' "$*"; }
run() { rm -f "$CFG"; ( cd "$ROOT" && $RUN "$BIN" --seed 1337 --config "$CFG" --do "$1" ) 2>&1; }
field() { printf '%s' "$1" | sed -n "s/.* $2=\([0-9]*\).*/\1/p" | head -1; }

# figcheck at the four DIST tiers — invariants AND the near quadruple
NEAR=""
i=0
for d in 0 3 12 30; do
  line="$(run "figcheck 60 $d" | grep '^figcheck' | head -1)"
  for k in open flip zfight degen; do
    v="$(field "$line" "$k")"
    [ "${v:-x}" = 0 ] || { say "GATE figcheck dist=$d $k=$v (want 0)"; fail=1; }
  done
  n="$(field "$line" near)"
  NEAR="${NEAR:+$NEAR }$n"
  i=$((i + 1))
done
# near is config-conditioned; fresh defaults give 14/14/26/21 (§10.1). A change
# here is a geometry regression OR a config that leaked in — investigate, don't
# just bump. It lives ONCE, here, the only reader.
WANT_NEAR="14 14 26 21"
[ "$NEAR" = "$WANT_NEAR" ] || { say "GATE near '$NEAR' != baseline '$WANT_NEAR'"; fail=1; }

# parity: no MISMATCH in the shared-code rows
run "parity" | grep -q MISMATCH && { say "GATE parity MISMATCH"; fail=1; } || true

# budget at 20 bots hard: nothing dropped. Render a frame first (budget only
# accumulates during scene build).
bl="$(run "bots 20; skill hard; wait 1200; shot screenshots/_ci.png; wait 300; shot screenshots/_ci.png; budget" | grep '^budget' | head -1)"
rm -f "$ROOT/screenshots/_ci.png"
for k in drops ev_drops; do
  v="$(field "$bl" "$k")"
  [ "${v:-x}" = 0 ] || { say "GATE budget $k=$v (want 0)"; fail=1; }
done

# glibc floor — printed, not gated (a rise silently excludes distros; §13.4)
if command -v objdump >/dev/null 2>&1; then
  floor="$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)"
  say "info glibc floor: ${floor:-none} (2.38 = Ubuntu 23.10+ / Debian 13+ / SteamOS 3.7+)"
fi

[ "$fail" = 0 ] && say "ci-proofs: OK ($ELF, near=$NEAR)" || say "ci-proofs: FAIL"
exit $fail
