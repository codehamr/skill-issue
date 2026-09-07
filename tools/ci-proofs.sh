#!/bin/sh
# The self-contained release proof gate. Hard invariants apply on every
# supported architecture; contact censuses use the reviewed architecture and
# fresh-config references below. Their numbers and attribution belong here.
#
#   tools/ci-proofs.sh <binary> [evidence-directory]
#
# Asserted contracts (contact references are selected by architecture):
#   figcheck: surface closed + wound + no coplanar overlap (open/flip/dup/
#             zfight/degen = 0) at four profile tiers, and `near` AND `cross` at their
#             documented fresh-config baselines (the WANT_AR_* / WANT_SR_*
#             rows below are the single authority — the config is part of the
#             number, so the script writes fresh defaults itself)
#   boresight: the round lands on the eye line — AR/SR x hip/ADS x level and tilted
#              aim x three ranges, impact against the analytic eye-ray intersection
#   parity  : no MISMATCH in the shared-code rows
#   budget  : scene/event/world/UI drops = 0 at 20 and 50 bots hard (nothing silently
#             dropped a triangle, event, arena decoration or interface vertex)
#   vmframe : the solved 3P SR low-ready axis matches its production descriptor,
#             uses a monotone ready_s raise/lower transition, keeps spatial action
#             gates exact, and proves fire T0 anchors the tracer on the live barrel
#             along the sim bore, with the ballistic segment inverting back onto the
#             shooter's eye, while the visible arms raise only partially at T1
#             (including prediction)
#   slidecheck: eight-direction thigh/shin clearance, anatomical ordering, bounded
#             bend-plane travel, front/side knee-pad vs back-foot contact roles and
#             bone-length residual
#   armcheck: AR/SR low-ready run starts and slow sweeps through PITCH_MAX retain
#             bounded elbows, weapon frames and torso clearance
#   naturalcheck: real human jumps, air tuck/recovery, landing and buffered re-jump
#             retain authored timing, bounded joints and pure pose restaging;
#             AR/SR carry distinguishes relaxed air from spatial action gates
#   fighit : 280 weapon/pose/yaw/profile cases retain analytic/history agreement,
#             the reviewed hit-covered sample census, and identical visible/hit
#             witnesses when the actor and ray grid rotate together
#   vmtrig  : complete 70-state FP/3P x AR/SR contact matrix, exact child/part
#             census, no patch/pool/counter overflow, bounded work and scratch,
#             and at most 13,000,000 bytes of static vmtrig scratch pools
#   homeui  : at four required resolutions the open readout fits, has zero
#             hover/click pixels outside the cursor mask, preserves root/focus/
#             uistat state, and does not exceed its secured state-matched peak
#   decorcheck: actual crown containment, authored support, grounded free pieces,
#             finite nondegenerate meshes, overflow and unchanged sim ownership
#   weatherproof: seed distribution, deterministic bounded samples, rain roof
#             exclusion, unchanged gameplay RNGs and delayed thunder with map resets
#   boundarycheck: five square biomes, low shot surfaces, full circuits, jumps and mesh capacity
# Also prints the glibc floor for the log — a rise excludes whole distros, so
# it is worth seeing even though it is not gated here.
set -eu

[ "$#" -ge 1 ] && [ "$#" -le 2 ] || {
  echo "usage: ci-proofs.sh <binary> [evidence-directory]" >&2; exit 2;
}
BIN="$1"
BIN="$(realpath "$BIN")"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Failures retain their exact proof logs. An optional directory also keeps success
# evidence in a new child directory, so an earlier run is never overwritten.
keep_logs=0
if [ "$#" = 2 ]; then
  mkdir -p "$2"
  TMPD="$(mktemp -d "$2/ci-proofs.XXXXXX")"
  TMPD="$(realpath "$TMPD")"
  keep_logs=1
else TMPD="$(mktemp -d)"; fi
cleanup() {
  result=$?
  trap - EXIT
  if [ "$keep_logs" = 1 ] || [ "$result" -ne 0 ]; then
    printf '%s\n' "ci-proofs: evidence $TMPD"
  elif ! rm -rf "$TMPD"; then
    printf '%s\n' "ci-proofs: cleanup incomplete; evidence retained at $TMPD" >&2
  fi
  exit "$result"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' HUP TERM
RUN=""
# Run a foreign ELF through its matching QEMU user interpreter. This validates
# harness behavior, not the foreign platform's native devices.
ELF="$(od -An -tx1 -j18 -N2 "$BIN" | tr -d ' ')"
case "$ELF" in
  3e00) [ "$(uname -m)" = x86_64 ]  || RUN="qemu-x86_64-static" ;;
  b700) [ "$(uname -m)" = aarch64 ] || RUN="qemu-aarch64-static" ;;
esac

fail=0
CROSS=""
say() { printf '%s\n' "$*"; }
# Each invocation owns a newly allocated directory. Fresh defaults must not
# depend on deleting a previous file, including on shared Windows filesystems.
fresh_config() {
  cfg_dir="$(mktemp -d "$TMPD/config.XXXXXX")" || return 1
  CFG="$cfg_dir/config.cfg"
}
run() {
  fresh_config || return 1
  ( cd "$ROOT" && $RUN "$BIN" --seed 1337 --config "$CFG" --do "$1" ) 2>&1
}
# spawn: run one proof in the BACKGROUND into $TMPD/NAME.{log,status}. The
# grading below stays sequential and unchanged — it just finds these results
# pre-run. Each job gets its OWN fresh config path (the config is part of the
# number, and the shared $CFG would race between concurrent jobs).
spawn() {
  s_name="$1"; s_script="$2"; s_seed="${3:-1337}"
  (
    if fresh_config && ( cd "$ROOT" && $RUN "$BIN" --seed "$s_seed" --config "$CFG" \
          --do "$s_script" ) >"$TMPD/$s_name.log" 2>&1
    then echo 0 >"$TMPD/$s_name.status"
    else echo $? >"$TMPD/$s_name.status"; fi
  ) &
}
spawned_status() { cat "$TMPD/$1.status" 2>/dev/null || echo 99; }
run_size() {
  width="$1"; height="$2"; script="$3"
  fresh_config || return 1
  ( cd "$ROOT" && $RUN "$BIN" --w "$width" --h "$height" \
      --seed 1337 --config "$CFG" --do "$script" ) 2>&1
}
field() {
  printf '%s\n' "$1" | awk -v key="$2" -v unit="${3:-}" '
    { for (i=1; i<=NF; i++) if (index($i,key "=")==1) {
        n++; value=substr($i,length(key)+2)
        if (unit == "mm") {
          if (value !~ /^[0-9]+[.][0-9]+mm$/) bad=1
          sub(/mm$/, "", value)
          value=int(value)
        } else if (value !~ /^[0-9]+$/) bad=1
      }
    }
    END { if (n==1 && !bad) print value }
  '
}
gate_command() {
  name="$1"; script="$2"; pattern="$3"
  log="$TMPD/$name.log"
  if [ -f "$TMPD/$name.status" ]; then status="$(spawned_status "$name")"
  elif run "$script" >"$log"; then status=0; else status=$?; fi
  [ "$status" = 0 ] || { say "GATE $name exit=$status"; fail=1; }
  contract_rows="$(grep -Ec "$pattern" "$log" || true)"
  [ "$contract_rows" = 1 ] || {
    say "GATE $name success-contract rows=$contract_rows (want 1): '$pattern'"; fail=1;
    # Publish the measured result beside the rejected contract. The complete
    # artifact stays available, while a bounded tail exposes summary drift or
    # a proof's own diagnostics directly in the workflow annotation log.
    say "GATE $name evidence: $log"
    tail -n 12 "$log" | sed "s/^/GATE $name output: /"
  }
}

