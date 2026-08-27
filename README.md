<p align="center">
  <img src="media/hero.gif" alt="bullet time headshot, slide under fire, jump the lens, fire at the camera" width="832">
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
  One file, just over a megabyte. It runs fast. No installer, no launcher, no account.<br>
  <sub>Windows warns you because the game is new and unsigned. Click More info, then Run anyway.
  On Linux run <code>chmod +x</code> first.
  Details on the <a href="https://github.com/codehamr/skill-issue/releases/latest">release page</a>.</sub>
</p>

<p align="center">
  <img src="media/fight.png" alt="carbine headshot at dusk on a procedural sand arena" width="49%">
  <img src="media/scope.png" alt="a bot in the sniper crosshair, one breath before the shot" width="49%">
</p>

## The game

**A new arena every match.** The map is generated from a seed and thrown away when the
match ends. Nobody can build map knowledge. That is the point. Nothing ships as an asset
either. The arena, both weapons, the soldiers and every sound are built in code at startup
instead of loaded from a file. What carries over between matches is your aim and your
movement.

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

**Fast to start, fast to run.** The whole game is one native executable of about a
megabyte. There is a live firefight behind the start screen while you read it. Two presses,
SINGLEPLAYER then START MATCH, and you are shooting, keyboard or pad. The bots hear you,
remember you and push, so it works solo too.

## Multiplayer

The dedicated server is the same file with graphics and sound stripped out. It runs the
same simulation at the full 120 Hz tick. Lag compensation rewinds the world to the tick you
actually shot on. Up to 8 humans share an arena. My own Quick Join box is set lower while
it is small. Bots fill the empty slots and are never disguised as humans. The scoreboard
badges the humans, not the bots.

Quick Join lands on a small server I run and pay for myself. No promises it stays smooth if
a crowd shows up. That is fine. Anyone can host with any Linux copy.

## Why this exists

Fun project by a single dev who grew up on Quake 3 and is done with SBMM, DLC and
microtransactions. I wanted to know how clean and fast a shooter decided by skill gets when
you drop all of that. No engine, no bloat, plain C compiled to one native executable that
runs fast on basically anything. If a real crowd forms and wants friend join and stable
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
