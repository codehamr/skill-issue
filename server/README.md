# Dedicated server

All deployment files live here: `docker-compose.yaml`, `Dockerfile`,
`entrypoint.sh`, `report.awk` and `.dockerignore`. An optional, ignored `.env`
beside Compose overrides its inline defaults. The root Makefile also reads
`server/.env` for `SERVER_HOST` (default `vps`) and `SERVER_DIR` (default
`skill-issue`).

Local startup from the repository root:

```sh
docker compose -f server/docker-compose.yaml up -d --build
```

Compose builds with this directory as its context. The Dockerfile copies only
the supervisor and report generator; the supervisor fetches the Linux release
and verifies its SHA256SUMS entry. A compiler or checkout is unnecessary on the
server. Runtime files stay beside Compose: `data/`, `server.log` and `stats.txt`.
The build context excludes runtime state and `.env`.

`make server-up` copies the deployment files flat into `SERVER_DIR` on
`SERVER_HOST`, copies `.env` when present, then runs Compose there. It preserves
existing data and logs. Deployment requires maintainer authorization.

| Command | Effect |
| --- | --- |
| `make server-down` | Stop and remove the container; retain state. |
| `make server-stats` | Show the report regenerated each minute. |
| `make server-logs` | Tail raw server and updater logs. |
| `make server-reset` | Interrupt games and clear log/stat history; retain binaries. |
| `make server-delete` | Delete container, image and deployment directory, including state. |

Compose comments own environment defaults, UDP port publishing and container
restrictions. `entrypoint.sh` owns release checks, occupied-server update deferral
and restart behavior. The optional local-binary mount shown in Compose disables
updates and uses `../build/game-x86_64` relative to this directory.