# The figure sweeps and viewmodel contact proofs run concurrently into
# independent config/log/status files. Grading remains sequential below.
# Supported figure contact censuses use the industrial control seed 2. Seed 1337 is
# a snow map: terrain edits alter its bot spawn/path, changing the
# measured poses even when the soldier mesh is unchanged. World-dependent
# movement remains covered by boundarycheck, naturalcheck and the bot proofs.
FIG_SEED=1337
case "$ELF" in b700|3e00) FIG_SEED=2 ;; esac
for wpn in ar sr; do
  for d in 0 3 8 12 30; do spawn "figcheck-$wpn-$d" "figcheck 60 $d $wpn" "$FIG_SEED"; done
done
spawn vmtrig "vmtrig"
spawn vmcheck "vmcheck"
wait

# figcheck at all four profile tiers, plus the close-distance control, for both weapons.
# The 8 m case selects k=12; 0 m and 3 m both select k=16. Keep each profile's
# near/cross family separate: an SR baseline cannot accidentally bless an AR move.
AR_NEAR=""; AR_CROSS=""; SR_NEAR=""; SR_CROSS=""
for wpn in ar sr; do
  NEAR=""; CROSS=""
  for d in 0 3 8 12 30; do
    log="$TMPD/figcheck-$wpn-$d.log"
    status="$(spawned_status "figcheck-$wpn-$d")"
    [ "$status" = 0 ] || { say "GATE figcheck wpn=$wpn dist=$d exit=$status"; fail=1; }
    fig_rows="$(awk '/^figcheck / { n++ } END { print n + 0 }' "$log")"
    [ "$fig_rows" = 1 ] || {
      say "GATE figcheck wpn=$wpn dist=$d summary rows=$fig_rows (want exactly 1)"; fail=1;
    }
    line="$(sed -n '/^figcheck /p' "$log")"
    case "$line" in
      *" wpn=$wpn "*) ;;
      *) say "GATE figcheck wpn=$wpn dist=$d summary has wrong weapon"; fail=1 ;;
    esac
    case "$d" in 0|3) want_k=16 ;; 8) want_k=12 ;; 12) want_k=8 ;; 30) want_k=6 ;; esac
    actual_k="$(field "$line" k)"
    [ "$actual_k" = "$want_k" ] || {
      say "GATE figcheck wpn=$wpn dist=$d k=$actual_k (want $want_k)"; fail=1;
    }
    pw="$(field "$line" pose_wpn)"
    [ "${pw:-x}" = 1 ] || {
      say "GATE figcheck wpn=$wpn dist=$d missing/failed pose_wpn solver witness"; fail=1;
    }
    pd="$(field "$line" pose_delta mm)"
    case "$pd" in
      ''|*[!0-9]*)
        say "GATE figcheck wpn=$wpn dist=$d missing numeric pose_delta"; fail=1 ;;
      *) [ "$pd" -ge 10 ] || {
        say "GATE figcheck wpn=$wpn dist=$d pose_delta=${pd}mm (want >=10mm)"; fail=1;
      } ;;
    esac
    for k in open flip dup zfight degen; do
      v="$(field "$line" "$k")"
      [ "${v:-x}" = 0 ] || { say "GATE figcheck wpn=$wpn dist=$d $k=$v (want 0)"; fail=1; }
    done
    n="$(field "$line" near)"
    NEAR="${NEAR:+$NEAR }$n"
    c="$(field "$line" cross)"
    CROSS="${CROSS:+$CROSS }$c"
  done
  if [ "$wpn" = ar ]; then AR_NEAR="$NEAR"; AR_CROSS="$CROSS"
  else SR_NEAR="$NEAR"; SR_CROSS="$CROSS"; fi
