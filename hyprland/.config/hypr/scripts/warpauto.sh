#!/bin/bash/

$Start=(systemctl start warp-svc)
if $Start true; then
    warp-cli connect
fi
