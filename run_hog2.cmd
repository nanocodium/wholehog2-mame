@echo off
rem Launch the Wholehog II emulator (WSL/MAME, windowed via WSLg) plus the Art-Net bridge to this PC.
rem Art-Net then appears on 127.0.0.1:6454 (ArtNetominator: adapter "127.0.0.1 (Inside this computer only)").
wsl -e bash /mnt/d/games/hog2/tools/run_hog2.sh
start "Hog2 Art-Net bridge" /min py -3 "%~dp0tools\artnet_bridge_win.py"
