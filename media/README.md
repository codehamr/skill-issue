# Trailer kit

The local capture entry point is [`screenshots/index.html`](../screenshots/index.html).
The [procedural environment and weather gallery](../screenshots/2026-09-07-procedural-weather/index.html)
keeps its capture recipes and evidence with the images. Every arena is generated
procedurally. Weather is selected automatically from the shared map seed; there
is no weather menu or configuration override. Reusing a seed reproduces the
weather preset, while the simulation tick determines its current motion and
lightning phase.

Run `python3 media/weather.py` after building the game to capture ten 1600×900
images, including all five natural weather presets, a three-frame lightning
sequence and Match Settings. `--only 07-lightning` repeats a single view from
the same build. The generator validates the weather from the game's map dump,
checks the build's source hashes and preserves PNGs, scripts, logs and a manifest.

`hero.gif` is the README trailer. `hero.mp4` is the same edit at 60 fps with
captured game sound. `fight.png` and `scope.png` share the trailer's staging.
Every arena, character, movement, tracer and impact comes from the game.
FFmpeg supplies the cuts, speed changes and light sharpening. The trailer
contains no titles or text overlays. The game's FILMIC look supplies the contrast, cool shadows, warm
highlights, glow and grain; the edit adds no second colour grade.
There are no generated or painted gameplay frames.

The loop alternates a low sand slide, an imperfect wet-ground burst, a
jump diagonally through incoming rounds, live strafing counterfire, a corrected
sniper shot and its external impact, a magazine seat, and a night slide into
hipfire. Short landing and approach inserts return to the opening slide.
The full 16:9 frame keeps the terrain and skyline visible.

## Rebuild

Requires `build/game`, Python 3, FFmpeg/FFprobe with libx264, and
Gifsicle for palette-preserving GIF assembly. On Debian the additional tools
are `ffmpeg gifsicle`. ImageMagick is optional for lossless PNG
compression. No external Python packages are needed.

```sh
make build/game
python3 media/media.py check
python3 media/media.py                 # all captures, stills, GIF and MP4
./tools/split-check.sh
```

The prerequisite check verifies the completed native build transaction, current
source and tuning content, required tools and valid recipes. It accepts absent
or stale clip caches because a normal build regenerates them; cached edits
require every recorded content hash to match.
The renderer uses a fresh config for every take. Source footage is 120 fps,
1280×720, with a 1600×900 optic take. The default GIF is 832×468 at 20 fps. Each clip gets its own 160-colour
palette; repeated clip sections share that palette, including the loop seam.
This keeps aurora gradients from competing with snow and sand for colours.
`gif_bayer` controls ordered dithering; smaller values soften colour steps
but increase texture and file size. `gif_lossy: 40` adds a reviewed Gifsicle
compression pass; set it to `0` for lossless assembly of the palettized frames.
Caches and event logs live under `media/.cache/`; review captures live under
ignored `screenshots/`. The MP4 is also ignored by Git. It retains H.264 slow CRF 18 at 60 fps,
AAC stereo at 192 kbit/s and faststart. Both requested final outputs are encoded
and checked privately before their publication group replaces the previous pair.

## Author and review

- `shots.py` stages cameras, actors and actions. Coordinates are world metres;
  a timeline frame is one 120 Hz simulation tick.
- `scene.json` defines cuts in beats at 120 BPM. `speed` stretches duration:
  `2` is half-speed; `in_f` is inclusive and the computed end is exclusive.
  Omitting `in_f` continues the preceding section of the same clip.
- `media.py` captures, validates and assembles; `verify.py` owns decoded
  waveform, frame and synchronization checks. `biomes.py` independently
  generates the wider environment gallery.
- `boundaries.py` captures the forest quarry at player height across natural
  clear, mist, rain and sunshower seeds. `--before build/SESSION/game-before`
  adds matching pre-change views. PNGs, recipes, budgets and hashes go to
  `screenshots/forest-boundaries/`; each image uses a fresh config.

