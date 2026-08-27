Free arena FPS. A brand new procedurally generated map every match, two guns, Quake-fast movement
with a modern slide and lean. No unlocks, no loadouts, no SBMM, no anticheat driver. If you lose:
skill issue.

Everyone spawns with the same kit: a fast rifle and a sniper that kills in one shot. Servers run at
120 ticks and rewind time to the moment you pulled the trigger, so your shots land where you saw
them — and any Linux copy can host one.

One file, about a megabyte. No installer, no launcher, no account. Maps, guns, sounds and animation
are all built by code at startup, so there is nothing else to download.

Solo dev, and this is an experiment, so do not take it too seriously.

---

**Windows** — download `skill-issue.exe` and run it.

It is new and unsigned, so you get warned twice: by your browser (**Keep**), then by Windows on
first launch (**More info** → **Run anyway**). Nothing is installed — the exe *is* the game.

**Linux / Steam Deck** — download `skill-issue-linux`, then give it permission to run:

```sh
chmod +x ./skill-issue-linux
./skill-issue-linux
```

Needs Ubuntu 23.10+, Debian 13+, Fedora 39+, Arch or SteamOS 3.7+.

---

The game updates itself on the start screen, never mid-match; your old copy is kept beside it as
`.old`. Turn that off with `update_check 0` in `config.cfg`. `SHA256SUMS` verifies your download.