done
say "info figcheck AR near=$AR_NEAR cross=$AR_CROSS"
say "info figcheck SR near=$SR_NEAR cross=$SR_CROSS"
# `near` (two surfaces parallel within 1 mm) is config-conditioned; fresh
# defaults give the WANT_NEAR census below. Baselined rather than gated at
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
# - 2026-08-30 rounded hero fingertips and continuous thumb roots move the
#   reviewed contact census to 72/72/73/59. The new closing caps are near-tier
#   only; full `figv` sweeps retain zero open/flip/dup/zfight/degen and vmtrig's
#   exact/proxy forbidden crossings remain zero.
# - The tucked-trouser combat boot pass broadens the outsole/upper and buries the
#   shin more decisively inside its shaft: 75/75/75/59. `figv` locates the added
#   close contacts in shin/boot; every hard topology class remains zero.
# - 2026-08-30 adaptive pose/face pass, reviewed against an archived HEAD binary:
#   the new jaw planes and the one-knee/slide contacts move the per-frame maxima to
#   77/77/78/61. The paired 60-tick histogram review is recorded with the cross
#   census below; all five hard topology classes remain zero.
# - The segment-aware arm solve and collision-reactive slide legs move the same
#   deterministic sweep to 75/75/75/61. This is the exact old/new build comparison
#   documented with the cross census below, not an unreviewed baseline bump.
# - 2026-08-30 physical-bore/arm/slide pass, identical O3 flags, seed and fresh
#   config: the AR firing carriage no longer enters low-ready, downward aim moves the
#   complete weapon complex forward, and slide exit retains its knee bend plane. The
#   reviewed pair-family diff moves only arm/weapon and thigh/shin contacts; all five
#   hard topology classes remain zero.
# - 2026-08-31 directional-slide/SR-pole A/B against the saved pre-change O3 binary:
#   knee-pad front/side contacts and sole-supported rear contacts move the maxima to
#   78/78/75/61. At k=16 the aggregate shin/boot, shin/bootcuff and shin/outsole
#   families fall 346->313, 64->15 and 107->28; at k=6 thigh/shin falls 69->26.
#   The bounded firing-arm pole redistributes only the expected arm/carrier/weapon
#   families. All eight sweeps retain zero open/flip/dup/zfight/degen.
# - The committed directional-slide state measures one contact off that record in
#   three cross slots (AR k=6 65->66, SR close 183->182): the census was recorded one
#   tweak before the final pole values. Re-verified against the pre-pass build with a
#   full figv family diff: shin/boot 345->312, thigh/shin and shin/knee fall, gun/palm
#   and grip_digits rise a few contacts — exactly the pass's own families, no foreign
#   family moves, near quadruples and every hard topology class unchanged.
# - 2026-09-01 physical-bore/slide-handoff pass, A/B against the saved pre-change O3
#   binary with identical seed and fresh config: AR near 78/78/75/61 ->
#   75/75/75/61 and SR near 98/98/58/41 -> 94/94/58/41. The full 60-tick close-tier
#   `figv` family diff moves the maximum through the expected pose contacts:
#   neck/helm_shell -8, carrier/gun +4 for AR, and the shared hand/weapon/arm families
#   for SR. No mesh primitive changed; all eight sweeps retain zero
#   open/flip/dup/zfight/degen.
# The live-bot census is conditioned by both gameplay and compiler target. A secured
# same-flags A/B from b1da1b5 through 2e3d26f to the current source reproduces the
# immediate-recoil/no-ADS-speed-scale pose change. Figure/weapon primitives,
# skel_solve and anim_tick are byte-identical; every hard topology class stays zero.
# Close-tier near deltas are neck/helm_shell +8, shin/boot -2 and AR carrier/gun -4
# over all 60 ticks. This is a reviewed pose census, not a geometry tolerance.
# 2026-09-06 tactical AI: this is a LIVE bot pose census. Matched -O3 builds
# retain the exact controlled puppet sweep (AR near/cross 74/80, SR 94/161),
# while the new spawn personalities, footwork and tracking choose different live
# poses. The 60-tick figv A/B attributes the largest changes to palm/forearm,
# gun/hand, neck/helmet, carried equipment and gait contacts. Mesh sources and
# topology are unchanged; vmtrig still gates forbidden weapon/hand contacts.
# Native evidence: build/ai-multiplayer-{before,after}/controlled-*.log,
# figure-families.json and the preserved pre-change binary. Every hard topology
# class remains zero at all eight weapon/profile combinations.
# The retained baseline/model-only/integrated O3 sweeps isolate the current
# sleeve roots, elbows and pelvis-frame pouch from the gait release. Model-only
# near is unchanged except SR far's removed forearm/pouch tangent. The integrated
# near changes are boot/shin contacts; every topology class stays zero. Evidence:
# build/player-polish-20260906/ci-{before,model-final,after}/ (archived evidence).
# The native reference for 8 m is retained in
# build/player-natural-20260906/ci-before/: AR k=12 near75/cross100 and SR
# near84/cross157, with all five hard topology classes zero.
# Natural model: matched O3 baseline/model-only/integrated censuses are retained
# under build/player-natural-20260906/ci-{before,model,integrated}. The yoke
# removes trap contacts; the thinner curved straps enter their shirt/vest roots.
# The new boot shaft has nine self crossings over 60 AR ticks (previously 11);
# 962 additional bootcuff/bootlace witnesses are the attached cross-laces, not
# folded shaft faces. Boot/shin crossings fall 376->5; the wider instep joins
# the shaft more deeply. Near changes after model isolation are hand/carry poses.
# Topology stays zero in all ten sweeps.
WANT_AR_NEAR="72 72 76 71 63"
# The soldier references come from complete native AArch64 and emulated
# x86_64 sweeps of the release executables.
# Deeper sleeve/yoke roots close the visible shoulder seam; attached shirt/strap
# contacts change while all hard topology and forbidden grip classes stay zero.
# AR maxima agree on both measured architectures. Unsupported ELFs retain
# the older seed and reference rather than borrowing an unmeasured census.
# Seed-2 control, reviewed against a native HEAD rebuild with pinned tuning
# and flags, plus the preserved pre-change x86 executable. Before/after full
# summaries match at all five distances for both weapons on both supported ELFs.
# No topology or contact threshold is relaxed. Paired logs and measurements:
# build/organic-boundary-ki75y5tj/figure-controls-final.json.
case "$ELF" in b700|3e00) WANT_AR_NEAR="70 70 73 71 63" ;; esac
[ "$AR_NEAR" = "$WANT_AR_NEAR" ] || {
  say "GATE AR near '$AR_NEAR' != reviewed baseline '$WANT_AR_NEAR'"; fail=1;
}

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
# - The shared anatomy/contact and weapon-aware pose authority settled at
#   101/101/81/58. Pair-family review separates palm/thumb/digits that used to
#   be one `hand` range; exact guard/blade/receiver primitive and runtime-proxy
#   crossings are independently zero in all 70 vmtrig states, including five
#   reload stations, recoil extrema, fire T0/T1, low/raise/swap/slide/dead and
#   the SR bolt hold.
# - 2026-08-30 headset cleanup: 101/101/81/58 -> 97/97/81/58. The only removed
#   geometry is the symmetric ear-cup pair (two 32-triangle chains); `figv`
#   shows no ear-cup family in the new histogram, while the near quadruple and
#   every coarse-tier cross count remain unchanged.
# - The broader protective toe, buried upper and tight shaft move the reviewed
#   census to 110/110/92/61. `figv` attributes the delta to shin/boot contact;
#   the laces and the support-thumb shortening introduce no hard mesh defect.
# - 2026-08-30 adaptive pose/face A/B (`git archive HEAD`, identical seed/config):
#   torso/uarm drops 54 -> 28, uarm/farm 12 -> 1 and shin/boot 398 -> 322 over
#   60 close-tier frames — the reported arm/torso and folded-leg penetrations are
#   reduced. Expected joint continuity moves to elbow/farm and uarm/uarm; the new
#   skull planes redistribute its designed equipment burial from skull/helm_shell
#   into skull/goggle, skull/helm_rail and skull/chinstrap. Slide boot contacts move
#   from outsole/shaft into the folded cuff. Every run keeps open/flip/dup/zfight/
#   degen at zero, and vmtrig keeps forbidden hand/weapon crossings at zero.
# - Follow-up A/B (identical O3/fast-math/LTO flags, seed and fresh config): checking
#   the whole arm segment keeps both stop-transition arms beyond their expanded torso
#   ellipse. Latching the slide lead and solving in its travel frame removes the four
#   knee/knee crossings after the final proximity response; aggregate thigh/shin drops
#   85 -> 38 at k=16 and 90 -> 49 at k=6. Joint and boot-host contacts rise where the
#   folded leg is deliberately joined, but per-frame maxima fall to 121/121/111/63.
#   All five hard topology classes remain zero at all four tiers.
# - 2026-08-30 lateral-slide A/B, same compiler/seed/fresh config: replacing a
#   travel-relative ground hand (which crosses the legs on a left skid) with the
#   anatomical-side/body-back target removes `thigh/grip_digits` 6 -> 0,
#   `farm/farm` 11 -> 3 and `uarm/elbow` 15 -> 7 from the aggregate SR close sweep.
#   A 26 cm side offset is the reviewed minimum that keeps the wrist outside the
#   lead knee; it also keeps the AR/SR close maxima at 119/208. The remaining
#   coarse-tier redistribution is joint/host contact, with every hard topology
#   class and vmtrig's forbidden mesh/proxy crossings still zero.
# zfight/open/flip/dup/degen are 0 on every tier through all of it, and
# `near` moved only where its own note above says.
# 2026-09: the aim-up working room (skel_solve's `aim_up` gun_o push + elbow
# hints, band 0.12..0.85) moves the arm/torso contact seams whenever the live
# bot aims above ~7 deg, so the close tiers' cross census rose 105 -> 113 (SR
# 182 -> 181). near and every hard topology class are unchanged on all tiers.
# 2026-09-01: the same saved-binary A/B above moves AR cross to 112/112/96/67
# and SR to 180/180/121/95. Close-tier aggregate deltas are confined to the solved
# weapon/arm chain and recovered gait contacts (gun/farm, torso/gun, carrier/arm,
# shin/boot/knee and their equipment hosts); these are new POSES of unchanged closed
# meshes, and vmtrig independently retains zero forbidden hand/weapon crossings.
# 2026-09-02 bot recoil-compensation retune (BOT_SKILL rec_comp, HARD 0.92 -> 0.40):
# figcheck's subject is the LIVE hard bot, so this census tracks that difficulty row —
# on an unchanged binary the same sweep reads cross 95 at EASY and 92 at NORMAL against
# 112 at HARD, so the tier spread dwarfs this move. AR cross goes 112/112/96/67 ->
# 114/114/96/65 with near unchanged. The 60-tick close-tier `figv` aggregate moves a few
# contacts inside the SAME families (shin/boot 340 -> 336, torso/gun 68 -> 64, carrier/gun
# 41 -> 46, thigh/knee 82 -> 85); AR totals 4586 -> 4583 CROSS and 4268 -> 4270 near. No
# family enters or leaves but the rarest (neck/neck out, shingle_strap/farm in, one contact
# each), and all five hard topology classes stay zero on all four tiers.
# The preserved pre-improvement reference has one extra far-tier maximum on x86.
# The reviewed continuous hand route, shared forearm stations and actual firing
# palm profile now give the same census on native AArch64 and emulated x86_64.
# Every non-hand/forearm family is exact against that reference. New shallow
# palm/thumb/index folds were individually measured and reviewed in matched views;
# they include exposed soft knuckle folds, not only hidden root embedding.
# vmtrig still requires zero forbidden contacts; all hard topology limits remain0.
# The near face is one closed mask with an integrated nose and two fitted lenses.
# Its 60-tick attribution has 300 skull/helm_rail crossings, zero skull/goggle,
# skull/chinstrap or skull/helm_shell crossings, and no non-head family changes.
# This removes eight crossings per frame versus the secured swept-head build;
# near counts and both distant profiles stay exact. Matched front/side captures
# and the full visible-hit sweep review the new surface independently.
# The three-way review attributes geometry changes only to sleeve/elbow and
# pouch-host families. The pouch frame removes gun/shingle and hand/shingle
# contacts; deeper sleeve roots trade exposed caps for trunk attachment. Gait
# changes only leg/boot families: close outsole/outsole 26->6 and boot/boot 17->8
# over sixty ticks, with ankle/cuff host contacts redistributed. Both weapons
# preserve exact upper-body family counts between model-only and integrated runs.
WANT_AR_CROSS="160 160 155 130 79"
case "$ELF" in b700|3e00) WANT_AR_CROSS="129 129 124 109 74" ;; esac
[ "$AR_CROSS" = "$WANT_AR_CROSS" ] || {
  say "GATE AR cross '$AR_CROSS' != reviewed baseline '$WANT_AR_CROSS'"; fail=1;
}

