#!/bin/bash
swww query
if [ $? -eq 1 ]; then
  swww-daemon --format xrgb &

  swww img /data929G/br\ br\ windows\ 7/wallpaper/wallhaven-ml91w9.jpg \
    --transition-type "wipe" \
    --transition-duration 3
fi