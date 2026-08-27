Arena FPS in one C file. No engine, no assets, everything procedural. Solo dev, and this is an experiment, so do not take it too seriously.

Fast Quake movement crossed with modern slide and lean. No pickups, no armor, no item timing. Everyone spawns with both guns, a fast hitscan AR and a bolt sniper for one shot kills. New arena every match, so nobody grinds map knowledge.

The dedicated server is the same binary with graphics and sound ripped out. 120 tick, authoritative, client prediction, and it rewinds the world to the tick you actually shot on. Any Linux copy can host one. No SBMM, no kernel anticheat.

Sounds, animation, maps and guns are all generated in code. Nothing on disk except the game.

---

**Windows** — grab `skill-issue-windows-amd64.zip` and unpack it. The exe inside is unsigned, so
SmartScreen throws up a "Windows protected your PC" wall. **More info** → **Run anyway**. (The bare
`.exe` asset is what the auto-updater fetches — downloading it in a browser tends to trip Chrome's
unknown-file warning, the zip mostly doesn't.)

**Linux** — grab `skill-issue-linux-amd64`. Runs on desktop and on Steam Deck. Your browser strips
the execute bit on the way down:

```sh
chmod +x ./skill-issue-linux-amd64
./skill-issue-linux-amd64
```

Needs glibc 2.38 or newer, so Ubuntu 23.10+, Debian 13+, Fedora 39+, Arch or SteamOS 3.7+.

On the start screen — before you are in a match, menu on its top level — the game installs a newer
build by itself and restarts, showing `UPDATING...` for a moment. Once a session is running it never
does that: from then on the update is a row in the menu that you press. `update_check 0` in
`config.cfg` turns it off. The binary it replaces is left beside the new one with a `.old` suffix,
so a bad build is one rename away from being undone. `SHA256SUMS` is here if you want to check your
download.
