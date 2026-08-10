# FFA Bot Belief Final Fix Report — Round 1

Date: 2026-08-09
Worktree: `/workspaces/skill-issue/.worktrees/ffa-bot-ai`
Branch: `codex/ffa-bot-ai`
Starting commit: `94a30da` (`fix netloop portable proof hash`)

## Status

All four Important final-review findings are fixed with proof-first coverage. The
compatible diagnostic cleanup was also applied by changing only the appended
belief key from `state=` to `belief_state=`; the original `state=` field remains
in its original position with its original meaning.

## TDD RED evidence

The first edit changed only `botmemory_proof`: it added a visible incumbent
outside acquisition FOV, a seeded stronger threat plus both crossfire orders,
successive search replans, and nested-proof state equality. Production code was
unchanged for this run.

```sh
make build/game
CFG_RED=$(mktemp -u)
./build/game --seed 1337 --config "$CFG_RED" --do 'botmemory'
```

Result: exit 1 for the intended assertions (compile succeeded):

```text
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=0 holdstill=1 searchanchor=1 searchprogress=0 searchmove=1 decoyhold=1 incumbent=0 acquirefov=1 recency=0 proofrestore=0 lose_t=300
```

This independently exposed all four findings while the controls stayed valid:
hidden firing remained zero, search movement was real, the hidden decoy hold
remained stable, and targetless acquisition still obeyed FOV.

The existing caller-visible leak was reproduced before the fix:

```sh
./build/game --seed 1337 --config "$(mktemp -u)" --do 'budget; botmemory; budget'
```

```text
budget ... ev_peak=0 ... ev_drops=0 ...
botmemory ...
budget ... ev_peak=5 ... ev_drops=0 ...
```

The diagnostic ambiguity also had a shell-level RED check:

```text
diagnostic rows=3 state_keys=6 want_equal
```

## Production changes

### 1. Visible incumbent FOV bypass

- The acquisition cone now rejects only entities other than `b->tgt`.
- A currently visible incumbent is included in the normal score, epsilon,
  distance/entity tie-break, and hold-bonus arbitration.
- A targetless bot and every genuinely new candidate still require
  `align > BOT_FOV_COS`.

### 2. Monotonic threat evidence

- Removed the unconditional `b->tgt == src` evidence replacement.
- Evidence now orders by score first, remaining bounded timer as recency/age
  second, and entity ID for equal evidence in the same tick.
- Weaker repeated hits can refresh the current target's damage-revealed memory
  point but cannot lower or refresh the stronger threat score/timer.
- `BOT_THREAT_TICKS` remains the upper bound and no observation beyond the
  existing damage-reveal source position was added.

### 3. Search-anchor progression

- Added a six-bit per-bot visited mask, reset with search/focus state.
- Valid physical duplicates are collapsed deterministically.
- A sweep excludes visited anchors, consumes every valid anchor before wrap,
  and reverses left/right ordering only when the finite set wraps.
- Score ties use `BOT_SEARCH_EPS` and lower candidate index.
- Each replan still creates exactly the six specified candidates and performs
  at most one LOS call per eligible candidate (at most six, below the limit 16).
- No RNG was added to planning or per-tick search.

### 4. Proof restoration

- `bothear_proof` now saves/restores `g_ev_peak`, `g_ev_drops`, and
  `g_predicting` in addition to its existing event buffer/count, simulation,
  collision, entity, match, tick, and RNG restoration.
- `botmemory_proof` asserts nested before/after equality for the event counters
  and prediction flag.

### Diagnostic cleanup

- The appended bot field is now `belief_state=`. The original combat/dead/
  patrol `state=` field order and meaning are unchanged.

## GREEN evidence

### Focused proofs and strict native compile

```sh
./build/game --seed 1337 --config "$(mktemp -u)" --do \
  'botmemory; bothear; botweapon'
gcc -std=c23 -O2 -Wall -Wextra -Werror -fsyntax-only code/game.c
```

Result: exit 0. Key output:

```text
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=1 searchanchor=1 searchprogress=1 searchwrap=1 searchmove=1 decoyhold=1 incumbent=1 acquirefov=1 equalid=1 recency=1 proofrestore=1 lose_t=300
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botweapon empty=sr far=sr near=ar moving=ar heard=ar raise=1 cooldown=kept hysteresis=1
```

