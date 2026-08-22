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
#             degen = 0) at four profile tiers, and `near` AND `cross` at their
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
i=0
for d in 0 3 12 30; do
  line="$(run "figcheck 60 $d" | grep '^figcheck' | head -1)"
  for k in open flip zfight degen; do
    v="$(field "$line" "$k")"
    [ "${v:-x}" = 0 ] || { say "GATE figcheck dist=$d $k=$v (want 0)"; fail=1; }
  done
  n="$(field "$line" near)"
  NEAR="${NEAR:+$NEAR }$n"
  c="$(field "$line" cross)"
  CROSS="${CROSS:+$CROSS }$c"
  i=$((i + 1))
done
# near is config-conditioned; fresh defaults give the WANT_NEAR quadruple
# below (§10.1). A change here is a geometry regression OR a config that
# leaked in — investigate, don't just bump. It lives ONCE, here, the only
# reader — the prose deliberately repeats no number.
# Re-baselined 2026-08-10 (evening) after the reference remodel of BOTH
# weapons (HK416-class AR: quad-rail block, slider stock, continuous rail;
# AWM-class SR: thumbhole chassis, heavy barrel, ported brake). All new
# grazes are advisory-tier burials by construction; zfight is 0 on every
# tier and vmcheck.
# +1 near at DIST 0/3 since 2026-08-11: the crossed low-ready carry (yin
# 0.50 rad) re-orients the idle figure's rifle and one known gun-internal
# rail-top pair (0.39 mm, n=+Y, advisory burial) lands under the 1 mm tier.
# Re-baselined 2026-08-19, -1 at DIST 12 and 30, when the live-tuned play
# session's values became the compiled defaults (movement, both weapons, pad).
# NOT a geometry change, and proven so rather than assumed: the HEAD binary fed
# the new values through --config prints this same quadruple, and the new binary
# on fresh defaults prints it too — identical on all four tiers, tris and
# worst_t included. `figcheck 60` runs 60 ticks first, so the movement defaults
# (run 12 -> 7 m/s, accel/stop 200 -> 100 ms) put the figure in a different pose
# at the sample tick; two advisory grazes in the low-detail tiers separate. Every
# hard invariant stayed 0 on every tier through the change.
# Re-baselined 2026-08-20 with the CARRY POSE fix. The low-ready used to rotate
# the weapon about the GRIP, which swung the buttstock up and rearward into the
# ribs: measured with the new `figbury` proof, the AR's butt pad sat 20 mm inside
# the trunk and the deepest buried vertex was at chest height, i.e. the stock was
# not visible from outside at all. It now pivots about the shoulder pocket and is
# carried 130 mm further outboard, which takes the butt 10-26 mm CLEAR of the
# body. `near` FELL by 2 at the two close tiers because a weapon that is no
# longer inside the chest stops grazing it; the two far tiers are unchanged.
# Re-baselined 2026-08-20 (third pass) with the REALISTIC SHOULDERED STANCE.
# The buttstock used to sit 62 mm BEHIND the spine — inside the ribcage — because
# the shoulder line was held square to the target, and with square shoulders the
# arithmetic has no solution: the AR's fore-end sits 0.61 m ahead of its butt and
# the figure's arm is 0.58, so a pocketed stock puts the support hand somewhere
# no arm reaches. A real shooter turns. Blading the shoulder line 30 degrees, a
# third-person-only support station (GH_FORE3), and GUN_GIRTH_3P 1.22 -> 1.05
# together put the butt 127 mm IN FRONT of the spine with 11.7 mm of bite into
# the deltoid — a stock resting in the pocket — and 0.7% of the weapon inside the
# trunk against 7.2 before. `near` rose 7 and 4 at the close tiers because a
# weapon carried CLEAR of the torso lies along the forearm instead, which is a
# graze where a burial was not. zfight is 0 on every tier through all of it.
# -2 at DIST 30 with the SLIDE pose, below.
# +1 at DIST 30 only, with the MODEL pass below. The thigh is 20 mm narrower
# across, so a trouser and a boot cuff that used to pass through each other now
# graze instead — a crossing became a near-parallel pair, which is the trade
# this whole pass is about and the direction it should go.
WANT_NEAR="40 40 46 36"
[ "$NEAR" = "$WANT_NEAR" ] || { say "GATE near '$NEAR' != baseline '$WANT_NEAR'"; fail=1; }

