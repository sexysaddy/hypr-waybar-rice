#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$(realpath "$0")")"

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]]; then
  echo "ERROR: run this from a terminal inside the active Hyprland session." >&2
  exit 1
fi
command -v hyprctl >/dev/null || { echo "ERROR: hyprctl not found" >&2; exit 1; }

plugin="$HOME/.local/lib/hyprcursorfx/hyprcursorfx.so"
version="$(tr -d '\r\n' < VERSION)"

# Unload the currently running copy before replacing/reloading it. Ignore only the
# expected case where this plugin isn't loaded yet; the following load is checked.
hyprctl plugin unload "$plugin" >/dev/null 2>&1 || true

# `make install` now performs a clean build regardless of archive/file timestamps.
make install

# Migrate only values that were shipped as defaults in previous releases.
# Back up first; user-customized values not matching a shipped default are preserved.
config="$HOME/.config/hypr/hyprcursorfx.conf"
if [[ -f "$config" ]] && grep -Eq '^(bubble_spacing=(16|8)|bubble_limit=(18|56)|snowflake_spacing=(12|6)|snowflake_limit=(8|24))$' "$config"; then
  backup="${config}.pre-${version}.bak"
  if [[ ! -e "$backup" ]]; then
    cp -p "$config" "$backup"
    echo "Backed up previous default config to: $backup"
  fi
  sed -i \
    -e 's/^bubble_spacing=16$/bubble_spacing=5/' \
    -e 's/^bubble_spacing=8$/bubble_spacing=5/' \
    -e 's/^bubble_limit=18$/bubble_limit=100/' \
    -e 's/^bubble_limit=56$/bubble_limit=100/' \
    -e 's/^snowflake_spacing=12$/snowflake_spacing=3/' \
    -e 's/^snowflake_spacing=6$/snowflake_spacing=3/' \
    -e 's/^snowflake_limit=8$/snowflake_limit=32/' \
    -e 's/^snowflake_limit=24$/snowflake_limit=32/' \
    "$config"
  if ! grep -q '^bubble_lifetime_ms=' "$config"; then echo 'bubble_lifetime_ms=4200' >> "$config"; fi
  if ! grep -q '^fairy_dust_spacing=' "$config"; then echo 'fairy_dust_spacing=2' >> "$config"; fi
  if ! grep -q '^fairy_dust_limit=' "$config"; then echo 'fairy_dust_limit=140' >> "$config"; fi
  echo "Migrated unchanged previous default settings in: $config"
fi
# New options are appended with defaults if not present; never replace user values.
if [[ -f "$config" ]]; then
  grep -q '^bubble_lifetime_ms=' "$config" || echo 'bubble_lifetime_ms=4200' >> "$config"
  grep -q '^fairy_dust_spacing=' "$config" || echo 'fairy_dust_spacing=2' >> "$config"
  grep -q '^fairy_dust_limit=' "$config" || echo 'fairy_dust_limit=140' >> "$config"
  grep -q '^clock_radius=' "$config" || echo 'clock_radius=45' >> "$config"
  grep -q '^clock_date_color=' "$config" || echo 'clock_date_color=#0000FF' >> "$config"
  grep -q '^clock_face_color=' "$config" || echo 'clock_face_color=#000000' >> "$config"
  grep -q '^clock_seconds_color=' "$config" || echo 'clock_seconds_color=#FF0000' >> "$config"
  grep -q '^clock_minutes_color=' "$config" || echo 'clock_minutes_color=#000000' >> "$config"
  grep -q '^clock_hours_color=' "$config" || echo 'clock_hours_color=#000000' >> "$config"
fi

load_result="$(hyprctl plugin load "$plugin" 2>&1)" || {
  echo "ERROR: Hyprland could not load HyprCursorFX:" >&2
  echo "$load_result" >&2
  exit 1
}
printf '%s\n' "$load_result"

printf '\n--- Loaded plugins ---\n'
plugin_list="$(hyprctl plugin list)"
printf '%s\n' "$plugin_list"
plugin_count="$(printf '%s\n' "$plugin_list" | grep -c '^Plugin HyprCursorFX' || true)"
if [[ "$plugin_count" -ne 1 ]]; then
  echo "ERROR: expected exactly one HyprCursorFX plugin, found $plugin_count. A different copy may still be autoloaded from your Hyprland config." >&2
  echo "Search for plugin-load lines with: grep -Rni 'hyprcursorfx.*so' ~/.config/hypr" >&2
  exit 4
fi
loaded_block="$(printf '%s\n' "$plugin_list" | awk '/Plugin HyprCursorFX/{show=1} show{print} show && /^$/{exit}')"
if ! printf '%s\n' "$loaded_block" | grep -Fq "Version: $version"; then
  echo "ERROR: the loaded HyprCursorFX is not version $version. The startup config may be loading a different .so path." >&2
  echo "Loaded HyprCursorFX block:" >&2
  printf '%s\n' "$loaded_block" >&2
  echo "Search your config for old plugin load lines and ensure they point to: $plugin" >&2
  exit 3
fi

printf '\n--- HyprCursorFX runtime status ---\n'
ctl_status="$(hyprctl hyprcursorfx status 2>&1)" || true
printf '%s\n' "$ctl_status"
if ! printf '%s\n' "$ctl_status" | grep -Fq "HyprCursorFX $version"; then
  echo "Native hyprctl command wasn't answered; trying the plugin dispatcher as a compatibility fallback." >&2
  dispatch_result="$(hyprctl dispatch hyprcursorfx status 2>&1)" || true
  printf '%s\n' "$dispatch_result"
  if printf '%s\n' "$dispatch_result" | grep -Eqi 'invalid dispatcher|unknown dispatcher|unknown request'; then
    echo "ERROR: version $version is loaded, but neither control entrypoint is responding. Check the Hyprland log for HyprCursorFX registration errors." >&2
    exit 2
  fi
fi

printf '\n--- Enable visible bubbles test ---\n'
mode_result="$(hyprctl hyprcursorfx mode bubbles 2>&1)" || true
printf '%s\n' "$mode_result"
if printf '%s\n' "$mode_result" | grep -Eqi 'unknown request|invalid dispatcher|unknown dispatcher'; then
  mode_result="$(hyprctl dispatch hyprcursorfx mode bubbles 2>&1)" || true
  printf '%s\n' "$mode_result"
  if printf '%s\n' "$mode_result" | grep -Eqi 'unknown request|invalid dispatcher|unknown dispatcher'; then
    echo "ERROR: neither the Hyprctl command nor the dispatcher can change the mode." >&2
    exit 5
  fi
fi

printf '\nThe final status should say mode=bubbles; renderer_available=true, active_render_frames > 0, and queued_effect_elements > 0 should appear after moving the pointer. If not, the status identifies whether the renderer or monitor context is missing.\n'
