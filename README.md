# skill-issue

A minimalist low-poly tactical FPS. The whole game — renderer, physics,
procedural audio, bots, and netcode — is **one C23 translation unit**
(`code/game.c`). No engine, no assets on disk: the arena, the weapons, the
characters and every sound are generated at startup.

Singleplayer is instant (boots straight into a bot deathmatch). Multiplayer is
one button: **Quick Join**.

## Play

Grab a binary from [Releases](../../releases/latest) — one file, no installer.

| File | Machine |
| --- | --- |
| `skill-issue-windows-amd64.exe` | Windows 11 (also Windows-on-ARM via emulation) |
| `skill-issue-linux-amd64` | Linux on Intel/AMD — desktop, Steam Deck / SteamOS |
| `skill-issue-linux-arm64` | Linux on ARM64 (e.g. Asahi) |

On Linux the browser strips the execute bit, so:

```sh
chmod +x ./skill-issue-linux-amd64 && ./skill-issue-linux-amd64
```

The game checks for updates on startup and offers them — it never installs
silently. Linux needs glibc ≥ 2.38 (Ubuntu 23.10+ / Debian 13+ / SteamOS 3.7+).

### Controls

| | |
| --- | --- |
| Move | `W` `A` `S` `D` |
| Jump / Crouch / Slide | `Space` / `Shift` (crouch near run speed to slide) |
| Lean | `Q` / `E` |
| Fire / Aim | `Mouse1` / `Mouse2` |
| Reload / Swap | `R` / `X` |
| Weapons | `1` assault rifle · `2` sniper |
| Menu | `Esc` |

Gamepads work too (raw stick input, aim assist, full menu navigation). Rebind
anything in the settings menu; it also has a live-tuning DEV tab.

### Multiplayer

`Esc → GAME → ONLINE → QUICK JOIN` connects to the configured server, keeps you
in the local match until the first snapshot arrives, then switches you in — so
there is never a spinner. **LEAVE MATCH** drops you back to a local arena. Set
the server in the config (`mp_host`, `mp_port`) or launch with
`--connect host[:port]`.

Bots fill empty slots and are never disguised as people; the arena regenerates
each match. It is server-authoritative with client-side prediction.

## Build

Needs **GCC ≥ 14** (for `-std=c23`). The devcontainer (`debian:trixie-slim`)
has both toolchains.

```sh
make            # both deliverables: build/game (Linux) and build/game.exe (Windows, via MinGW)
make build/game # Linux only, fast iteration
```

The Linux binary is also the **test harness** and the **dedicated server**,
chosen at runtime:

```sh
./build/game                        # play
./build/game --server --port 27015  # dedicated server (sim + UDP, no window)
./build/game --directory --port 27000   # instance directory for Quick Join
./build/game --do "figcheck 60; parity; budget"   # headless proof harness
./build/game --help                 # every command and flag
```

There is no separate test suite: testing is driving the headless harness with
its script language, and `--help` is the command reference. See `CLAUDE.md` for
the architecture and the proof commands, and `docs/OPS.md` for running a server.

## License

[PolyForm Noncommercial 1.0.0](LICENSE) — free to use, modify and share for any
purpose that isn't primarily commercial. © 2026 [codehamr](https://codehamr.com).
