# Task 4 Report: Fixed-budget investigation/search and remembered-point cover

## Status

Implemented in `code/game.c`.

Scope stayed on Task 4: bounded investigation/search and remembered-point cover. No Task 5 diagnostics/target-score output was added.

## Files changed

- `code/game.c`
  - Added `bot_cover_spot_point(const bot_t *, v3 threat_point, v3 *out)`.
  - Kept visible-target `bot_cover_spot()` wrapper.
  - Added deterministic six-candidate search planning through `bot_search_plan_next(bot_t *, const bot_skill_t *)`.
  - Replaced hidden target movement with belief/search-only HOLD, INVESTIGATE, and SEARCH_SWEEP handling.
  - Extended `botmemory` proof for HOLD stillness and non-stale first search anchor.
- `.superpowers/sdd/2026-08-09-ffa-bot-belief/task-4-report.md`
  - This report.

## TDD RED

Test added first: extended `botmemory_proof()` to assert that after LOS loss:

- `OCCLUDED_HOLD` keeps the bot near the loss point.
- first search anchor is not the stale `last_seen`/`belief_pos`.
- live hidden target is sealed away so the assertion is about remembered search data, not current hidden position.

Command:

```sh
make build/game && ./build/game --do botmemory
```

Expected RED output:

```text
mkdir -p build
gcc -std=c23 -O3 -ffast-math -funroll-loops -flto=auto -fno-tree-vectorize -Wall -Wextra -DBUILD_VERSION='"dev"' -DBUILD_COMMIT='"unknown"' code/game.c -o build/game -lEGL -lGL -lX11 -lm
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=0 searchanchor=0 lose_t=300
```

Exit code: `1`.

## Implementation notes

- Candidate order is exactly:
  1. `belief_pos`
  2. `last_seen`
  3. first lateral offset
  4. second lateral offset
  5. close forward offset
  6. long forward offset
- Lateral/forward basis comes from meaningful `last_seen_vel`; otherwise it comes from bot-to-belief direction.
- `search_dir` reverses left/right ordering after lateral selections.
- Candidates are arena-clamped and accepted only if `aabb_fits(candidate, PLAYER_RADIUS, HEIGHT_STAND)`.
- `bot_search_plan_next()` uses no RNG and caps replan LOS checks with `los_calls < 16`; with six candidates it currently issues at most six candidate LOS checks per replan.
- `bot_cover_spot_point()` keeps fixed two-ring/eight-direction cover and checks candidate-to-threat distance before `los_clear`.
- Hidden movement uses `bot_belief_aimpoint(b)` and `search_pos`; it does not call `ent_aimpoint(b->tgt)`.
- Firing remains gated by `engaging = see`.
- Movement still flows through the existing player-parity movement helpers; no direct hidden-path velocity assignment was added.
- Audible investigation branch was not changed.
- MinGW-safe local names used (`close_step`, `long_step`), no `near`/`far` locals.

## GREEN / verification

Focused GREEN:

```sh
make build/game && ./build/game --do botmemory
```

Output:

```text
mkdir -p build
gcc -std=c23 -O3 -ffast-math -funroll-loops -flto=auto -fno-tree-vectorize -Wall -Wextra -DBUILD_VERSION='"dev"' -DBUILD_COMMIT='"unknown"' code/game.c -o build/game -lEGL -lGL -lX11 -lm
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=1 searchanchor=1 lose_t=300
```

Final build verification:

```sh
make build/game build/game.exe
```

Output:

```text
mkdir -p build
gcc -std=c23 -O3 -ffast-math -funroll-loops -flto=auto -fno-tree-vectorize -Wall -Wextra -DBUILD_VERSION='"dev"' -DBUILD_COMMIT='"unknown"' code/game.c -o build/game -lEGL -lGL -lX11 -lm
mkdir -p build
x86_64-w64-mingw32-gcc -std=c23 -O3 -ffast-math -funroll-loops -flto=auto -march=x86-64-v2 -Wall -Wextra -mwindows -DBUILD_VERSION='"dev"' -DBUILD_COMMIT='"unknown"' code/game.c build/game.res.o -o build/game.exe -static -lgdi32 -luser32 -lopengl32 -lwinmm -lole32 -lwinhttp -lws2_32 -lm
```

Final botmemory:

```sh
./build/game --do botmemory
```

Output:

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=1 searchanchor=1 lose_t=300
```

Final tacstat smoke:

```sh
./build/game --seed 1337 --do 'tacstat 600'
```

Output:

```text
phit t=113 part=head d=300 hp=0 by=ROOK
kill t=113 by=ROOK victim=PLAYER hs=1
respawn t=233 ent=PLAYER
kill t=312 by=HAVOC victim=ROOK hs=0
respawn t=432 ent=ROOK
phit t=445 part=body d=26 hp=74 by=VIPER
phit t=462 part=body d=26 hp=48 by=HAVOC
phit t=472 part=body d=26 hp=22 by=HAVOC
phit t=482 part=head d=52 hp=0 by=HAVOC
kill t=482 by=HAVOC victim=PLAYER hs=1
kill t=598 by=VIPER victim=ROOK hs=0
tacstat ticks=600 botticks=1677 slides=4 slide_pct=19.4 jumps=0 crouch_pct=13.5 preaim_pct=3.7 cover_pct=0.0
```

Required headless scenario:

```sh
mkdir -p screenshots
CFG=$(mktemp -u)
./build/game --seed 1337 --config "$CFG" --do 'bots 1; skill normal; botmemory; wait 120; bot; cambot 0 1.2 0; shot screenshots/ffa_ai_search.png'
```

Output:

```text
bothear shot=1 turn=1 move=1 nofire=1 step=1 crouch=0 far=0 priority=shot expiry=patrol cap=1
botmemory acquired=1 loss=1 t60=1 t300=1 reacquired=1 retained=1 hidden_fire=0 crossfire=1 holdstill=1 searchanchor=1 lose_t=300
bot 0 VIPER t=120 pos=(-18.694 0.000 16.054) yaw=-4.64 hp=100 state=combat st=slide cov=0 tgt=PLAYER respawn=0
shot screenshots/ffa_ai_search.png
```

Screenshot inspection:

- Inspected `/workspaces/skill-issue/.worktrees/ffa-bot-ai/screenshots/ffa_ai_search.png` with `view_image`.
- Frame is a close bot camera shot with the bot shouldered/aimed; no visible hidden live-player snap in the captured frame.
- `screenshots/` is ignored by `.gitignore`, so the generated capture remains untracked/ignored.

Diff hygiene:

```sh
git diff --check
```

Output: no output, exit code `0`.

## Self-review

- Exact signatures present: `bot_cover_spot_point(const bot_t *, v3, v3 *)` and `bot_search_plan_next(bot_t *, const bot_skill_t *)`.
- Six candidates are deterministic and ordered; no per-tick RNG in hidden search.
- Search replan LOS budget is bounded below 16 calls.
- Cover helper uses explicit world threat point and the two-ring/eight-direction pattern.
- Hidden path uses only stored belief/search data for aim/movement; `ent_aimpoint(b->tgt)` remains only in visible-target paths/wrapper.
- Ordinary firing gate remains `engaging && see`.
- Movement/weapon parity preserved; audible-channel behavior unchanged.
- No Task 5 diagnostics or target-score output added.
- Requesting-code-review skill was considered, but this Codex session does not expose subagent dispatch tools; this report is prepared for the requested SDD review instead.