# `cross` — surfaces passing THROUGH each other at 1.8-15 degrees, the tier the
# coplanar test is blind to (it measures the far end of a band, so a barrel
# grazing out through a fore-end's skin reads as 2.4 mm and falls out of the
# 1 mm tier). Baselined, not gated at 0, and the reason is geometric: two curved
# bodies that touch are tangent somewhere by construction, so a hand on a grip
# and a goggle on a skull each contribute a few correct pairs. The DELTA is the
# signal — investigate a move, never just bump it, exactly as for `near` above.
# Unlike `near` this is a per-tier MAXIMUM over the 60 ticks rather than the
# value at the worst-scoring tick, so the two numbers can move independently.
# Baselined 2026-08-19 with the tier itself, on a tree whose sniper fore-end no
# longer grazes its own barrel (that fix alone was vmcheck cross 105 -> 95) and
# whose sniper carries no bipod legs. Known and NOT fixed: the AR's gas tube
# stands 0.6-1.2 mm out through the top of its own handguard for 130 mm.
# +5 at DIST 12 with the same change, and known: the burial that used to be the
# stock inside the chest is now the magazine's floor plate against the strong-side
# thigh, which is a shallow crossing rather than a coplanar graze and therefore
# lands in THIS tier instead of `near`. It is the correct trade — a magazine
# touching a leg is what a carried rifle does, a stock inside a ribcage is not —
# and `figbury` is the proof that says so in millimetres.
# Second pass on the same pose, +8 at DIST 0/3, +6 at 12, +6 at 30: the carry now
# rides 165 mm outboard and 30 mm higher (figbury: 14.6% of the AR buried in the
# trunk -> 5.7%, deepest 95 mm -> 46, butt pad clear of every part of the body)
# and the support hand sits at a station the arm can actually reach (figm elbow:
# 162 degrees at 104% of the arm -> 135 at 93%). A weapon standing CLEAR of the
# torso is a weapon lying against the forearm and the thigh instead, at exactly
# the shallow angles this tier counts — the pairs did not appear, they MOVED out
# of the body and into the limbs. `near` did not follow (33/33/42/33 through
# both passes) and `zfight` is 0 on every tier, which is the pair of facts that
# says nothing started fighting for a pixel.
# Third pass, and this tier FELL hard (153 -> 109, 108 -> 94, 95 -> 76). Three
# causes, all of them removals: the face lost its nose chain (a small chain
# following a big ellipse grazes it everywhere) and its goggle straps, the AR
# magazine went from 160 mm and five nodes to 110 and three, and the whole
# third-person weapon is 14% smaller. Fewer surfaces lying along each other at
# shallow angles is exactly what this tier counts.
# +1 at DIST 0/3 and -2 at 30 with the LONGER BUTTSTOCK (20 mm) and the ADS
# pose that stops lifting it off the shoulder. `near` did not move at all
# (40 40 46 37 through both), which is the pair of facts that matters: more
# stock lying along a shoulder is a shallow crossing, and nothing started
# fighting for a pixel.
# -11 at DIST 0/3 and +5 at 30 with the SLIDE POSE. A slide is a one-handed
# carry and the weapon used to keep the low-ready angle through it, so it lay
# along a reclined belly and vanished — filmed from the boot screen's tracking
# camera the figure was a crouched mass with no rifle in it anywhere. It now
# rotates 66 degrees muzzle-up about its own right and the anchor rises with it
# (figbury during a slide: 0.5% of the weapon inside the trunk -> 0.0%, butt pad
# 13.5 mm in -> 16 mm clear). A weapon that is no longer lying along a torso is
# a long list of shallow crossings that stopped happening. `tris` moves 16 with
# it, which is fig_chain deciding one ring instead of two at a joint the new
# pose straightens; zfight is 0 on every tier.
# -2 at DIST 0/3 and +1 at 30 with the SLIDE HEAD. A slide is crouched by
# definition, so the crouch drop and the slide tuck used to stack and the head
# sank into its own shoulders with no neck left — from the boot screen's
# tracking camera, a man whose head had been pushed into his chest. The crouch
# drop is taken back 80% under a slide and the tuck cut from 40 mm to 16; a head
# that is no longer inside a collar is two shallow crossings that stopped.
# -5 -5 -1 -6 with the CROUCH/CARRY pass, and every one of the three causes is
# a surface that stopped lying along another one. (1) Crouched AND shouldered,
# the head used to sit in its own collar: nothing in the hold lowers the stock
# in a crouch, so the 75 mm crouch tuck stacked on the 26 mm cheek weld and put
# the skull 75 mm BELOW the stock it was supposedly welded to. `figbury head`
# — added with this change, same ray-parity method, skull against trunk — read
# 13.1% of the head inside the trunk with the jaw 20 mm under the collar line,
# against 0.0% standing. The tuck is now taken back 85% under ADS: 0.0% and the
# jaw 25 mm clear, with the head at 1083 mm, still inside the 1180 mm crouched
# head hitbox. (2) The low ready is now held 65 mm STRAIGHT OUT of the chest
# along the trunk's forward. The old hold only pushed the weapon OUTBOARD, which
# moves it across the plate carrier and never off it — survivable for the AR's
# flat receiver, not for a sniper whose optic tower stands 90 mm over the bore
# and gets tipped inboard by the carry rotation. `figbury`: sniper 12.9% inside
# the trunk (deepest 40 mm at chest height, i.e. the scope) -> 0.1%, AR 4.8% ->
# 0.4%, with ready and ADS untouched because the term is scaled by the carry
# blend. (3) The knee ball's middle axis was 0.076 against tube ends of 0.072 —
# 4 mm, where the ball's own comment sets the rule at >= 6. Standing it never
# showed; folded, the shin leaves the ball at a right angle and its skin runs
# alongside the ball's for a whole ring at 4 mm, which the arris chamfer then
# eats into. 0.084 puts every axis at 12-13 mm.
# `near` did not move at all (40 40 46 35 through all of it) and `zfight` is 0
# on every tier, which is the pair of facts that says nothing started fighting
# for a pixel — these pairs did not appear, they STOPPED.
# -28 -28 -31 -34 with the MODEL pass. `figv` was used as the instrument
# throughout — it names the offending PAIR, which turns "the model has z-races"
# into a list — and every one of these is a join that was fixed rather than a
# threshold that was moved. The four biggest, in the order they cost:
#   outsole/boot, 10 pairs -> 0. Both chains put every ring's BOTTOM on their
#     own plane, so the upper's floor is a flat cap and the sole's top is a flat
#     cap, and at UPPR_H 0.021 those two ran 0.2-1.0 mm apart and PARALLEL
#     across the whole middle of the foot. 0.031 buries the leather 8-11 mm into
#     the slab.
#   strap/neck, 9 -> 0. The carrier's shoulder straps ran through the collar for
#     their whole length; they pass over the trapezius now. Half of it came back
#     as strap/deltoid and strap/sho_ball, and that is the correct trade: the
#     gap between collar and deltoid is narrow enough that a harness has to
#     cross one of them, and a strap lying on a shoulder cap is what a strap
#     does.
#   helm_shell x {itself, chinstrap, skull, nvg, trap}, 12 -> 0. The brim flared
#     31 mm of radius in 25 mm of run — 51 degrees — and fig_chain's bevel
#     folded it back through the ring behind it, so the shell intersected
#     ITSELF for a 25 mm seam on the brow line. The NVG shroud is gone with it.
#   thigh/thigh, 0 -> 3 -> 0. hip_span is 190 mm, so the first slimming pass
#     left each thigh reaching 7 mm past the midline; they always had
#     intersected, but a 30 mm overlap crosses steeply and a 14 mm one grazes.
#     Narrowing ACROSS (90 mm) rather than in depth (108) leaves a 5 mm gap.
# What is LEFT is structural and named: the grip (gun/gun, hand/hand, gun/hand,
# gun/trigfinger), the joints (uarm/elbow, torso/sho_ball, neck/skull,
# skull/goggle) and thigh/shin inside the knee ball. zfight is 0 on every tier
# through all of it, in four poses.
# -4 at DIST 12 and +7 at DIST 30 with the WELD/ARM/FINGER pass, and the two
# move in opposite directions because two different things happened. The
# trigger wrist moved 25 mm back and 10 mm up, which is the fix for a defect
# `vmtrig` had been printing all along and nobody had read: `3p ar live` was
# wrist=25.3 clamp=1 on EVERY frame, i.e. the wrist sat inside fig_hand's own
# 39.2 mm floor and the clamp answered by sliding the grip CYLINDER out from
# under the rifle — so the palm and all five digits were built around a cylinder
# the weapon does not draw. 56.1 mm and clamp=0 now. And every fingertip gained
# a DOME ring (a flat 12 x 10 mm cap on the end of each finger is what a
# first-person hand actually shows), which is +128 tris on the figure and a few
# more grazes at the coarse tier where the digits merge.
# +12 +12 +11 +2 with the ELASTIC JOINTS, and this one is a deliberate trade
# rather than a repair. Every flexing joint was a near-round BALL — the shoulder
# 132 x 124 x 136 mm at k=16, a smooth sphere on an otherwise 8-faceted figure,
# which is why it read as an action-figure ball joint (the file had twice tried
# to fix that with COLOUR; colour cannot fix a shape). `fig_joint` replaces all
# three with one mass built from the two BONE DIRECTIONS: its long axis is their
# bisector, so a straight joint is an ellipsoid buried inside its own limb and
# shows nothing; it shortens 30% along the limb as the joint folds; and it
# swells on the OUTSIDE of the bend, centre and radius together, which is where
# an elbow point, a kneecap and a deltoid actually are.
# The cost is geometric and unavoidable: a ball crosses its limb steeply and
# briefly, an elongated blend runs ALONGSIDE it and grazes — which is exactly
# what this tier counts. Every new pair is sleeve-on-sleeve or pad-on-sleeve,
# i.e. the same material at the same value, where the pairs this session
# REMOVED (strap through a collar, a sole against a boot, a shell through
# itself) were all visible seams between different surfaces. `zfight` is 0 on
# every tier in four poses, `near` moved not at all, and `tris` FELL by 192
# because the shoulder no longer needs 16 facets to hide being a sphere.
# The deltoid pad was re-spaced with it (-0.058/+0.034 against -0.035/+0.015):
# authored against a sphere, it emerged through a long shallow stretch of the
# new mass instead of through a decisive crossing.
# +3 at DIST 12 and -3 at 30 with the BUFFER TUBE onto the receiver's axis.
# The AR's whole stock group sat 7 mm under the box it exits (tube axis 0.014
# against the upper receiver's 0.021 and the bore's 0.0225), so 8.9 mm of bare
# receiver face stood above the tube while the tube hung 5.1 mm out under it —
# from the first-person camera, which is above the weapon and on its left, a
# stock bolted on off-centre. The group moves as ONE rigid +7 mm, so nothing
# inside it was re-authored and `gun/gun` is unchanged at 480 pairs over the
# sweep: the model gained no internal defect. `figv` names every pair that DID
# move and they are all one contact class — gun/hand +8, gun/farm -7,
# carrier/gun +1, net +2 over 60 ticks — and they are all in the SLIDE, where
# bot 0 is at these ticks (`bot` reads st=slide): a slide is a one-handed carry
# with the weapon 66 degrees muzzle-up beside a crouched body, so the buttstock
# is the part of the rifle nearest the palm, and moving it 7 mm redistributes
# grazes between the hand and the forearm at 11-15 degrees and 3.4-4.5 mm of
# seam. `near` did not move at all (40 40 46 36), `zfight` is 0 on every tier,
# vmcheck is byte-identical (tris 6472, near 34, cross 93), and so are vmtrig,
# vmsight, vmhand, figcheck at all four tiers under `figcheck 120`, parity and
# all four bot proofs — the only two entries in the whole battery that moved are
# `figbury ar` and `figm gun`, which are the two that measure the thing that
# changed.
# -4 at DIST 12 and -2 at 30 with the SLIDING ADS HEAD, and this tier falling is
# the whole point of the change rather than a side effect. The crouch tuck was
# already taken back under a slide and the ADS cheek weld was not, so a man who
# slid with his weapon up took the weld's 90 mm drop on top of a trunk the slide
# had already reclined — and a reclined spine puts the skull ~50 mm lower over
# the chest for free, because both the neck's 0.10 and the skull's 0.16 are
# measured on an UPRIGHT spine. `figbury head` on a sliding puppet with the
# weapon up read **63.0% of the skull inside the trunk with the jaw 111 mm
# INSIDE it**, against 0.9% standing and 0.2% crouched: a helmet resting on the
# plate carrier with no neck anywhere, which is exactly the read the crouch pass
# above fixed once and never covered this path. The weld's DROP is now taken
# back 85% under a slide (its forward and across components stay — the forward
# one is what keeps the jaw out of the collar) and the skull's offset along its
# own up axis grows 50 mm with the slide blend, giving back what the recline
# took. 1.5% and the jaw 7 mm in. A skull that is no longer inside a collar and
# a carrier is a short list of shallow crossings that stopped happening.
# `near` did not move (40 40 46 36) and `zfight` is 0 on every tier, which is
# the pair of facts that says nothing started fighting for a pixel; both terms
# are written so that at slide_s == 0 they are bit-identical to what they
# replaced, and every non-sliding clip in the trailer battery re-rendered to the
# same md5.
# +3 at DIST 30 ONLY with the third and last part of the same fix, and this one
# is the part that actually closed it. Lifting the skull got `figbury head` to
# 1.5% with the jaw 7 mm in — nearly clear by the numbers — and the PICTURE was
# still a helmet fused into the backpack with no neck anywhere. Ray parity
# cannot see why: the defect was never the skull being inside the trunk, it was
# the skull being BEHIND THE SHOULDER LINE, where the only thing under it is the
# pack. The recline is applied to `spine` and both `chest` (0.40) and `neck`
# (0.10) are built along it, so a slide carries the head's anchor half a
# spine-length backward as well as down; a man sliding feet-first does the
# opposite, the trunk goes back and the neck flexes FORWARD. The head now gives
# back 75% of that carry along the trunk's own forward, and `figbury head` in a
# sliding aim reads **0.0% with no contact at all**, from 63.0% and a jaw 111 mm
# under the collar. The +3 is at the 6-gon tier only and it is the expected
# trade: a skull over its own chest grazes a collar at a shallow angle, where a
# skull over the pack was simply buried in it. `near` did not move at any tier
# (40 40 46 36), `zfight` is 0 at all four, and `tris` is unchanged (7196 / 7196
# / 5180 / 4076).
# +5 at DIST 12 with the SLIDE NECK TUNE (2026-08-22). The previous pass gave
# back the FULL 50 mm of slide head drop plus 75% of the forward carry, and a
# close side camera on a live slide photographed a giraffe: a full head-length
# of bare neck cylinder between collar and chin — shipped in a trailer cut and
# caught by a viewer inside a day. The two give-backs trade against each other
# (both pull the skull off the collar), so they are now tuned TOGETHER against
# that camera: 22 mm of lift and 45% of the forward carry, the skull riding
# just clear of the collar. The +5 is the same benign family as every head move
# above — `figv` shows the max-tick's pair profile shifting to uarm/sho_ball
# grazes (pairs MOVED, not appeared), `near` did not move at any tier
# (40 40 46 36), `zfight` is 0 at all four, every hard invariant is 0, and
# every non-sliding pose is byte-identical (4 A/B screenshots, md5-equal).
WANT_CROSS="76 76 73 47"
[ "${CROSS:-}" = "$WANT_CROSS" ] || { say "GATE cross '${CROSS:-}' != baseline '$WANT_CROSS'"; fail=1; }

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

[ "$fail" = 0 ] && say "ci-proofs: OK ($ELF, near=$NEAR, cross=$CROSS)" || say "ci-proofs: FAIL"
exit $fail
