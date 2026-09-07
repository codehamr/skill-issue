# C23 development container: native Linux GCC, MinGW cross-compilation and
# Mesa surfaceless rendering. The renderer actually selected at runtime is the
# timing authority; software rendering is not a physical GPU/display test.
# Makefile owns the additional X11 and foreign-architecture build requirements.
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
