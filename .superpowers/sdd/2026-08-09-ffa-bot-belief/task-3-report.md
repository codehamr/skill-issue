# Task 3 Report: Belief transitions replace abrupt perception loss

## Scope

Implemented only Task 3 in `code/game.c`:

- Extended `botmemory_proof` to assert `VISIBLE -> OCCLUDED_HOLD -> INVESTIGATE -> VISIBLE` across LOS loss, 60 hidden ticks, 300 hidden ticks, and wall-removal reacquisition.
- Kept hidden firing gated off by the production `engaging = b->tgt >= 0 && see` path.
- Replaced the old hidden close-range `<1.2f` target deletion with belief-state retention until target death or `sk->memory_ticks`.
- Replaced lost-sight pre-aim/peek coordinates with `bot_belief_aimpoint(b)`.
- Added per-bot `threat_src`, bounded `threat_t`, and `threat_score` fields for stable damage reveal.
- Routed bot damage reveal through one helper that refreshes current-target memory and resolves repeated crossfire by deterministic evidence score, then lower entity id as the same-score tie-break.
- Did not implement Task 4 search planner; hold expiry transitions to `BOT_BELIEF_INVESTIGATE` and may remain there.

## TDD RED evidence

After changing only `botmemory_proof`, before production implementation:

```sh
make build/game && CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "botmemory"
```

Result: expected failure, exit 1.

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=0 t300=0 reacquired=0 retained=0 hidden_fire=0 lose_t=0
```

The failure proved the new assertions caught the missing hold expiry, old memory-window clear, and failed reacquisition while preserving the no-hidden-fire condition.

## GREEN evidence

Focused proof after implementation:

```sh
make build/game && CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "botmemory"
```

Result: pass, exit 0.

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 lose_t=300
```

Strict compile:

```sh
gcc -std=c23 -O2 -Wall -Wextra -Werror -fsyntax-only code/game.c
```

Result: pass, exit 0, no output.

Relevant existing proofs:

```sh
CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "bothear; botweapon; botmemoryobserve"
```

Result: pass, exit 0.

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botweapon empty=sr far=sr near=ar moving=ar heard=ar raise=1 cooldown=kept hysteresis=1
botmemoryobserve speed=0.000
```

## Self-review

- No hidden fire: trigger path remains gated by `engaging`, and `engaging` still requires current authoritative `see`.
- No premature clear: hidden target no longer clears on `<1.2f`; clear now happens on target death or `lose_t >= sk->memory_ticks`.
- No hidden live target reads in lost-sight path: lost-sight pre-aim, peek probes, and movement derive from `bot_belief_aimpoint(b)`.
- Deterministic focus: damage reveal stores `threat_src`, bounded `threat_t`, and `threat_score`; same-score crossfire has a stable lower-entity-id tie-break and is covered by `crossfire=1`.
- Proof restoration: `botmemory_proof` now saves/restores the full bot array plus prior globals/events/RNG state, and still runs `bothear_proof()` after restoration.
