# Game media

`make media` verifies/builds the native Linux game, captures every take from
scratch, and replaces `hero.gif`, `hero.mp4`, `fight.png` and `scope.png` together
after verification. It prints total wall time, including the build check, on
success or failure. Equal game inputs reuse the verified binary; captures are
always fresh. Existing final assets survive a failed capture or encode.

Requires the Linux game toolchain, Python 3, FFmpeg/FFprobe with libx264 and
libx264rgb, and Gifsicle. No external Python packages are needed. ImageMagick is
optional for PNG compression. The comparison gallery additionally uses FFmpeg's
drawtext filter and a system font.

## The edit

A 30-second loop moves from a sunset slide into rain and forest combat, live
counterfire, a corrected sniper shot and its external impact. A forest run and
jump under a sunshower open the frame again, then a desert flank, storm fight and
winter night rush lead back into the opening slide. All five biomes appear;
weather changes at scene cuts. No titles, slogans or painted gameplay frames.

Actors use the game's actual movement, collision, recoil, damage and audio.
Mouse gestures are finite, with acquisition, misses and corrections. Live bots
supply return fire in the quarry and storm scenes. No target lock runs during
capture. Setup-only `aim` establishes the view before a take.

## Files and delivery

- `build.py` owns the timed fresh build invoked by Make.
- `shots.py` owns actors, inputs and cameras; one frame is a 120 Hz sim tick.
- `scene.json` owns cuts at 120 BPM; `in_f` is inclusive, end is exclusive.
  `speed` stretches time: 2 is half speed. Omitted `in_f` continues that clip.
- `media.py` captures, edits and publishes; `verify.py` checks decoded output
  and audiovisual timing. `gallery.py` produces explicit arena comparisons.

Capture uses 1920×1080 at 120 fps, with a 2880×1620 scope take so its crop retains
Full HD detail. A bounded FIFO in the system temporary directory streams pixels
directly to lossless RGB H.264. There are no raw video files filling the checkout.
Consecutive capture segments share one game mixer clock; a measured quiet tail
completes each take.

`hero.mp4` is always 1920×1080, 60 fps, H.264/yuv420p, 24 Mbit/s CBR with a
48 Mbit buffer, AAC stereo at 48 kHz with a 320 kbit/s target, and faststart. Verification rejects
video below 20 Mbit/s or audio below 256 kbit/s, missing/empty/clipped sound,
incorrect frame cadence, and missing or shifted required sound cues.
`hero.gif` derives from the same completed edit at 832×468/20 fps, with a stable
160-colour palette per clip and Gifsicle compression. The high-resolution edit
is assembled once. The two PNGs use the same staging as the trailer.

## Author and review

```sh
make media
python3 media/media.py list
python3 media/media.py probe frost_scope 118 120 123 145
python3 media/media.py render frost_scope frost_impact
python3 media/media.py all --skip-render
python3 media/media.py review
python3 media/gallery.py --before build/SESSION/game-before
python3 tools/hand-captures.py --before build/SESSION/game-before
python3 tools/hand-captures.py --suite --weapon both --before build/SESSION/game-before
```

`check` verifies the native build transaction, tools and recipes; it reports
stale clip caches without blocking regeneration. `--skip-render` requires matching
binary, script, resolution, footage and sound hashes. Impossible cut ranges fail.
`stills` regenerates just the PNGs; `gif` and `mp4` select one trailer output.

Inspect `.cache/clips/KEY/probe.png`, `sheet.png` and their logs. `events.log` and
`cues.jsonl` retain actual fire, hits, deaths and mixer onsets; a visual impact
alone does not prove return fire. Fire inside capture with `+fire` followed by
`-fire`, since `tap fire` advances outside it. The source cue tolerance is one
120 Hz tick; additional encoded drift is limited to one delivered 60 Hz frame.
The checks compare decoded audio/pictures against retained pre-encode witnesses.
These are file/mixer checks, not physical speaker latency measurements.

`review` writes `.cache/review/final-sheet.png` and `final-report.json`. Inspect
the exported GIF at README size for readability, palette banding and loop pacing.
The MP4 and cache are ignored by Git. Neither production nor review reads
`screenshots/`; source inputs are explicit recipes and named clip files.

`gallery.py` retains raw before/after PNGs, recipes, logs and hashes in a unique
`build/media-gallery-*` directory and exports only `screenshots/arenas.png`.
The hand capture `--suite` records finger attachments, both sides of the trigger
contact and support fingers for either or both weapons. Thumb and normal gameplay
views remain selectable through `--views`. Cameras use fixed weapon-space
stations, so changing the wrist or hand mesh cannot move the comparison camera.
Each weapon/pose starts in a fresh process with the same seed and copied profile;
two independent takes must have identical PNG hashes before any export is published.
`--poses hip ads reload`, `--views support trigger` and `--repeat 3` select the matrix.
The default suite exports four labelled comparisons, with BEFORE/AFTER columns and
AR/SR rows. Complete game frames are only arranged and captioned, never retouched.
Executables, input profiles, recipes, budgets, raw frames, hashes and a JSON manifest
remain in `build/hand-suite-*`. Existing same-name exports are archived there before
replacement. Use `--output-dir build/SESSION/iteration-02` for intermediate reviews.
For finger work, use `--views fingers trigger-contact trigger-reverse support-fingers`.
These four fixed cameras expose the palm attachments, both sides of the index/trigger
contact and all four support fingers. `--poses hip ads reload` repeats the same views
in each action state. The `vmfinger` proof builds twelve canonical FP poses (both
weapons, hip, ADS and four reload phases) and checks all 96 finger paths, real blade
triangle crossings, blade containment, guard clearance and a surface contact gap
of at most 1.5 mm. It is part of `tools/ci-proofs.sh`; the larger `vmtrig` matrix also
rejects blade crossings independently of legacy contact-interval relaxations.

`vmbench 1000` reports CPU time for building the frozen viewmodel and its total,
hand and thumb triangle counts. It does not time simulation or GPU rendering;
use matched build flags and several alternating before/after runs for comparisons.

`tools/hand-captures.py` exports separate `000-before.png`, `000-after.png`,
`001-before-closeup.png` and `001-after-closeup.png` frames using the same seed,
profile and pose. The closeup moves the actual viewmodel camera; use `--closeup`
to supply a `vmorbit` or `vmbore` command. `--tag iteration-02` captures just that
version. `--config-source PATH` pins an existing profile; otherwise the oldest
binary supplies the shared defaults. `--captions` adds one caption band to each
raw frame. Captures, recipes, logs and manifests stay in `build/hand-captures-*`.
Every export validates the expected dimensions and a complete single PNG;
exports are never inputs, so rerunning cannot stack frames or labels.
The harness `shot` command renders once and writes one framebuffer. It does not
add captions or construct contact sheets; those operations belong to export
tools. The hand exporter retains those individual frames as evidence; each caption
pass starts from the raw PNG with exactly one `drawtext` operation per frame.
At the end of each prompt, keep `screenshots/` limited to a few final new results,
preferably before/after collages. Archive needed raw/earlier evidence under
`build/`, preserving other sessions' work.
