#!/usr/bin/env bash
# Run this script with INCLUDES_GIT=1 in order to install the entire git history

set -e

# ========================================================================================
# Clone the repo into ~/.config
# ========================================================================================
FLAG=""
if [[ -z "${INCLUDES_GIT}" ]]; then
    FLAG="--depth 1"
fi

echo "Cloning repo into ~/.config/rofi..."
git clone $FLAG https://github.com/iluvgirlswithglasses/cutie-colorful-rofi ~/.config/rofi
cd ~/.config/rofi


# ========================================================================================
# Install uv and sync
# ========================================================================================
export PATH=$PATH:$HOME/.local/bin
if command -v uv >/dev/null 2>&1; then
    echo "Found uv package manager..."
else
    echo "uv is not installed, installing..."
    echo "Executing: curl -LsSf https://astral.sh/uv/install.sh | sh"
    curl -LsSf https://astral.sh/uv/install.sh | sh
fi

uv sync

