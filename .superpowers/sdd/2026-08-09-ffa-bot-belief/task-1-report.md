# Task 1 report: failing LOS-memory regression proof

## Status

Implemented and committed on `codex/ffa-bot-ai`. This is intentionally a red
checkpoint: production belief-memory behavior was not changed.

## Change

- Added `static void botmemory_proof(void)` beside the existing bot proof
  helpers.
- Staged bot `(0,0,0)`, human `(0,0,-8)`, an open acquisition tick, then the
  non-penetrable wall `min=(-2,0,-5), max=(2,3,-4)` and 300 hidden ticks.
- Added the exact acquisition/old-window assertions and preserved/restored
  bot, player, match, solids, events, human roster, counters, tick, bot RNG,
  frozen/predicting flags, and event peak/drop state.
- Registered `botmemory` in help and `run_script`; the restored proof invokes
  `bothear` in the same process as the state-leak checkpoint.

## Verification

`make build/game` — exit 0, no warnings.

`CFG=$(mktemp -u); ./build/game --seed 1337 --config "$CFG" --do "botmemory"`
— exit 1 as required. Output included:

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 held=0 lose_t=0
```

The build succeeded, acquisition passed, the checkpoint proof passed, and the
non-zero result came from the requested final assertion rather than a build,
crash, or fixture error. `git diff --check` also passed.
