#!/usr/bin/env bash
# Host packages required to build the Chat Robot T5-E1 tone test.
# The ARM toolchain and T5AI platform repo are downloaded later by tos.py.
set -euo pipefail

if [[ "$(id -u)" -eq 0 ]]; then
    apt_get=(apt-get)
else
    apt_get=(sudo apt-get)
fi

sudo dpkg --add-architecture i386
"${apt_get[@]}" update
"${apt_get[@]}" install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang-format \
    cmake \
    cmake-curses-gui \
    curl \
    git \
    lcov \
    libncurses-dev \
    libsystemd-dev \
    libusb-1.0-0 \
    libusb-1.0-0-dev \
    locales \
    make \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    wget \
    libc6-i386 \
    libstdc++6:i386

printf 'Host packages installed.\n'
printf 'Next, from the project root:\n'
printf '  bash scripts/build-tone.sh\n'
