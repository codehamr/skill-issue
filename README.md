<p align="center">
  <img src="media/hero.gif" alt="bullet-time headshot, slide under fire, jump the lens, fire at the camera" width="832">
</p>

<h1 align="center">skill-issue</h1>

<p align="center">
  <b>Free arena FPS. A new procedural map every match, two guns, 120 Hz servers.<br>
  No SBMM, no unlocks, no attachments. If you lose: skill issue.</b>
</p>

<p align="center">
  <a href="https://github.com/codehamr/skill-issue/releases/latest/download/skill-issue-windows-amd64.exe"><b>Download for Windows</b></a>
  &nbsp;·&nbsp;
  <a href="https://github.com/codehamr/skill-issue/releases/latest/download/skill-issue-linux-amd64"><b>Linux / Steam Deck</b></a><br>
  One file, about a megabyte. No installer, no launcher, no account.<br>
  <sub>Windows will warn (new, unsigned): More info, then Run anyway. Linux: <code>chmod +x</code> it first.
  Details on the <a href="https://github.com/codehamr/skill-issue/releases/latest">release page</a>.</sub>
</p>

<p align="center">
  <img src="media/fight.png" alt="assault rifle firefight on a procedural sand arena" width="49%">
  <img src="media/scope.png" alt="sniper scope on a bot returning fire" width="49%">
</p>

## The game

**A new arena every match.** Map, weapons, characters, sounds: everything is generated
from a seed at startup. Nobody has map knowledge. What carries over is aim and movement.

**Two guns, everyone gets both.** A hitscan rifle and a bolt sniper that one-shots to the
body. No loadouts, no battle pass. Most walls can be shot through, at full damage.

**Movement you can get good at.** Air control to carve a jump, a slide that carries real
speed out of a corner, `Q`/`E` lean to peek with your eye instead of your whole body.

**No SBMM. Ever.** Mixed lobbies. You will run into someone better than you. If you get
farmed, you got farmed. The title of the game is the diagnosis.

**Instant.** The match is already running behind the menu: one press and you are
shooting, keyboard or pad. The bots hear you, remember you and push, so it works solo too.

## Multiplayer

The dedicated server is the same file with graphics and sound stripped out, running the
same simulation at the full 120 Hz tick, with lag compensation that rewinds the world to
the tick you actually shot on. Up to 8 humans per match; bots fill the empty slots and
are never disguised as humans.

Quick Join lands on a small server I run and pay for myself, so no promises it stays
smooth if a crowd shows up. Which is fine: anyone can host with any Linux copy.

## Why this exists

Fun project by a single dev who grew up on Quake 3 and is done with SBMM, DLC and
microtransactions. I wanted to know how clean and fast a skill-based shooter gets when
you drop all of that: no engine, no bloat, plain C compiled to one native executable that
runs fast on basically anything. If a real crowd forms and wants friend join and stable
servers, a fun Steam release is on the table. Until then: do not take my little
experiment too seriously, have fun with it.

## Usage ping

The game pings my server about once a minute: a random install id, the mode, seconds
played, Windows or Linux, build version. No name, no hardware details, no addresses
stored. `telemetry 0` in `config.cfg` opts out. It also checks GitHub at startup for a
newer build, always asks first, keeps the old binary for rollback. `update_check 0`
turns it off.

## License

Free for private use: read it, change it, share it. Just no commercial use (PolyForm
Noncommercial 1.0.0, [source available](LICENSE)). © 2026
[codehamr.com](https://codehamr.com), who also builds
[codehamr](https://github.com/codehamr/codehamr), a local-first open source LLM terminal
coding agent.

---

<p align="center"><b>Star it if it earned one.</b></p>
