# Minimal C game-dev container: native Linux build (gcc 14) + Windows 11
# cross-compile (MinGW-w64 gcc 14). Headless GL comes from Mesa llvmpipe (CPU
# software rendering) — the game creates its context via EGL surfaceless, no
# GPU and no X11 in this container. trixie ships Mesa 25 natively, so no
# backports repo is needed (bookworm required one).
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      gdb \
      git \
      procps \
      gcc-mingw-w64-x86-64 \
      python3 \
      ffmpeg \
      fonts-dejavu-core \
      imagemagick \
      openssh-client \
      libegl1-mesa-dev libgl1-mesa-dev libgl1-mesa-dri \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*
