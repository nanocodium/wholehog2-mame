#!/bin/bash
# WSL half of run_hog2.cmd: restart the Art-Net relay and the MAME console window.
pkill -9 -x mame
pkill -9 -f "[a]rtnet_relay_wsl.py"
mkdir -p ~/hog2run && cd ~/hog2run || exit 1
[ -f /mnt/d/games/hog2/disks/show1.img ] || cp /mnt/d/games/hog2/disks/blank.img /mnt/d/games/hog2/disks/show1.img
nohup python3 /mnt/d/games/hog2/tools/artnet_relay_wsl.py > relay.out 2>&1 &
HOG2_ARTNET_HOST=127.0.0.1 DISPLAY=:0 nohup ~/mame/mame wholehog2 -rompath /mnt/d/games/hog2/mame/roms \
  -flop /mnt/d/games/hog2/disks/show1.img -pluginspath ~/mame/plugins -plugin layout -window -nomaximize -sound none -view Console > win.out 2>&1 &
sleep 3; pgrep -x mame > /dev/null && echo "MAME running" || { echo "MAME failed:"; cat win.out; }
