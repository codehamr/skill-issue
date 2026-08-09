# Task 5 Report: FFA target arbitration and diagnostics

## Summary

Implemented deterministic FFA target arbitration with explicit score epsilon/tie rules, recent-threat evidence, current-target hold bonus, distance and FOV scoring, and a hidden-target hysteresis gate. Focus changes now invalidate stale cover/search tactical state before belief is reseeded.

Extended diagnostics:

- `bot` keeps the existing fields/order and appends `state`, `lose`, `belief`, `rad`, `search`, and `reacq`.
- `tacstat` keeps existing fields and appends hold/investigate/search/visible/reacquisition counters plus `memory_pct`, `investigate_pct`, `search_pct`, `reacquired`, and `searchanchors`.
- `--help` describes the expanded bot/tacstat diagnostics.

## RED evidence

Added a focused `botmemory` proof assertion staging a visible decoy while bot 0 is holding a hidden remembered human target. Before implementation, the old `!see` scan switch discarded the hidden target.

Command:

```sh
make build/game && ./build/game --seed 1337 --config /tmp/ffa-red.cfg --do 'botmemory'
```

Result: exit 1.

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=0 t300=0 reacquired=0 retained=0 hidden_fire=0 crossfire=1 holdstill=0 searchanchor=0 decoyhold=0 lose_t=71
```

The intended failing field was `decoyhold=0`; the downstream memory/search fields also failed because the bot switched away from the hidden remembered target.

## GREEN evidence

Focused proof after implementation:

```sh
make build/game && ./build/game --seed 1337 --config /tmp/ffa-green.cfg --do 'botmemory'
```

Result: exit 0.

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=1 searchanchor=1 decoyhold=1 lose_t=300
```

## Final verification

Strict Linux + MinGW compile:

```sh
make all
```

Result: exit 0, no warnings.

Individual proofs:

```sh
./build/game --seed 1337 --config /tmp/ffa-proof-bothear.cfg --do 'bothear'
./build/game --seed 1337 --config /tmp/ffa-proof-botweapon.cfg --do 'botweapon'
./build/game --seed 1337 --config /tmp/ffa-proof-botmemory.cfg --do 'botmemory'
```

All exited 0.

Combined diagnostic script:

```sh
./build/game --seed 1337 --config /tmp/ffa-proof-diag.cfg --do 'bothear; botweapon; bots 1; skill normal; botmemory; tacstat 2400; match'
```

Result: exit 0. Key output:

```text
tacstat ticks=2400 botticks=2400 slides=2 slide_pct=7.5 jumps=0 crouch_pct=0.0 preaim_pct=31.7 cover_pct=0.0 hold=247 investigate=87 search=427 visible=305 reacquisition=3 memory_pct=31.7 investigate_pct=3.6 search_pct=17.8 reacquired=3 searchanchors=17
```

`memory_pct` and `search_pct` are non-zero, and search-anchor transitions are counted separately as `searchanchors=17`.

Bot diagnostic smoke:

```sh
./build/game --seed 1337 --config /tmp/ffa-botvisible.cfg --do 'bots 1; skill normal; warp -25.5 0 15.8; wait 20; bot'
```

Result includes appended belief diagnostics and stable label:

```text
state=visible lose=0 belief=(-23.650 0.000 15.800) rad=0.20 search=(-23.650 0.000 15.800) reacq=0
```

Deterministic repeat comparison:

```sh
./build/game --seed 1337 --config /tmp/ffa-repeat-a.cfg --do 'bothear; botweapon; bots 1; skill normal; botmemory; tacstat 2400; match' > /tmp/ffa-repeat-a.txt
./build/game --seed 1337 --config /tmp/ffa-repeat-b.cfg --do 'bothear; botweapon; bots 1; skill normal; botmemory; tacstat 2400; match' > /tmp/ffa-repeat-b.txt
cmp -s /tmp/ffa-repeat-a.txt /tmp/ffa-repeat-b.txt
```

Result: `cmp` exit 0, byte-identical repeat output.

## Scope notes

Stayed within Task 5: target arbitration/hysteresis, tactical invalidation on focus change, `bot`/`tacstat`/help diagnostics, and proof coverage. Did not change movement constants, weapon behavior, event contracts, search planner geometry, or Task 6 tuning/validation.
