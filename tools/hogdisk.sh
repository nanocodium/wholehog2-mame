#!/bin/bash
# Manage Wholehog II show disk images (1.44 MB FAT12) with mtools.  Run from WSL:  bash tools/hogdisk.sh <cmd> ...
#   new  <image>              create a blank, DOS formatted disk (label NO_NAME; the console relabels it on save)
#   ls   <image>              list the files on the disk
#   get  <image> <dir>        copy the whole show (all files and folders) out of the disk into <dir>
#   put  <image> <dir>        copy a show folder (files + LIBRARY/, SETUP/) onto the disk
# The console writes the show as plain DOS files (_ZCAT.DAT catalogue, *.DAT, LIBRARY\, SETUP\); a disk from a real
# console, or one unzipped from a show archive, can be built with "put" and loaded with Setup -> Change Show -> Load Show.
set -e
cmd=$1; img=$2
case "$cmd" in
  new)  mformat -C -i "$img" -f 1440 -v NO_NAME :: && echo "created $img" ;;
  ls)   mdir -i "$img" -/ :: ;;
  get)  mkdir -p "$3" && mcopy -i "$img" -s -n -m :: "$3" && echo "copied to $3" ;;
  put)  (cd "$3" && mcopy -i "$img" -s -n -m ./* ::) && echo "copied $3 to $img" ;;
  *)    sed -n 2,9p "$0"; exit 1 ;;
esac
