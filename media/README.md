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
tools. The hand exporter keeps those individual frames separate, and its optional
caption pass always starts from the raw PNG with exactly one `drawtext` operation.
At the end of each prompt, keep `screenshots/` limited to a few final new results,
preferably before/after collages. Archive needed raw/earlier evidence under
`build/`, preserving other sessions' work.
