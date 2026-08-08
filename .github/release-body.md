**skill-issue** — low-poly tactical shooter. One binary per platform, no installer.

## Which file for which machine?

| File | Machine |
| --- | --- |
| `skill-issue-windows-amd64.exe` | Windows 11 (also Windows-on-ARM: runs there via the built-in emulation) |
| `skill-issue-linux-amd64` | Linux on Intel/AMD — desktop, Steam Deck / SteamOS handhelds |

## Linux: start in two lines

```sh
chmod +x ./skill-issue-linux-amd64
./skill-issue-linux-amd64
```

Browsers strip the execute bit on download — without `chmod +x`, double-clicking reports
"cannot be executed" or opens the file in a text editor.

**System requirement:** Ubuntu 23.10+, Debian 13+, Fedora 39+, Arch or SteamOS 3.7+
(glibc ≥ 2.38). On older systems you'll see `version 'GLIBC_2.38' not found` — please
update your distribution; an older build does not exist.

## Windows: the SmartScreen notice

The binary is not signed. On first launch Windows shows "Windows protected your PC".
This is the normal path for unsigned downloads:

1. Click **"More info"**,
2. **"Run anyway"**.

The `SHA256SUMS` file in this release contains the checksums of all assets if you want
to verify your download.

## Updates

The game checks for a new version on startup and offers it — it never installs
silently. One release per day; the previous release is kept as a fallback.