The search proof uses six distinct valid anchors and confirms that only the
seventh replan wraps. Threat coverage includes the pre-existing equal-score,
empty-store pair in both orders plus the required stronger seeded incumbent,
weaker unequal hits in both orders, and a newer equal-score event.

### Budget leak sequence

```sh
./build/game --seed 1337 --config "$(mktemp -u)" --do \
  'budget; botmemory; budget'
```

Both budget rows reported:

```text
ev_peak=0 ev_max=128 ev_drops=0
```

### Diagnostic key check

The final command output was counted per bot row using exact leading-space keys:

```text
diagnostic rows=3 state_keys=3 belief_state_keys=3
```

### Deterministic repeat and raw netloop

Two fresh-config runs of this exact script were compared as complete stdout:

```sh
botmemory; bothear; botweapon; bots 20; skill hard; wait 1200;
tacstat 600; match; netloop 300; budget
```

```text
deterministic_repeat=byte_equal bytes=6510 checksum=2589636884
netloop n=300 ok hash=981848b7
budget ... ev_peak=11 ev_max=128 ev_drops=0 ...
```

No `MISMATCH` occurred; raw per-tick netloop hashing was not altered.

### Sanitizers

```sh
make build/game-asan
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
./build/game-asan --seed 1337 --config "$(mktemp -u)" --do \
  'botmemory; bothear; botweapon; bots 20; skill hard; wait 2400; budget'
```

Result: exit 0; no ASan, UBSan, float-cast-overflow, float-divide-by-zero, or
leak report. Final budget had `ev_drops=0`.

### GCC/MinGW/build and cross-architecture gates

```sh
x86_64-w64-mingw32-gcc -std=c23 -O2 -Wall -Wextra -Werror \
  -fsyntax-only code/game.c
make build/game.exe
make build/game-x86_64
```

All passed. Native and qemu-x86_64 outputs were byte-equal for the required
isolated cases:

```text
cross_arch case="botmemory" byte_equal checksum=129993100
cross_arch case="tacstat 1200; match" byte_equal checksum=1372202552
cross_arch case="netloop 300" byte_equal checksum=1278054780
netloop n=300 ok hash=152acd2d
mingw_strict=PASS windows_build=PASS x86_64_build=PASS
```

### Release proof gate

```sh
tools/ci-proofs.sh ./build/game
```

```text
info glibc floor: GLIBC_2.38 (2.38 = Ubuntu 23.10+ / Debian 13+ / SteamOS 3.7+)
ci-proofs: OK (b700, near=14 14 26 21)
```

The full focused battery also reported parity `mismatches=0`, scene/event
`drops=0`, and `ev_drops=0`.

## Files changed

- `code/game.c` — focused proofs plus the four bounded fixes and diagnostic key.
- `.superpowers/sdd/2026-08-09-ffa-bot-belief/final-fix-report.md` — this report.

No screenshot asset was added or retained.

## Self-review

- Hidden firing still derives from `engaging = b->tgt >= 0 && see`.
- The only `ent_aimpoint(b->tgt)` uses are the visible engagement aim path and
  its visible-target cover wrapper. Hidden hold/investigate/search/cover use
  `bot_belief_aimpoint` and stored search points.
- `BOT_FOV_COS`, hold bonus, movement, weapon, `MAX_EVENTS`, scene, particle,
  and UI constants are unchanged.
- Search planning has no RNG and no more than six LOS calls per replan.
- Threat replacement is monotonic, recency-aware, ID-stable, and timer-bounded.
- Proof restoration covers event storage/count/peak/drop, prediction mode,
  staged entities/world/match, tick, and bot RNG.
- `git diff --check` passed and no unrelated tracked file was modified.

An auxiliary read-only Codex review was also launched against base `94a30da`
with the four-finding contract. It inspected the plan and relevant production/
proof paths but did not return a verdict after three bounded 30-second polls;
per the user instruction for hanging commands it was interrupted and recorded,
with no edits or findings emitted. The checks above were run independently and
do not rely on that incomplete review.

## Non-blocking exploratory observation

A combined exploratory cross-arch script that first ran a long 1,800-tick
20-bot scenario and then `netloop 300` produced identical printed gameplay,
budgets, and `netloop ... ok` status, but its final portable report hash differed
(`48edfa9c` native vs `ad9e40d7` x86_64). The required isolated cross-arch cases
above are byte-equal, and raw within-architecture per-tick netloop comparison
remained clean. No netloop normalization or out-of-scope tuning was introduced.
