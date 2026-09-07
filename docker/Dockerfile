# Runtime shell for the dedicated server. The entrypoint downloads the Linux
# release into /srv/data and verifies its SHA256SUMS row before installation.
# The image provides runtime dependencies; it does not contain the game binary.
# GL/X11 libraries are linked dependencies even though --server opens no GL.
FROM debian:trixie-slim
RUN apt-get update -qq && apt-get install -y -qq --no-install-recommends \
      libx11-6 libegl1 libgl1 ca-certificates curl bash \
    && rm -rf /var/lib/apt/lists/*
COPY --chmod=0755 entrypoint.sh /entrypoint.sh
COPY report.awk /report.awk
ENTRYPOINT ["/entrypoint.sh"]