# SR had no selectable figcheck path at 2a6b08c. For this first authority the
# secured source was instrumented in /tmp with ONLY the final `ar|sr` selector;
# its AR output reproduced 76/76/75/62 and 94/94/79/56 exactly before its SR
# result was admitted. The old SR values were near=99/99/48/31 and
# cross=161/161/107/87. Full 60-tick pair histograms explain the new values:
# close near drops 12 as the undivided hand/hand and gun/hand contacts become
# bounded palm/thumb/digit contacts; the two far near tiers are unchanged.
# Cross +13/+13/-2/+10 comes from those disjoint hand ranges, the new scope
# saddle/low-ready hold, and the shared jaw/chinstrap anatomy. gun/gun remains
# 4800/4800/2640 across the first three aggregate sweeps (2703 -> 2700 at 30 m),
# while vmtrig reports zero forbidden mesh/proxy crossings and figcheck reports
# zero open/flip/dup/zfight/degen at every tier.
# Removing the same symmetric ear-cup chains first changed only the close-tier
# maxima. Rounded fingertip caps and the connected support-thumb sweep moved that
# census again. The shared combat-boot pass now gives the values below; its `figv`
# delta is the same designed shin/boot burial as AR and all hard topology classes
# remain zero. The reviewed adaptive pose/face pass described in the AR census moves
# the SR maxima, and the follow-up segment/slide A/B moves them again with the same
# anatomy pair-family deltas: close-tier cross stays 208 while the other censuses fall.
# The closed-chain follow-up documented in the AR census affects only shared anatomy:
# SR near becomes 94/94/58/39 and cross falls 208/208/135/92 -> 193/193/126/85, with
# open/flip/dup/zfight/degen still zero in every one of the eight full sweeps.
# 2026-09-02 rec_comp retune (the AR census above carries the review): the same pose
# sample takes SR near's k=6 tier 41 -> 40 and SR cross 180/180/121/95 -> 181/181/122/95,
# with SR aggregates 8681 -> 8705 CROSS and 5427 -> 5425 near inside the existing families.
WANT_SR_NEAR="93 93 84 55 40"
# Architecture-specific near counts are stable in each paired seed-2 control.
case "$ELF" in
  b700) WANT_SR_NEAR="95 95 86 55 42" ;;
  3e00) WANT_SR_NEAR="94 94 85 55 42" ;;
