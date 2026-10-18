#!/usr/bin/env bash
# ждём, пока xfdesktop создаст свой слой обоев
for _ in $(seq 60); do
  hyprctl layers | grep -q 'namespace: desktop,' && break
  sleep 0.5
done
# перезапускаем демон, чтобы его слой стал выше
pkill -x awww-daemon
sleep 0.5
setsid -f awww-daemon
sleep 1
waypaper --restore
