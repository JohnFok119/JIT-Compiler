# Image for rendering docs/demo.tape: the pinned VHS image plus a C toolchain.
# The base image tracks Debian "stable" (now trixie) while its packages are
# bookworm, so the suites are pinned to bookworm before installing.
FROM ghcr.io/charmbracelet/vhs:v0.10.0
RUN sed -i 's/^Suites: stable-security/Suites: bookworm-security/; s/^Suites: stable stable-updates/Suites: bookworm bookworm-updates/' /etc/apt/sources.list.d/debian.sources \
 && apt-get update \
 && apt-get install -y --no-install-recommends gcc make libc6-dev \
 && rm -rf /var/lib/apt/lists/*