esac
# SR uses the same reviewed sleeve, pouch and gait changes, with its own
# weapon-aware pose and independently retained sixty-tick family census.
WANT_SR_CROSS="229 229 211 152 114"
case "$ELF" in b700|3e00) WANT_SR_CROSS="205 205 185 133 104" ;; esac
[ "$SR_NEAR" = "$WANT_SR_NEAR" ] || {
  say "GATE SR near '$SR_NEAR' != reviewed baseline '$WANT_SR_NEAR'"; fail=1;
}
[ "$SR_CROSS" = "$WANT_SR_CROSS" ] || {
  say "GATE SR cross '$SR_CROSS' != reviewed baseline '$WANT_SR_CROSS'"; fail=1;
}

# Viewmodel, contact, Lean and recoil contracts. Each command gets a fresh config;
# an exit status alone is not enough because several older proofs publish their hard
# result in a summary line consumed here.
# The exact near/cross witness catches shallow self-intersections between hand
# parts (including the hero-tier thumb shoulder) that vmtrig intentionally does
# not classify as forbidden weapon contact.
# 2026-08-30 physical-bore/arm A/B: stable downward preferred angles and the unified
# live firing carriage move the shallow hand/weapon contact census; hard topology and
# vmtrig's forbidden mesh/proxy census remain zero.
# near=200: zeroing RECOIL_POSE's angular channels (the barrel spring owns the rise)
# changed the poses the 20 recoil states sweep, moving the shallow hand/weapon census
# 199->200 while cross returned 145->138; the 194 this gate carried was stale for the
# whole recorded history and never blocked because it drifted alongside real reds.
# 2026-09-02 near=198, worst [sr ads=1.0 side_neg] -> [sr ads=1.0 idle]: the visual
# kick is damped by the ROUND'S INDEX now (recoil_kick_damp, sim/skeleton.inc), and the
# recoil states this fixture sweeps are staged deep in a burst, so their impulses are
# smaller and two shallow hand/weapon pairs fall out of the census. Isolated by
# ablation — with the damp forced to 1.0 the line reads near=200 side_neg again, bit
# for bit — and nothing topological moved: tris, cross and all five zero invariants
# (open/flip/dup/zfight/degen) are unchanged.
# Identical native AArch64 builds of b1da1b5, 2e3d26f and the secured current
# source all measure 194/138 with all topology zeros. The identical secured source
# compiled for x86_64 measures 198/138 in the complete emulated sweep.
# The reviewed continuous reload and emitted-palm clearance preserve those near
# values and reduce the current cross witness to134 in both complete sweeps.
# Dorsal-only glove shaping retains every forbidden-contact and grip-interval
# invariant in vmtrig; the 20-state shallow crossing census changes 134->142.
WANT_VM_NEAR=198
[ "$ELF" != b700 ] || WANT_VM_NEAR=194
gate_command vmcheck "vmcheck" "^vmcheck tris=[1-9][0-9]* worst=\[.*\] open=0 flip=0 dup=0 zfight=0 near=$WANT_VM_NEAR cross=142 degen=0 recoil_states=20$"
gate_command vmsight "vmsight" '^vmsight total=0 '
gate_command vmscope "vmscope" '^vmscope scope_tris=[1-9][0-9]* open_at=0[.]55 baseline_err=.* recoil_min_ocular=.* recoil_axis=.* recoil_center=.* recoil_pip=.* recoil_radius_delta=.* ok$'
gate_command vmframe "vmframe" '^vmframe ok$'
grep -Eq '^vmframe pose_only state=1 pose=1 .* ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing state-pure SKEL_POSE_ONLY witness"; fail=1;
  }
grep -Eq '^vmframe 3p_axis low=.*deg target=.*deg raise=.*deg target=.*deg ready=0[.][0-9]+ busy=5 max=0[.]000deg ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing production-derived 3P low-ready axis/busy witness"; fail=1;
  }
grep -Eq '^vmframe ready_transition raise=\(0[.]000 0[.][0-9]+ 0[.][0-9]+ 0[.][0-9]+ 0[.][0-9]+\) lower=\(0[.][0-9]+ 0[.][0-9]+ 0[.][0-9]+\) ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing monotone ready transition witness"; fail=1;
  }
grep -Eq '^vmframe reset_transition ready=1[.]000 pose=0[.]000mm lower=\(0[.][0-9]+ 0[.][0-9]+ 0[.][0-9]+\) ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing reset-to-idle transition witness"; fail=1;
  }
# The C proof owns the measured tolerances (including the 0.001 mrad bore
# bound). Fast-math normalization need not round to the literal 0.0000 on
# every CPU/compiler. Require all measurements and its explicit verdict;
# gate_command above also preserves the proof's failing exit status.
grep -Eq '^vmframe fire sr T0 .*anchor=[0-9]+[.][0-9]+mm visible_hold=[0-9]+[.][0-9]+mm bore=[0-9]+[.][0-9]+mrad ballistic_eye=[0-9]+[.][0-9]+mm .* stale=1 ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing exact live-barrel/tracer T0 witness"; fail=1;
  }
grep -Eq '^vmframe fire sr T1 .*ready=0[.]000/0[.][12][0-9][0-9] .* ok$' \
  "$TMPD/vmframe.log" || {
    say "GATE vmframe missing partial T1 fire carry witness"; fail=1;
  }
gate_command vmtrig "vmtrig" '^vmtrig worst .* ok$'
gate_command slidecheck "slidecheck" '^slidecheck dirs=8 clear=[0-9]+[.][0-9]mm@[0-7]/[0-9]+ side=[0-9]+[.][0-9]mm step=[0-9]+[.][0-9]mm@[0-7]/[0-9]+/[01] pole=[0-9]+[.][0-9]+deg@[0-7]/[0-9]+/[01] steady=[0-9]+[.][0-9]mm/[0-9]+[.][0-9]+deg contact=[0-9]+[.][0-9]@[0-7]/[0-9]+/[0-9]+[.][0-9]@[0-7]/[0-9]+mm exit_step=[0-9]+[.][0-9]mm@[0-7]/[0-9]+/[01] exit_pole=[0-9]+[.][0-9]+deg@[0-7]/[0-9]+/[01] exit_side=[0-9]+[.][0-9]mm handoff=[0-9]+[.][.][0-9]+ swing_lag=[0-9]+ bone=[0-9]+[.][0-9]+mm finite=1 ok$'
gate_command armcheck "armcheck" '^armcheck weapons=2 max_step=[0-9]+[.][0-9]mm trigger=[0-9]+[.][0-9]mm gun=[0-9]+[.][0-9]mm/[0-9]+[.][0-9]+deg min_clear=[0-9]+[.][0-9]+ ok$'

