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
      ffmpeg gifsicle \
      fonts-dejavu-core \
      imagemagick \
      openssh-client \
      libegl1-mesa-dev libgl1-mesa-dev libgl1-mesa-dri \
      curl \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Native agents live in the image, outside the mounted runtime state.
# Codex's standalone payload belongs in /opt so its config volume cannot hide it.
# Remove Claude's installer scaffolding before Docker seeds fresh config volumes.
# Rebuild without cache to fetch current releases.
SHELL ["/bin/bash", "-o", "pipefail", "-c"]
RUN set -eu; \
    curl -fsSL https://claude.ai/install.sh | bash; \
    ln -s /root/.local/bin/claude /usr/local/bin/claude; \
    curl -fsSL https://chatgpt.com/codex/install.sh \
      | CODEX_INSTALL_DIR=/usr/local/bin CODEX_HOME=/opt/codex CODEX_NON_INTERACTIVE=1 sh; \
    curl -fsSL https://codehamr.com/install.sh | TERM=xterm PREFIX=/usr/local bash; \
    claude --version; \
    codex --version; \
    codehamr --version; \
    rm -rf /root/.claude /root/.claude.json
