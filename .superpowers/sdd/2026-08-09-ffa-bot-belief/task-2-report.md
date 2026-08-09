# Task 2 Report

Status: complete for Task 2 scope in the isolated `codex/ffa-bot-ai` worktree.

Summary:
- Added `bot_belief_state_t` with the exact four states from the brief.
- Extended `bot_skill_t` and `BOT_SKILL` with the exact per-skill tick budgets:
  - Easy: `360, 42, 216`
  - Normal: `540, 60, 312`
  - Hard: `720, 78, 432`
- Added per-bot belief storage on `struct bot_t`:
  `belief`, `last_seen_vel`, `belief_pos`, `belief_radius`,
  `last_seen_tick`, `search_pos`, `search_index`, `search_dir`, `search_t`,
  `reacquire_count`.
- Initialized the new fields in `bot_spawn` with the brief’s required defaults:
  semantic `BOT_BELIEF_VISIBLE`, `tgt=-1`, zeroed velocity/radius/ticks,
  `search_dir=1`, `search_index=0`.
- Added the pure information-boundary helpers before `bot_tick`:
  `bot_belief_aimpoint`, `bot_belief_clear`, `bot_belief_observe`,
  `bot_belief_predict`.
- Wired the helpers into the existing visible/occluded handling without
  changing the old target deletion policy, so the new belief state is populated
  while `botmemory` still fails red until the later transition task lands.
- Kept gameplay tuning unchanged: existing difficulty behavior, movement, and
  weapon behavior were preserved in `code/game.c`.

Notes on behavior:
- `bot_belief_aimpoint` derives chest height from `belief_pos` and does not
  read `ent_aimpoint(b->tgt)`.
- `bot_belief_observe` records horizontal velocity only, clamps it to
  `g_cfg.mv[MV_RUN]`, sets `belief_radius = 0.20f`, resets `lose_t`, and stores
  `g_tick` in `last_seen_tick`.
- `bot_belief_predict` advances `belief_pos` from the stored horizontal
  velocity and grows `belief_radius` by
  `0.015f + speed * TICK_DT * 0.20f`, capped at `4.5f`.
- Reveal-by-damage now seeds the belief store consistently as an occluded hold
  rather than leaving the new belief fields empty.

Verification:
- `gcc -std=c23 -O2 -Wall -Wextra -Werror -fsyntax-only code/game.c`
  - Result: PASS
- `CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "botmemory"`
  - Result: expected FAIL
  - Output:
    - `bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1`
    - `botmemory acquired=1 held=0 lose_t=0`

Self-review:
- Confirmed the old `BOT_MEMORY_TICKS` deletion path is still active, which is
  why `botmemory` remains red.
- Confirmed no live hidden-target aimpoint reads were introduced in the new
  helpers.
- Confirmed strict C23 syntax-only compile stays clean with `-Wall -Wextra -Werror`.

Files changed:
- `code/game.c`

Commit:
- Recorded after verification in this isolated worktree so the review package
  can reference a stable HEAD.

## Round 1 fix: visible-sample-only observation velocity

Scope addressed:
- Reviewer finding on `code/game.c` around `bot_belief_observe`: reacquisition
  velocity was being derived from `belief_pos`, which can be advanced by
  `bot_belief_predict` during occlusion and seeded on reveal-by-damage.
- Minor reviewer observation was kept in mind: no-target outward behavior
  remains Patrol, and this fix does not expand belief-transition scope into
  Task 3.

Root cause:
- `bot_belief_observe` used `belief_pos` as the previous sample whenever
  `last_seen_tick > 0`.
- `belief_pos` is intentionally not restricted to visible observations: it is
  prediction state during occlusion and is also seeded when damage reveals a
  shooter.
- That meant the first visible sample after an occlusion or damage reveal could
  inherit velocity from hidden-state bookkeeping instead of from two successive
  visible `ent_pos` observations.

Fix:
- Restricted velocity estimation to the case where the prior belief state was
  `BOT_BELIEF_VISIBLE`.
- Switched the velocity delta source from `belief_pos` to `last_seen`, which is
  the last visible feet position recorded by the existing visible-observation
  path.
- Kept the required formulas and fields unchanged:
  - horizontal-only delta
  - `1 / ((g_tick - last_seen_tick) * TICK_DT)` scaling
  - clamp to `g_cfg.mv[MV_RUN]`
  - `belief_pos`, `belief_radius`, `last_seen_tick`, and `lose_t` updates still
    happen exactly in the observation helper
- No hidden aimpoint or hidden target entity reads were introduced.

Regression proof added:
- Added `botmemoryobserve` as a focused deterministic proof in `code/game.c`.
- Proof flow:
  1. observe two visible samples to establish a non-zero horizontal velocity
  2. mark the bot occluded and run `bot_belief_predict`
  3. reacquire visibility at a new position
  4. assert the reacquisition sample records `last_seen_vel == 0`
- Red before fix:
  - `botmemoryobserve speed=3.000`
- Green after fix:
  - `botmemoryobserve speed=0.000`

Verification after fix:
- `gcc -std=c23 -O2 -Wall -Wextra -Werror -fsyntax-only code/game.c`
  - Result: PASS
- `make build/game`
  - Result: PASS
- `CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "botmemoryobserve"`
  - Result: PASS
  - Output: `botmemoryobserve speed=0.000`
- `CFG=$(mktemp -u) && ./build/game --seed 1337 --config "$CFG" --do "botmemory"`
  - Result: expected FAIL
  - Output:
    - `bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1`
    - `botmemory acquired=1 held=0 lose_t=0`

Self-review:
- Confirmed the fix is minimal and isolated to observation velocity semantics
  plus the focused regression proof.
- Confirmed `bot_belief_clear` still resets to the semantic default requested by
  Task 2, while no-target outward behavior remains governed by `tgt == -1` and
  thus Patrol-facing behavior was not changed here.
- Confirmed the Task 3 transition work remains intentionally undone; the
  required `botmemory` proof is still red for that reason.
