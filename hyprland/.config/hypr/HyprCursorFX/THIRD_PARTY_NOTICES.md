# Third-party notices

Selected cursor-effect behavior in HyprCursorFX is implemented in native C++ based on effects published by Tim Holman in `tholman/cursor-effects`:

- https://github.com/tholman/cursor-effects
- https://tholman.com/cursor-effects/

The upstream package metadata declares the project MIT licensed. This port does not bundle the upstream JavaScript library; it reimplements rendering and update behavior for Hyprland's render-pass API. Rainbow colors, Fairy Dust palette, Bubble palette, and the retro Clock animation follow the upstream defaults. Fairy Dust and Snowflakes use built-in ASCII bitmap glyphs to avoid font/emoji dependencies.