# Do not make field order part of the interface: parse the named witnesses from the
# self-test, every state row, and the final census independently. `bytes` is only the
# static vmtrig proof scratch reported by the harness (hand/weapon leaf and node
# pools plus any compact seam sidecars), not the process's complete BSS. The
# 13,000,000-byte ceiling admits the reviewed compact four-pool layout while,
# together with the separately measured non-vmtrig delta, keeping total extra BSS
# below roughly 20 MB. C-side failures still own geometric tolerances; these checks
# prevent a truncated/reformatted log or a silently dropped pack/pool/counter witness
# from satisfying CI merely because one final line says `ok`.
if ! awk '
  function clear_fields(    k) {
    for (k in field) delete field[k]
  }
  function parse_fields(    i, p, key) {
    clear_fields()
    for (i = 1; i <= NF; i++) {
      p = index($i, "=")
      if (p > 1) {
        key = substr($i, 1, p - 1)
        field[key] = substr($i, p + 1)
      }
    }
  }
  function need(key, value) {
    if (!(key in field) || field[key] != value) bad = 1
  }
  function is_uint(value) {
    return value ~ /^[0-9]+$/
  }

  $1 == "vmtrig" && $2 == "self" {
    self_rows++
    parse_fields()
    need("ambiguity", "0")
    need("proof", "0/0/0/0/0/0/0/0")
    if ($NF != "ok") bad = 1

    ncap = split(field["scratch"], cap, "[/:]")
    if (ncap != 4) bad = 1
    for (i = 1; i <= ncap; i++)
      if (!is_uint(cap[i]) || cap[i] + 0 <= 0) bad = 1
    self_hand_leaf_cap = cap[1] + 0
    self_hand_node_cap = cap[2] + 0
    self_weapon_leaf_cap = cap[3] + 0
    self_weapon_node_cap = cap[4] + 0

    if (!is_uint(field["work"]) || field["work"] + 0 <= 0) bad = 1
    if (!is_uint(field["bytes"]) || field["bytes"] + 0 <= 0 ||
        field["bytes"] + 0 > 13000000) bad = 1
    self_bytes = field["bytes"] + 0
  }

  $1 == "vmtrig" && ($2 == "fp" || $2 == "3p") {
    state_rows++
    parse_fields()
    need("miss", "0/0")
    need("ov", "0/0")
    need("missing", "0")
    need("overflow", "0")
    need("unclassified", "0")
    need("proof", "0/0")
    need("amb", "0")
    need("state", "0")
    need("clamp", "0")
    if ($NF != "ok") bad = 1
  }

  $1 == "vmtrig" && $2 == "worst" {
    summary_rows++
    parse_fields()
    need("rows", "70")
    need("children", "840")
    need("bucket", "16/18:17/19")
    need("live", "68/816/816")
    need("dead", "2/24/0")
    need("hand", "420/420")
    need("part", "140/140/48/512")
    need("state", "0")
    need("matrix", "0")
    need("census", "0/0")
    # Reviewed actual hand clearance removes the former100 shallow crossings.
    need("mesh", "0.0mm")
    need("mx", "0")
    need("proxy_cross", "0")
    need("missing", "0")
    need("overflow", "0")
    need("unclassified", "0")
    need("proof", "0/0")
    need("amb", "0")
    need("counter_ov", "0")
    if ($NF != "ok" || !is_uint(field["pairs"]) || field["pairs"] + 0 <= 0)
      bad = 1

    nwork = split(field["work"], work, "/")
    if (nwork != 2 || !is_uint(work[1]) || !is_uint(work[2]) ||
        work[1] + 0 <= 0 || work[2] != "64000000000" ||
        work[1] + 0 > work[2] + 0) bad = 1

    nscratch = split(field["scratch"], scratch, "[/:]")
    if (nscratch != 4) bad = 1
    for (i = 1; i <= nscratch; i++)
      if (!is_uint(scratch[i]) || scratch[i] + 0 <= 0) bad = 1
    summary_hand_peak = scratch[1] + 0
    summary_hand_node_peak = scratch[2] + 0
    summary_weapon_peak = scratch[3] + 0
    summary_weapon_node_peak = scratch[4] + 0

    nsummary_cap = split(field["cap"], summary_cap, "[/:]")
    if (nsummary_cap != 4) bad = 1
    for (i = 1; i <= nsummary_cap; i++)
      if (!is_uint(summary_cap[i]) || summary_cap[i] + 0 <= 0) bad = 1

    if (!is_uint(field["bytes"]) || field["bytes"] + 0 <= 0 ||
        field["bytes"] + 0 > 13000000) bad = 1
    summary_bytes = field["bytes"] + 0
  }

  END {
    if (self_rows != 1 || state_rows != 70 || summary_rows != 1) bad = 1
    if (self_rows == 1 && summary_rows == 1) {
      if (self_bytes != summary_bytes) bad = 1
      if (summary_cap[1] != self_hand_leaf_cap ||
          summary_cap[2] != self_hand_node_cap ||
          summary_cap[3] != self_weapon_leaf_cap ||
          summary_cap[4] != self_weapon_node_cap) bad = 1
      # Keep at least 20% capacity above every measured leaf/node peak while
      # the hard per-row pool-overflow witnesses remain zero.
      if (summary_hand_peak * 6 > self_hand_leaf_cap * 5 ||
          summary_hand_node_peak * 6 > self_hand_node_cap * 5 ||
          summary_weapon_peak * 6 > self_weapon_leaf_cap * 5 ||
          summary_weapon_node_peak * 6 > self_weapon_node_cap * 5) bad = 1
    }
    exit bad ? 1 : 0
  }
' "$TMPD/vmtrig.log"; then
  say "GATE vmtrig incomplete 70-state census or work/scratch/BSS/overflow contract"
  fail=1
fi

vmhand_log="$TMPD/vmhand.log"
if run "vmhand" >"$vmhand_log"; then status=0; else status=$?; fi
[ "$status" = 0 ] || { say "GATE vmhand exit=$status"; fail=1; }
if ! awk '
  $1 == "vmhand" && ($2 == "ar" || $2 == "sr") {
    rows++; by_weapon[$2]++; bend = -999; bore = -999;
    for (i = 1; i <= NF; i++) {
      if ($i == "bend=") bend = $(i + 1) + 0;
      if ($i == "bore=") bore = $(i + 1) + 0;
    }
    if ($3 == "hip" || $3 == "ads") base++;
    else if ($3 ~ /^rl(15|35|55|75|90)$/) reload++;
    else if ($3 ~ /^(hip|ads)_(back|lift|side[+-]|over)$/) recoil++;
    else bad = 1;
    if ($4 == "trigger") { trigger++; if (!(bend >= 0 && bend < 45)) bad = 1; }
    else if ($4 == "support") { support++; if (!(bore >= 35 && bore <= 65)) bad = 1; }
    else bad = 1;
    if ($0 !~ / clamp=0 ok$/) bad = 1;
  }
  END {
    exit !(rows == 68 && by_weapon["ar"] == 34 && by_weapon["sr"] == 34 &&
           base == 8 && reload == 20 && recoil == 40 &&
           trigger == 34 && support == 34 && !bad);
  }
