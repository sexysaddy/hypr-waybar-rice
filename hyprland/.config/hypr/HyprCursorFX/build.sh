#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

if [[ ! -f /usr/include/hyprland/src/plugins/PluginAPI.hpp ]]; then
  echo "ERROR: Hyprland plugin headers were not found at /usr/include/hyprland/src/plugins/PluginAPI.hpp" >&2
  echo "Install the Hyprland headers matching the running compositor (0.56.x), then retry." >&2
  echo "For source builds, run 'sudo make installheaders' inside the exact Hyprland source tree first." >&2
  exit 1
fi

version="$(Hyprland --version 2>/dev/null | head -n1 || true)"
if [[ -n "$version" ]]; then
  echo "Hyprland: $version"
fi

echo "Building HyprCursorFX against the installed Hyprland headers..."
make clean
make -j"$(nproc)"
echo "Build finished: $(pwd)/hyprcursorfx.so"
