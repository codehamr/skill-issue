# Trailer kit

`hero.gif` is the README trailer. `hero.mp4` is the same edit at 60 fps with
captured game sound. `fight.png` and `scope.png` share the trailer's staging.
Every arena, character, movement, tracer and impact comes from the game.
FFmpeg supplies the cuts, speed changes, a restrained colour adjustment and
the closing title. There are no generated or painted gameplay frames.

The loop moves through dunes, forest, marsh, frost, quarry and aurora. It
alternates third-person movement with first-person fighting: slide, jump,
wet-ground duel, snow pursuit, scoped shot, the same impact from outside,
live return fire, then the aurora reveal. The final half-second is the run
immediately before the opening slide. The full 16:9 frame keeps the terrain
and skyline visible.

## Rebuild

Requires `build/game`, Python 3, FFmpeg/FFprobe with libx264 and drawtext,
Gifsicle for palette-preserving GIF assembly, and DejaVu Sans for the closing
title. On Debian the additional tools are `ffmpeg gifsicle fonts-dejavu-core`. ImageMagick is optional for lossless PNG
compression. No external Python packages are needed.

```sh
make build/game
python3 media/media.py check
python3 media/media.py                 # all captures, stills, GIF and MP4
./tools/split-check.sh
```

Confirm that the compiler ran after engine changes; timestamps can be skewed.
The renderer uses a fresh config for every take. Source footage is 120 fps,
1280×720, with a 1600×900 optic take. The default GIF is 832×468 at 20 fps. Each clip gets its own 160-colour
palette; repeated clip sections share that palette, including the loop seam.
This keeps aurora gradients from competing with snow and sand for colours.
`gif_bayer` controls ordered dithering; smaller values soften colour steps
but increase texture and file size. `gif_lossy: 40` adds a reviewed Gifsicle
compression pass; set it to `0` for lossless assembly of the palettized frames.
Caches and event logs live under `media/.cache/`; review captures live under
ignored `screenshots/`. The MP4 is also ignored by Git.

## Author and review

- `shots.py` stages cameras, actors and actions. Coordinates are world metres;
  a timeline frame is one 120 Hz simulation tick.
- `scene.json` defines cuts in beats at 120 BPM. `speed` stretches duration:
  `2` is half-speed; `in_f` is inclusive and the computed end is exclusive.
  Omitting `in_f` continues the preceding section of the same clip.
- `media.py` captures, validates and assembles. `biomes.py` independently
  generates the wider environment gallery.

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

`--skip-render` is for changing the edit. It rejects caches whose binary,
script, lens, resolution or media hashes differ. Impossible cut ranges are
errors instead of silent clamps. Re-render only the changed takes, then
assemble again. Source recipes shared by the optic and external impact keep
the kill synchronized.

## Review findings carried into this edit

Three independent agent reviews covered direction, environment scouting and
capture correctness. Framing probes were revised before full capture, and
the assembled GIF is the final review surface.

| Axis | Rule carried into the recipes |
| --- | --- |
| Hook | Start on the slide; keep the moving figure lit and visible. |
| Biomes | Preserve trees, mountains, wet reflections and aurora at GIF size. |
| Readable action | Alternate body-scale movement, optics and close impacts. |
| Pacing | End AR shots before enemy respawn; remove idle recovery. |
| Lighting | Keep the dunes runner out of the perimeter wall's shadow. |
| Loop | End on source frame 59, resume the same camera/action at frame 60. |
| Palette | Use clip-specific palettes; share colours across repeated shots to keep the loop stable. |
| Fidelity | Use the real game effects and recorded sound; preserve source hashes. |

`review` writes `screenshots/trailer-review/final-sheet.png` and a machine-readable
`final-report.json` with planned and decoded frame counts, dimensions and size.

Judge the exported GIF at its actual display size. Large contact sheets are
useful for occlusion and pose checks, but can hide palette banding, unreadable
targets, flashes and awkward pauses. Keep a pre-change GIF under `screenshots/`
for visual A/B comparison rather than overwriting the only reference.