```sh
python3 media/media.py list
python3 media/media.py probe dunes_slide 8
python3 media/media.py probe frost_impact 118 120 123 130 145 160 180
python3 media/media.py render dunes_slide
python3 media/media.py gif --skip-render
python3 media/media.py mp4 --skip-render
python3 media/media.py review
python3 media/media.py stills
```

Inspect `media/.cache/clips/KEY/probe.png` and its `probe.log`, then the rendered
`sheet.png` and `events.log`. A hit effect alone does not prove return fire:
look for `phit`, `hit` and `kill` events. Fire with a captured `+fire` tick and
then `-fire`; `tap fire` advances outside capture and can lose its sound.
Probe frames advance before drawing, matching captured frame indices.

Trailer takes use finite authored mouse gestures through `look` / `pan`.
They acquire the torso, overrun, correct after movement changes, and leave
physical recoil and spread intact. The source gate rejects `aimbot`, active
`filmtrack`, and text overlays in trailer recipes. Setup-only `aim` establishes
the initial view before recording; there is no target lock during capture.

The harness's separate `hold_aim` utility remains available for controlled
tracking proofs. `filmtrackproof` tests its motion, occlusion and life handling;
`filmcueproof` checks reload sound cursor timing. Event logging remains enabled
for manually aimed footage so misses, body hits, incoming fire and actual kills
can be checked independently of the pictures.

A take keeps one continuous mixer sample clock across all capture segments.
`cues.jsonl` records event identity, authored delays, actual source/dry onsets
and reached reload milestones; an unreached milestone stays null. `tail.wav`
drains the existing voices without advancing simulation and is appended to
the source PCM. `events.log` ties these records to actual source events,
rendered reload poses, face/bore/hit projections and emitted sight centres.
For buffered events, `source_tick` is their local admission tick; `script_tick`
is the harness drain step. Local network admission does not recover the
original server timestamp. Actual source/dry onsets keep the continuous mixer
sample clock, including any remaining confirmation delay.

`--skip-render` is for changing the edit. It rejects caches whose binary,
script, lens, resolution or media hashes differ. Impossible cut ranges are
errors instead of silent clamps. Re-render only the changed takes, then
assemble again. Source recipes shared by the optic and external impact keep
the kill synchronized.

## Review the final edit

Review framing probes before full capture, then inspect both exported outputs.
Use independent motion/aim, edit/readability and audiovisual reviews. Keep their
findings with the output fingerprints; prior reviews do not certify a new build.

| Axis | Rule carried into the recipes |
| --- | --- |
| Hook | Start on the slide; keep the moving figure lit and visible. |
| Biomes | Preserve trees, mountains, wet reflections and aurora at GIF size. |
| Readable action | Alternate body-scale movement, corrected torso aim, optics and close impacts. |
| Human motion | Finite mouse gestures, visible corrections and diagonal crossings; no target locks. |
| No text | No titles or slogans; crop the optic HUD outside the exported frame. |
| Pacing | End before respawns and empty frames; retain only the readable part of the impact. |
| Lighting | Keep the dunes runner out of the perimeter wall's shadow. |
| Loop | End on source frame 59, resume the same camera/action at frame 60. |
| Palette | Use clip-specific palettes; share colours across repeated shots to keep the loop stable. |
| Fidelity | Use the real game effects and recorded sound; preserve source hashes. |

`review` writes `screenshots/trailer-review/final-sheet.png` and a machine-readable
`final-report.json` with planned and decoded frame counts, dimensions and size,
plus the delivered MP4 stream and synchronization results. The MP4 checks
compare decoded audio with the retained pre-AAC master and decoded action
frames with the pre-encode picture witness. Required cue witnesses cannot be
missing or unmeasurable. Limits are one 120 Hz source tick for source onset
and one delivered 60 fps frame for additional edit/encode drift; propagation,
authored confirmation/reload delays, retiming and AAC padding are explicit.
These are mixer/output-file witnesses, not physical speaker latency.

Judge the exported GIF at its actual display size. Large contact sheets are
useful for occlusion and pose checks, but can hide palette banding, unreadable
targets, flashes and awkward pauses. Keep a pre-change GIF under `screenshots/`
for visual A/B comparison rather than overwriting the only reference.
