<p align="center">
  <img src="media/hero.gif" alt="arena FPS trailer: sand slide, reactive firefights, diagonal jump, sniper impact and night rush" width="832">
</p>

<h1 align="center">skill-issue</h1>

<p align="center">
  <b>Free arena FPS. A new procedural map every match, so nobody has map knowledge.<br>
  Two guns, pure movement and gunplay, 120 Hz servers. If you lose: skill issue.</b>
</p>

<p align="center">
  <a href="https://github.com/codehamr/skill-issue/releases/latest/download/skill-issue.exe"><b>Download for Windows</b></a>
  &nbsp;·&nbsp;
  <a href="https://github.com/codehamr/skill-issue/releases/latest/download/skill-issue-linux"><b>Linux / Steam Deck</b></a><br>
  One executable. No installer, no launcher, no account.<br>
  <sub>Windows warns you because the game is new and unsigned. Click More info, then Run anyway.
  On Linux run <code>chmod +x</code> first.
  Details on the <a href="https://github.com/codehamr/skill-issue/releases/latest">release page</a>.</sub>
</p>

<p align="center">
  <img src="media/fight.png" alt="carbine counterfire in a procedural industrial arena" width="49%">
  <img src="media/scope.png" alt="a moving opponent in the sniper crosshair on a snowy arena" width="49%">
</p>

## The game

**A new arena every match.** Starting the next match generates a map from a new seed.
Nobody can build map knowledge. That is the point. Nothing ships as an asset either.
The arena, both weapons, the soldiers and every sound are built in code instead of
loaded from a file. What carries over between matches is your aim and your movement.

**Different places, different light.** Desert ruins, wet industrial yards, overgrown
clearings and snowfields share the same angular style. Every arena is square, with
low perimeter walls and clear routes for slides and jumps. Distant mountains,
open skies and the sunset stay visible above the walls.
Night arenas mix stars, occasional shooting stars and moonlight with warm lamps,
cool floodlights and red watch lights; winter nights bring aurora. Broken masonry
has real stepped openings, and forest trunks are solid. Geometry, materials,
lighting and weather are generated in code.
Match Settings offers biome, DAY / SUNSET / NIGHT, and weather, each also offering
RANDOM. Choices apply to the next local arena and persist. Random favors daylight,
clear skies and dry deserts, with occasional nights; online, the server owns the
shared environment.

**Two guns, everyone gets both.** An automatic rifle and a bolt sniper that kills with a
single body hit. Both are hitscan. No loadouts, no battle pass. Rounds cross up to 1.2 m
of material at *full* damage. Most cover in the arena is therefore a timing problem rather
than a wall. Anything thicker still has corners. The arena's outer shell is the one thing
nothing goes through.

**Movement you can get good at.** Air control lets you carve a jump. A slide carries real
speed out of a corner. `Q` and `E` lean, so you peek with your eye instead of your whole
body.

**No SBMM. Ever.** Mixed lobbies. You will run into someone better than you. If you get
farmed, you got farmed. The title of the game is the diagnosis.

**Straight into a match.** There is a live firefight behind the start screen while you
read it. Press SINGLEPLAYER to start a fresh arena with keyboard or pad. MATCH SETTINGS
lets you choose 1–50 bots, their difficulty, a frag limit of 5–100 or OFF,
plus biome, time of day and weather. Each environment choice starts on RANDOM.
These preferences apply to your next new arena.
NEW ARENA starts again immediately; Escape returns to your current match.
The bots hear you, remember you and react to nearby fire, so it works solo too.

## Multiplayer

The dedicated server uses the same executable, running without a window or audio. It
runs the same simulation at the full 120 Hz tick. Lag compensation rewinds the world to the tick you
actually shot on. Up to 7 humans share an arena. My own Quick Join box is set lower while
it is small. Bots keep populated arenas company and are never disguised as humans. The scoreboard
badges the humans, not the bots.

Quick Join lands on a small server I run and pay for myself. No promises it stays smooth if
a crowd shows up. That is fine. Anyone can host with any Linux copy.

Server owners can choose the shared environment with, for example,
`--server --biome dunes --time sunset --weather haze`. Each option also accepts
`random`; `--help` lists the choices. Environment settings travel with arena
snapshots, so server and clients need matching protocol-15 builds.

Server deployment files and operating instructions live in [server/](server/README.md).

## Build and media

Linux builds require GCC 14+, Make and the EGL, OpenGL and X11 development libraries.
`make build/game` builds native play and the headless harness; `make` also builds the
Linux x86_64 and Windows releases, requiring their cross-toolchains where applicable.

`make media` regenerates `media/hero.gif`, `hero.mp4`, `fight.png` and `scope.png`
from fresh gameplay captures and prints total elapsed time. The 30-second MP4 is
1920×1080 at 60 fps, with 24 Mbit/s video and captured stereo game audio.
See [the media guide](media/README.md) for dependencies and authoring commands.

`screenshots/` is a temporary review surface. Tidy it at the end of each prompt:
keep only a few final results, preferably clear before/after collages. Keep raw
captures, logs and earlier evidence under `build/`; media generation never reads
from `screenshots/`.

## Why this exists

Fun project by a single dev who grew up on Quake 3 and is done with SBMM, DLC and
microtransactions. I wanted to know how clean and fast a shooter decided by skill gets when
you drop all of that. No engine, plain C compiled to one native executable.
If a real crowd forms and wants friend join and stable
servers, a fun Steam release is on the table. Until then, do not take my little experiment
too seriously. Have fun with it.

## Usage ping

The game pings a server about once a minute. This is the whole packet. An install id,
whether the ping is the first of the session or a later one, which mode you are in, how
many seconds have passed since the previous ping, Windows or Linux, and on that first ping
the build version. The install id is a number rolled from the clock when the game first
ran. It lives in `config.cfg` and says nothing about your machine. No name, no hardware
details, no addresses stored. It goes to the same host Quick Join dials. If you point the
game at somebody else's server, that operator gets the id instead of me. `telemetry 0` in
`config.cfg` opts out.

## Updates

The game asks GitHub for a newer build at startup, and again when you open the menu. On
the start screen it downloads the build, checks it against the published hash, installs it
by itself and restarts. You see `UPDATING...` for a moment. This only happens before you
are in a match, with the menu on its top level. Once a session is running it never does
that. From then on the update is a row in the menu that you press. If the folder the game
sits in is not writable it touches nothing and no update row appears. The binary it
replaced is left beside the new one with a `.old` suffix. A bad build is one rename away
from being undone. `update_check 0` in `config.cfg` turns the whole thing off.

## License

Free for private use. Read it, change it, share it. No commercial use. The license is
PolyForm Noncommercial 1.0.0 and the [source is available](LICENSE). © 2026
[codehamr.com](https://codehamr.com), who also builds
[codehamr](https://github.com/codehamr/codehamr), a local first open source LLM terminal
coding agent.

---

<p align="center"><b>Star it if it earned one.</b></p>
