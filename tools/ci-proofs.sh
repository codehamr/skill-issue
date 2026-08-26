#!/bin/sh
# The proof gate, self-contained. A build proves itself BEFORE it is tagged —
# that is the guard against a bad release, since there is no rollback history.
# Deliberately lean: the strong, arch- and config-independent invariants only.
# No recorded md5 baseline, no per-arch file, no qemu record — those caught
# geometry-tuning drift a developer already watches during that work, not what
# a release needs to gate.
#
#   tools/ci-proofs.sh <binary>
#
# Asserted (each must hold on every architecture):
#   figcheck: surface closed + wound + no coplanar overlap (open/flip/dup/
#             zfight/degen = 0) at four profile tiers, and `near` AND `cross` at their
#             documented fresh-config baselines (the WANT_NEAR / WANT_CROSS
#             lines below are the single authority — the config is part of the
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
CROSS=""
say() { printf '%s\n' "$*"; }
run() { rm -f "$CFG"; ( cd "$ROOT" && $RUN "$BIN" --seed 1337 --config "$CFG" --do "$1" ) 2>&1; }
field() { printf '%s' "$1" | sed -n "s/.* $2=\([0-9]*\).*/\1/p" | head -1; }

# figcheck at the four DIST tiers — invariants AND the near quadruple
NEAR=""
for d in 0 3 12 30; do
  line="$(run "figcheck 60 $d" | grep '^figcheck' | head -1)"
  for k in open flip dup zfight degen; do
    v="$(field "$line" "$k")"
    [ "${v:-x}" = 0 ] || { say "GATE figcheck dist=$d $k=$v (want 0)"; fail=1; }
  done
  n="$(field "$line" near)"
  NEAR="${NEAR:+$NEAR }$n"
  c="$(field "$line" cross)"
  CROSS="${CROSS:+$CROSS }$c"
done
# `near` (two surfaces parallel within 1 mm) is config-conditioned; fresh
# defaults give the WANT_NEAR quadruple below. Baselined rather than gated at
# 0 because two curved bodies that touch are tangent somewhere by
# construction. A change here is a geometry regression OR a config that leaked
# in — INVESTIGATE (`figv` names the pairs; diff the pair histogram against a
# HEAD build), never just bump. The number lives ONCE, here, the only reader.
# The full re-baseline record 2026-08-10..2026-08-25 is in this file's own git
# history (`git log -p -- tools/ci-proofs.sh`); what stays here is the
# justification of the CURRENT quadruple:
# - The 2026-08-25 weapon model pass roughly doubled it: both rifles went from
#   ~30 parts to ~72, so the number of neighbouring surfaces that can be
#   tangent roughly doubled with it. Investigated, not bumped — six genuinely
#   coplanar pairs were found and FIXED on the way rather than absorbed, each
#   written up at its own site in gun_build. What remains is the class the
#   tier exists for: the gap histogram is spread 0.1-1.0 mm exactly as HEAD's
#   was, not piled at zero.
# - TWELVE of the quadruple are the AR sight's coating annulus against its
#   window frame, and they are not going away: the frame's wall is 1.05 mm of
#   real thickness (1.6 before the micro-dot pass), so a lens ring buried in
#   it is inside a millimetre of BOTH of its faces by construction.
# - 2026-08-25 joint/arm pass: -1 at DIST 30 (thigh and shin now narrow into
#   the knee), verified by pair-histogram diff against HEAD: exactly
#   `thigh/shin 2 -> 1`, nothing else moved on any tier.
# - 2026-08-25 default retune: -1 at DIST 12 and 30 (the dev-tuned
#   config.cfg became MV_DEF/WP_DEFAULTS). No geometry changed: the SAME binary
#   under the OLD values reproduces the old quadruple exactly, and the mover is
#   mv_accel_ms 100 -> 200 alone (the 60-tick sweep spends longer in the
#   accelerating gait phases, so the worst-scoring tick lands on a different
#   pose). figv pair families are identical old vs new.
# - Last move: 80/80/79/63 -> 76/76/75/62 (2026-08-25 micro red dot — the AR
#   sight's hood walls, hood bridge, chassis and both posts are DELETED and the
#   frame moved forward onto a slot-centred clamp). figv family diff against a
#   HEAD build: only gun/gun, gun/hand and gun/farm move, every body family
#   identical — the retired pairs are the hood/chassis stack's own.
WANT_NEAR="76 76 75 62"
[ "$NEAR" = "$WANT_NEAR" ] || { say "GATE near '$NEAR' != baseline '$WANT_NEAR'"; fail=1; }

