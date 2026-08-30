# Runtime shell for the dedicated server. THE GAME BINARY IS NOT IN THE IMAGE —
# the entrypoint bootstraps the current Linux release into /srv/data and
# self-updates there (same SHA256SUMS date-ordering contract as the client
# updater), so this image changes ~never and needs no registry.
# libx11-6/libegl1/libgl1: the release binary links them even though --server
# opens no GL — the same trio the old server-deploy's ldd gate checked.
FROM debian:trixie-slim
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
      libx11-6 libegl1 libgl1 ca-certificates curl bash \
    && rm -rf /var/lib/apt/lists/*
COPY --chmod=0755 entrypoint.sh /entrypoint.sh
COPY report.awk /report.awk
ENTRYPOINT ["/entrypoint.sh"]