' "$vmhand_log"; then
  say "GATE vmhand incomplete live matrix or trigger-bend<45/support-bore=35..65 violation"
  fail=1
fi

gate_command figmotion "figmotion" '^figmotion summary rows=16 reload_boundary_mm=[0-9.]+ brace_boundary_mm=[0-9.]+ cancellations=4680 errors=0/0/0/0/0 ok$'
gate_command gaitcheck "gaitcheck" '^gaitcheck summary cases=7 plants=[0-9]+ onset_mm=[0-9.]+ planted_mm=0.000000 limb_mm=[0-9.]+ finite=0 state=0 failed=0 ok$'
gate_command naturalcheck "naturalcheck" '^naturalcheck summary jumps=12 carry=16 finite=0 pose_mutations=0 restore=1 failures=0 ok$'
# The sleeve root covers 45 additional body-grid samples against the preserved
# closed-face reference. All head rows, visible miss counts and outside-distance
# extrema are unchanged; the ray grid and history reconstruction are exact.
# Per-pose body/head rows also expose visible misses and empty hitbox space;
# the analytic/history error count does not claim those are zero.
# Model-only visible_hit is 26355; the integrated carry/air pose is 26550.
# The 280-row census keeps analytic/history and yaw-rotated witnesses identical.
WANT_FIGHIT_VISIBLE=26550
case "$ELF" in b700|3e00) WANT_FIGHIT_VISIBLE=26605 ;; esac
gate_command fighit "fighit" "^fighit summary rows=280 rays=272160 history=272160 visible_hit=$WANT_FIGHIT_VISIBLE triangle_control=1 yaw_errors=0 step_mm=20 errors=0/0/0/0 ok$"
# The summary cannot stand in for the complete weapon/pose/yaw/profile matrix.
# C owns the rotated-ray invariant; also require its individual witnesses to
# retain the same payload at every yaw and prohibit omitted or repeated rows.
if ! awk '
  $1 == "fighit" && $2 != "summary" {
    rows++;
    if (NF != 9 || $2 !~ /^(AR|SNIPER)$/ ||
        $3 !~ /^(stand|crouch|lean-left|lean-right|ads|reload|slide)$/ ||
        $4 !~ /^yaw=(0|45|90|145|180)$/ || $5 !~ /^k=(16|12|8|6)$/ ||
        $6 !~ /^body=[0-9]+\/[0-9]+\/[0-9]+[.][0-9]+$/ ||
        $7 !~ /^head=[0-9]+\/[0-9]+\/[0-9]+[.][0-9]+$/ ||
        $8 !~ /^gear=[0-9]+\/[0-9]+$/ ||
        $9 !~ /^hash=[0-9a-f]+$/ || length($9) != 21) bad = 1;
    key = $2 SUBSEP $3 SUBSEP $4 SUBSEP $5;
    if (++seen[key] != 1) bad = 1;
    group = $2 SUBSEP $3 SUBSEP $5;
    payload = $6 FS $7 FS $8 FS $9;
    if (group in reference && reference[group] != payload) bad = 1;
    reference[group] = payload;
  }
  END { exit !(rows == 280 && !bad) }
' "$TMPD/fighit.log"; then
  say "GATE fighit incomplete/duplicate matrix or yaw-dependent visible/hit witness"
  fail=1
fi
gate_command botsuppress "botsuppress" '^botsuppress summary cases=46 failed=0 authority=gun_shoot body=completed-tick ok$'
gate_command recoil "recoil" '^recoil ok$'
gate_command spstart "spstart" '^spstart summary cases=18 result=1 late_accept=3 audio_races=2 ok$'
gate_command audiocheck "audiocheck" '^audiocheck summary catalogue=20 lifecycle=13 ok$'
# Five biomes x three times x seven weather kinds; wire enums include RANDOM.
gate_command environmentcheck "environmentcheck" '^environmentcheck combinations=105 wire=192 widgets=21 deferred=1 arenas=3 authority=join-rematch-leave fail=0 ok$'
gate_command weatherproof "weatherproof" '^weatherproof modes=7 samples=33600 roof_cases=27 checked_vertices=[0-9]+ neutral=1 ok$'
gate_command acousticcheck "acousticcheck" '^acousticcheck summary geometry=4 segments=15 materials=3 rates=4 sim_rng=unchanged ok$'
gate_command feedbackproof "feedbackproof" '^feedbackproof cases=212 failed=0 ok$'
gate_command filmtrackproof "filmtrackproof" '^filmtrackproof summary cases=13 speed=180 accel=1200 face_mm=100x70 ok$'
gate_command filmcueproof "filmcueproof" '^filmcueproof summary cases=22 ok$'

gate_command decorcheck "decorcheck" '^decorcheck trees=2560 tips=14792 buildings=240 controls=10 peak=720 state=same fail=0 ok$'
# 128 maps x 4 walls x 9 positions x 2 ray heights; four contact modes.
# Tangents exercise both directions on each wall, circuits retain all six modes.
gate_command boundarycheck "boundarycheck 128" '^boundarycheck maps=128 rays=9216 routes=768 contacts=18432 tangents=1024 meshes=128 peak_verts=[0-9]+ fail=0 ok$'
decor_small_rows="$(grep -Ec '^decorcheck small=1728 floating=0 worst=(0[.]000000/){8}0[.]000000$' "$TMPD/decorcheck.log" || true)"
[ "$decor_small_rows" = 1 ] || {
  say "GATE decorcheck missing grounded small-piece census"; fail=1;
}
gate_command gunwall "gunwall" '^gunwall ok$'
gate_command boresight "boresight" '^boresight ok$'
gate_command netfigure "netfigure" '^netfigure carry .* ok$'
for wpn in ar sr; do
  fire_rows="$(grep -Ec "^netfigure fire=$wpn .* ok$" "$TMPD/netfigure.log" || true)"
  [ "$fire_rows" = 1 ] || {
    say "GATE netfigure $wpn fire witness rows=$fire_rows (want 1)"; fail=1;
  }
done
gate_command botmemory-normal "skill normal; botmemory" '^botmemory .* proofrestore=1 lose_t=[0-9]+ hold=1/1 expiry=1/1 skill=1$'
gate_command botmemory-hard "skill hard; botmemory" '^botmemory .* proofrestore=1 lose_t=[0-9]+ hold=1/1 expiry=1/1 skill=2$'
gate_command netrecoil "netrecoil" '^netrecoil cl_pred=.* ok$'
netrecoil_single_rows="$(grep -Ec '^netrecoil single .* replay_body=1 .* sidefx=1/1 counts=1/1 noise_t=[0-9]+ events=2/0 ok$' \
  "$TMPD/netrecoil.log" || true)"