# `cross` — surfaces passing THROUGH each other at 1.8-15 degrees, the tier
# the coplanar test is blind to (it measures the far end of a band, so a
# barrel grazing out through a fore-end's skin reads as 2.4 mm and falls out
# of the 1 mm tier). Baselined like `near` and for the same reason: a hand on
# a grip and a goggle on a skull each contribute a few correct pairs. The
# DELTA is the signal — investigate a move, never just bump it. Unlike `near`
# this is a per-tier MAXIMUM over the 60 ticks rather than the value at the
# worst-scoring tick, deliberately decoupled so a new tier cannot silently
# re-baseline an old one. Full re-baseline record: this file's git history.
# The CURRENT quadruple:
# - What remains is structural and named: the grip family (gun/gun, gun/hand,
#   hand/hand, gun/trigfinger), the joints (uarm/elbow, neck/skull,
#   skull/goggle), thigh/shin inside the knee ball, and the designed burials
#   of the carrier gear (shingle, radio, dump pouch, kneepad, boot shaft)
#   grazing their hosts at the k=6 tier — sleeve-on-sleeve at the same value,
#   not visible seams.
# - Known and NOT fixed: the AR's gas tube stands 0.6-1.2 mm out through the
#   top of its own handguard for 130 mm.
# - Last moves (all 2026-08-25): the joint/arm pass took 90/90/81/62 ->
#   84/84/70/66 (deltoid ball and armour pad deleted — sho_ball/deltoid alone
#   was ~7 pairs per run at the close tiers; the kneepad plate and taller
#   boot shaft graze their hosts at k=6); elbow blunting -2/-2/-1/0 (smaller,
#   flatter ball stops the ellipsoid's tip grazing the arm tubes); masculine
#   jaw +1/+1/+5/0 (chin 8 mm forward, jaw 9 mm wider — the goggle-on-skull
#   and strap-on-torso families regain a few pairs at the eight-flat tiers;
#   skull/goggle at DIST 12 is 300 on both HEAD and this build, so what
#   earlier passes retired stays retired).
# - 2026-08-25 default retune (see the `near` note): 83/83/74/66 ->
#   97/97/82/59. Entirely mv_accel_ms 100 -> 200 (single-param isolation:
#   reverting accel alone gives 85 at DIST 0, reverting all movement gives the
#   old quadruple byte-exact on the same binary; weapon values move nothing).
#   cross is a per-tier MAX over the sweep, so more ticks spent in the
#   accelerating gait raise the close tiers (limbs graze the weapon/torso in
#   the ramp-up poses) and drop the k=6 tier. figv pair families identical.
# - Last move: 97/97/82/59 -> 94/94/79/56 (2026-08-25 micro red dot, see the
#   `near` note above): the deleted hood/chassis stack was what the carry pose
#   grazed the support forearm against. figv family diff vs HEAD: gun/gun
#   1440 -> 1260, gun/hand 152 -> 146, gun/farm 38 -> 28 over the 60-tick
#   aggregate, nothing else on any tier.
# zfight/open/flip/dup/degen are 0 on every tier through all of it, and
# `near` moved only where its own note above says.
WANT_CROSS="94 94 79 56"
[ "${CROSS:-}" = "$WANT_CROSS" ] || { say "GATE cross '${CROSS:-}' != baseline '$WANT_CROSS'"; fail=1; }

# parity: no MISMATCH in the shared-code rows
run "parity" | grep -q MISMATCH && { say "GATE parity MISMATCH"; fail=1; } || true

# budget at 20 bots hard: nothing dropped. Render a frame first (budget only
# accumulates during scene build) — into a TEMPORARY path, not screenshots/:
# that directory is gitignored, so in a CI checkout the shot failed to open
# (swallowed by this block's own grep). render_frame still ran to completion
# before write_png, so drops/ev_drops were real — but the shot proved nothing
# and depended on an untracked directory. mktemp writes anywhere.
CISHOT="$(mktemp -u)_ci.png"
bl="$(run "bots 20; skill hard; wait 1200; shot $CISHOT; wait 300; shot $CISHOT; budget" | grep '^budget' | head -1)"
rm -f "$CISHOT"
for k in drops ev_drops; do
  v="$(field "$bl" "$k")"
  [ "${v:-x}" = 0 ] || { say "GATE budget $k=$v (want 0)"; fail=1; }
done

# glibc floor — printed, not gated (a rise silently excludes distros)
if command -v objdump >/dev/null 2>&1; then
  floor="$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)"
  say "info glibc floor: ${floor:-none} (2.38 = Ubuntu 23.10+ / Debian 13+ / SteamOS 3.7+)"
fi

[ "$fail" = 0 ] && say "ci-proofs: OK ($ELF, near=$NEAR, cross=$CROSS)" || say "ci-proofs: FAIL"
exit $fail
