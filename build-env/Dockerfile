# A neutral build machine: Ubuntu 20.04, a non-root user `build`, and every host package
# the build needs, installed by setup-build-env.sh and listed nowhere else. This file names
# only what that script needs in order to run (sudo, git, ca-certificates, lsb-release,
# locales, tzdata). It lives in build-env/ rather than at the root because the repo root is
# a build tree of its own; the build context is the root and .dockerignore passes in only
# setup-build-env.sh.
#
# Build (from the repo root):
#   docker build -f build-env/Dockerfile -t ludbox-build .
# Run — the host name goes into build artifacts (`arca@build`), so fix it:
#   docker run -it --hostname build ludbox-build
#   add --privileged ONLY for rootfs/make-card-image.sh, whose loop devices and mounts need it.
# Inside the container:
#   1. Clone the repo INSIDE it, not a bind mount, so no host path or user name enters an
#      artifact. git-lfs objects are needed (the source offer archives the repo), so do not
#      set GIT_LFS_SKIP_SMUDGE:  git clone <url> ~/ludbox && cd ~/ludbox
#   2. ./setup-build-env.sh --scummvm   (the host packages are already here; this clones the
#      pinned ScummVM tree, which is not an apt package)
#   3. Fill the caches with the repo's own device-less scripts; release.sh --stage-only
#      --source-offer needs them, because rootfs/make-source-offer.sh archives their sources:
#        vnc_client/build-deps.sh  bluetooth/build-bluez.sh  bluetooth/build-bluealsa.sh
#        native_apps/build-alsa-lib.sh  native_apps/build-libvterm.sh
#   4. ./tests/run-all.sh, then ./release.sh --stage-only (add --source-offer for the offer).
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive TZ=UTC LANG=C.UTF-8 LC_ALL=C.UTF-8
RUN apt-get update \
 && apt-get install -y --no-install-recommends sudo git git-lfs ca-certificates lsb-release locales tzdata \
 && rm -rf /var/lib/apt/lists/*
RUN useradd -m -d /home/build -s /bin/bash build \
 && echo 'build ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/build \
 && chmod 0440 /etc/sudoers.d/build
COPY setup-build-env.sh /usr/local/src/setup-build-env.sh
# apt lists were removed above, so refresh them for the script's own apt-get, then drop them.
RUN apt-get update \
 && bash /usr/local/src/setup-build-env.sh --install-deps \
 && rm -rf /var/lib/apt/lists/*
USER build
WORKDIR /home/build
