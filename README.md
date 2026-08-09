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

On ARM64 Linux, build from source (`make build/game`) — there is no release
asset for it.

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
there is never a spinner. **LEAVE MATCH** drops you back to a local arena.

The master server is compiled in, so a fresh install needs no setup. To point
somewhere else, set `mp_host` / `mp_port` in `config.cfg` or launch with
`--connect host[:port]` — the config wins over the compiled-in default. Note
that the file is written on first start, so an existing install keeps whatever
value it was created with.

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
./build/game --server --port 31415  # dedicated server (sim + UDP, no window)
./build/game --directory --port 31415   # instance directory for Quick Join
./build/game --do "figcheck 60; parity; budget"   # headless proof harness
./build/game --help                 # every command and flag
```

There is no separate test suite: testing is driving the headless harness with
its script language, and `--help` is the command reference. See `CLAUDE.md` for
the architecture and the proof commands, and `docs/OPS.md` for running a server.

### Dedicated server on a VPS

```sh
make server-start   # build x86_64 → stop what runs there → scp → start detached
make server-kill    # stop it again
```

Both are idempotent (`server-start` replaces a running instance). The host is
the ssh config Host `plaxtoris`; override `SERVER_HOST`, `SERVER_PORT` (31415),
`SERVER_DIR` (`/opt/skill-issue`) or `SERVER_OPTS` on the command line. The
server logs to `$SERVER_DIR/server.log` — a `digest` line every 10 s.

It does not run as root: `setpriv` drops to an unprivileged `skill-issue`
system account with no capabilities and `no_new_privs`, and directory, binary
and config stay root-owned, so a compromised server cannot rewrite the binary
it would be restarted from.

Two things the VPS needs once:

- `apt-get install -y libx11-6 libegl1 libgl1` — the ELF lists them as NEEDED,
  so the loader resolves them before `main` even though `--server` uses no GL.
- **Open the UDP port in the *provider* firewall**, not just the host's. On a
  Hetzner Cloud VPS, `ufw` off and `INPUT ACCEPT` are not enough: packets to
  31415/udp never reach the machine until an inbound rule **UDP 31415 from
  `0.0.0.0/0` and `::/0`** exists in the Cloud Firewall.

These targets are the iteration loop, not production — no crash restart, no
reboot survival. `tools/game@.service` + `docs/OPS.md` is that path.

Client and server must share a **CPU architecture**: the arena hash differs
between aarch64 and x86_64, and a mismatched client refuses with `ARENA
MISMATCH` rather than playing a divergent map. Testing an x86_64 server from
the aarch64 devcontainer therefore goes through qemu:

```sh
qemu-x86_64-static ./build/game-x86_64 --do "netclient HOST 31415 600"
```

## License

[PolyForm Noncommercial 1.0.0](LICENSE) — free to use, modify and share for any
purpose that isn't primarily commercial. © 2026 [codehamr](https://codehamr.com).
