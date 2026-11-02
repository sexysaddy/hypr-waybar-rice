# HyprCursorFX

Native Hyprland 0.56.2 render-pass implementations of selected cursor effects from [tholman/cursor-effects](https://github.com/tholman/cursor-effects) and the [90's Cursor Effects demo](https://tholman.com/cursor-effects/). It adds effects around the existing system cursor; it does not replace the user's cursor theme.

## Effects

| Mode | Behavior |
|---|---|
| `bubbles` | Dense, small hollow bubbles with a blue outline. Each bubble drifts upward, expands and stays visible for about 3.4–5 seconds by default. The default spacing is 5 logical pixels with up to 100 concurrent bubbles (subject to `max_particles`). |
| `rainbow` | Six continuous, round-ended horizontal rainbow bands behind the pointer, about 18 px tall at the default 3 px band width. Uses the upstream six-color palette and 20-node trail. |
| `fairy_dust` | A coloured ASCII `*` trail. The code samples the path since the last emitted star, rather than comparing only adjacent high-frequency pointer events, and fades small stars instead of scaling them into invisibility. |
| `snowflakes` | Small white ASCII `*`, `+`, and `x` flakes that fall faster and fade out. The built-in bitmap glyphs require no emoji or system font support. |
| `ghost` | Fading copies of the active system cursor texture, using Hyprland's own cursor bounding box and hotspot calculations. |
| `trailing` | A chain of 15 eased copies of the active cursor, also using the exact native cursor bounding box/hotspot. |
| `following_dot` | A semi-transparent dot that follows with a little lag. |
| `clock` | The site's old-school animated clock around the pointer: a numbered dial, red seconds-hand dots, black hour/minute dots, and a rotating date string around the outside. The clock follows the pointer with linked easing. It uses a built-in 3×5 bitmap font, so it doesn't depend on the system's fonts. |
| `off` | Disables effects. |

The native bitmap and render-pass drawings are adaptations rather than browser-canvas screenshots. The clock's orbit, dial labels, hand angles, date string and easing are ported from upstream `clockCursor.js`. The Fairy Dust and Snowflakes effects are deliberately ASCII, as requested, rather than emoji textures.

## Build and install

Target: **Hyprland 0.56.2**. Build against the exact headers used by the running compositor; plugins are ABI-sensitive.

```sh
cd HyprCursorFX
make check
./install-reload.sh
```

`make install` performs a clean rebuild before copying the plugin. `install-reload.sh` unloads the previously loaded copy, rebuilds, installs, reloads, verifies the loaded version, migrates only settings that exactly matched previous shipped defaults (and makes a backup), then tries a visible bubbles test.

The plugin works with both config formats. For `~/.config/hypr/hyprland.conf`, the startup snippet is:

```ini
exec-once = hyprctl plugin load $HOME/.local/lib/hyprcursorfx/hyprcursorfx.so
```

For `~/.config/hypr/hyprland.lua`, use instead:

```lua
hl.exec_cmd("hyprctl plugin load " .. os.getenv("HOME") .. "/.local/lib/hyprcursorfx/hyprcursorfx.so")
```

Do not add both snippets or change your existing config format.

## Control commands

```sh
hyprctl hyprcursorfx mode bubbles
hyprctl hyprcursorfx mode rainbow
hyprctl hyprcursorfx mode fairy_dust
hyprctl hyprcursorfx mode snowflakes
hyprctl hyprcursorfx mode ghost
hyprctl hyprcursorfx mode trailing
hyprctl hyprcursorfx mode following_dot
hyprctl hyprcursorfx mode clock
hyprctl hyprcursorfx off
hyprctl hyprcursorfx toggle
hyprctl hyprcursorfx status
hyprctl hyprcursorfx reload
```

## Configuration

File: `~/.config/hypr/hyprcursorfx.conf` (or `$XDG_CONFIG_HOME/hypr/hyprcursorfx.conf`). New default values are:

```ini
mode=fairy_dust
enabled=true
max_particles=240
particle_size=4
speed=1.0
colors=#D61C59,#E7D84B,#1B8798
bubble_spacing=5
bubble_limit=100
bubble_lifetime_ms=4200
bubble_fill=#E6F1F7
bubble_stroke=#3A92C5
fairy_dust_spacing=2
fairy_dust_limit=140
snowflake_spacing=3
snowflake_limit=32
ghost_spacing=5
ghost_limit=18
rainbow_size=3
following_dot_color=#323232
following_dot_alpha=0.65
clock_radius=45
clock_date_color=#0000FF
clock_face_color=#000000
clock_seconds_color=#FF0000
clock_minutes_color=#000000
clock_hours_color=#000000
```

- `bubble_spacing`, `bubble_limit`, `bubble_lifetime_ms`: density, active bubble cap and lifetime. A lower spacing or higher limit creates a longer/denser trail.
- `fairy_dust_spacing`, `fairy_dust_limit`: spacing between coloured ASCII stars and their live cap.
- `snowflake_spacing`, `snowflake_limit`, `speed`: flake density and fall speed.
- `ghost_spacing`, `ghost_limit`: number of cursor copies and distance between them.
- `rainbow_size`: thickness of each of the six rainbow bands.
- `clock_radius`: radius of the numbered dial; the date string orbits at 1.5× this radius. Clock colors can be changed with the `clock_*_color` options.
- `particle_size`, `speed`, `colors` customize particle modes. The rainbow uses its fixed upstream palette.
- `max_particles` is an overall safety cap.

On upgrade the installer backs up the config to `hyprcursorfx.conf.pre-0.4.0.bak` before migrating known unchanged defaults. Other custom values are left alone. Reload settings without restarting Hyprland:

```sh
hyprctl hyprcursorfx reload
```

## Diagnostics

`hyprctl hyprcursorfx status` reports the active mode, cursor movement and render callback counters. An element count means the plugin submitted elements to a render pass; it alone does not prove that pixels were visually inspected.

## Verification status

`make check` runs dependency-free parser and packaging regressions. This development environment does not have the matching Hyprland 0.56.2 headers or a live Wayland compositor, so a `.so` build and visual runtime test cannot be claimed here. Test the visuals in the active session after installing.

## Attribution

Selected effect behavior is reimplemented from Tim Holman's [cursor-effects](https://github.com/tholman/cursor-effects), whose package metadata declares the MIT license. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). No upstream JavaScript library is embedded in the plugin.
