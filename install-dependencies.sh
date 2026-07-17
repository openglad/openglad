#!/usr/bin/env bash
# Install build dependencies for OpenGlad (Debian/Ubuntu).
set -euo pipefail

sudo apt-get update
sudo apt-get install -y \
    cmake \
    ninja-build \
    libsdl2-dev \
    libsdl2-mixer-dev \
    libgtest-dev