[ "$netrecoil_single_rows" = 1 ] || {
  say "GATE netrecoil single-shot witness rows=$netrecoil_single_rows (want 1)"; fail=1;
}
netrecoil_burst_rows="$(grep -Ec '^netrecoil burst .* replay_body=1 .* sidefx=1/1 counts=3/3 noise_t=[0-9]+ events=2/0 ok$' \
  "$TMPD/netrecoil.log" || true)"
[ "$netrecoil_burst_rows" = 1 ] || {
  say "GATE netrecoil burst witness rows=$netrecoil_burst_rows (want 1)"; fail=1;
}
gate_command netdeath "netdeath" '^netdeath death=1 recoil=1 respawn=1 ok$'
netdeath_dead_rows="$(grep -Ec '^netdeath respawn motion=1 lean=1 life=1 wp=1 vm=1 buttons=1 rng=1 last_eye=[0-9]+[.][0-9]+$' \
  "$TMPD/netdeath.log" || true)"
[ "$netdeath_dead_rows" = 1 ] || {
  say "GATE netdeath dead-life respawn rows=$netdeath_dead_rows (want 1)"; fail=1;
}
netdeath_live_rows="$(grep -Ec '^netdeath respawn_live zero_err=1 fresh=1 buttons=1 rng=1 corr=0/0$' \
  "$TMPD/netdeath.log" || true)"
[ "$netdeath_live_rows" = 1 ] || {
  say "GATE netdeath live zero-error spawn rows=$netdeath_live_rows (want 1)"; fail=1;
}
gate_command leanstep "leanstep" '^leanstep symmetry=.* ok$'
gate_command netanim "netanim 600 4" '^netanim .* ok$'
gate_command netpredict "netpredict 600" '^netpredict .*dedup=stable '
if ! awk '
  /^netpredict / {
    rows++; stable = ($0 ~ / dedup=stable /); worst = 1e9;
    for (i = 1; i <= NF; i++) if ($i ~ /^worst_pos=/) {
      v = $i; sub(/^worst_pos=/, "", v); sub(/mm$/, "", v); worst = v + 0;
    }
    if (!stable || worst > 10.0) bad = 1;
  }
  END { exit !(rows == 1 && !bad); }
' "$TMPD/netpredict.log"; then
  say "GATE netpredict snapshot/dedup exceeds 10mm or is unstable"
  fail=1
fi
gate_command netloop "netloop 600" '^netloop n=600 ok hash='
gate_command nethandshake "nethandshake" '^nethandshake cases=21 ok$'

# HOME is a responsive interaction contract, so each required size gets its own
# process and fresh config. homeui masks only the two software-cursor squares while
# comparing the framebuffer, then clicks both the body and UPPER RECEIVER caption.
for size in 1280x720 1280x800 1024x768 640x360; do
  width="${size%x*}"; height="${size#*x}"
  log="$TMPD/homeui-$size.log"
  if run_size "$width" "$height" "homeui" >"$log"; then status=0; else status=$?; fi
  if [ "$status" != 0 ]; then
    say "GATE homeui res=$size exit=$status"
    sed -n '/^homeui /p' "$log"
    fail=1
  fi
  grep -Eq "^homeui res=$size .*hover_delta=0 click_delta=0 state=stable focus=0/6 .*ui_drops=0 ok$" "$log" || {
    say "GATE homeui res=$size missing hover/click/layout/budget success contract"
    [ "$status" != 0 ] || sed -n '/^homeui /p' "$log"
    fail=1
  }
done

# parity: preserve the command's exit status and require its completion census. The
# old grep pipeline ended in `|| true`, so a crash/exit 1 with no MISMATCH text was
# silently green, as was truncated output before the summary.
parity_log="$TMPD/parity.log"
if run "parity" >"$parity_log"; then status=0; else status=$?; fi
[ "$status" = 0 ] || { say "GATE parity exit=$status"; fail=1; }
grep -q 'MISMATCH' "$parity_log" && { say "GATE parity MISMATCH"; fail=1; }
parity_rows="$(awk '/^parity total / { n++ } END { print n + 0 }' "$parity_log")"
[ "$parity_rows" = 1 ] || {
  say "GATE parity summary rows=$parity_rows (want exactly 1)"; fail=1;
}
grep -Eq '^parity total rows=24 live=5 mismatches=0$' "$parity_log" || {
  say "GATE parity missing complete 24-row/5-live zero-mismatch contract"
  fail=1
}

# Budget at 20 and 50 bots hard, including twelve simulation ticks per frame.
# Render a frame first (budget only
# accumulates during scene build) — into a TEMPORARY path, not screenshots/:
# that directory is gitignored, so in a CI checkout the shot failed to open
# (swallowed by this block's own grep). render_frame still ran to completion
# before write_png, so drops/ev_drops were real — but the shot proved nothing
# and depended on an untracked directory. The owned temporary directory is writable.
for bot_count in 20 50; do
CISHOT="$TMPD/budget-$bot_count.png"
budget_log="$TMPD/budget-$bot_count.log"
if run "bots $bot_count; skill hard; fraglimit off; wait 1200; shot $CISHOT; appframe 12 12; wait 300; shot $CISHOT; budget" \
    >"$budget_log"; then status=0; else status=$?; fi
if [ ! -s "$CISHOT" ] || \
    [ "$(od -An -tx1 -N8 "$CISHOT" | tr -d ' \n')" != 89504e470d0a1a0a ] || \
    [ "$(tail -c 12 "$CISHOT" | od -An -tx1 | tr -d ' \n')" != 0000000049454e44ae426082 ]; then
  say "GATE budget missing or incomplete PNG"
  fail=1
fi
rm -f "$CISHOT"
[ "$status" = 0 ] || { say "GATE budget exit=$status"; fail=1; }
budget_rows="$(awk '/^budget / { n++ } END { print n + 0 }' "$budget_log")"
[ "$budget_rows" = 1 ] || {
  say "GATE budget summary rows=$budget_rows (want exactly 1)"; fail=1;
}
bl="$(sed -n '/^budget /p' "$budget_log")"
for k in drops ev_drops world_drops ui_drops; do
  v="$(field "$bl" "$k")"
  [ "${v:-x}" = 0 ] || { say "GATE budget $k=$v (want 0)"; fail=1; }
done
done

# glibc floor — printed, not gated (a rise silently excludes distros)
if command -v objdump >/dev/null 2>&1; then
  floor="$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1)"
  say "info glibc floor: ${floor:-none} (2.38 = Ubuntu 23.10+ / Debian 13+ / SteamOS 3.7+)"
fi

[ "$fail" = 0 ] && say "ci-proofs: OK ($ELF, AR=$AR_NEAR/$AR_CROSS SR=$SR_NEAR/$SR_CROSS)" || say "ci-proofs: FAIL"
exit $fail
