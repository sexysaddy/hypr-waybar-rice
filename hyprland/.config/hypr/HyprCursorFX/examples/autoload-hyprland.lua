-- Add this one line to ~/.config/hypr/hyprland.lua.
-- Alternative to autoload-hyprland.conf, not an additional step.
hl.exec_cmd("hyprctl plugin load " .. os.getenv("HOME") .. "/.local/lib/hyprcursorfx/hyprcursorfx.so")
