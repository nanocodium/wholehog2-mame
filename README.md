# Wholehog II emulation (MAME driver `wholehog2`) with DMX to Art-Net

Flying Pig Systems Wholehog II ("TNT" Mk3 processor, i960CF) running the real console OS v3.3 build 177 from
ETC's legacy download, inside MAME. The four DMX outputs are decoded from the IDT72125 FIFO words the OS writes
and transmitted as Art-Net (ArtDMX, UDP 6454), one universe per port.

## Layout

| Path | Content |
|------|---------|
| `mame/wholehog2.cpp` | the driver (also copied to WSL `~/mame/src/mame/flyingpig/wholehog2.cpp`, listed in `mame.lst`) |
| `mame/roms/wholehog2/` | the two release floppy files (`tnt_177_disk1_hog.bin`, `tnt_177_disk2_hog.bin`) |
| `tools/patch_i960.py` | i960 core patch: vectored interrupts, `modify`/`extract`/`eshro`, special function registers with IMSK |
| `tools/artnet_listen.py` | Art-Net monitor: `python3 -u artnet_listen.py 0.0.0.0 16 [port]` |
| `tools/drive.lua` | MAME autoboot script that drives the console (touch, keys, snapshots) from `HOG2_SCRIPT` |
| `tools/hog.dis` | disassembly of the OS image (`unidasm -arch i960 -basepc 0x50000000 -skip 12`) |
| `docs/` | Flying Pig Archives schematics, memory map, the Hog II handbook (`H2_manual_decoded.txt` is readable) |

## Build (WSL Ubuntu, MAME source in `~/mame`)

```bash
python3 /mnt/d/games/hog2/tools/patch_i960.py ~/mame      # once; the core patch is uncommitted in ~/mame
cp /mnt/d/games/hog2/mame/wholehog2.cpp ~/mame/src/mame/flyingpig/
cd ~/mame && make -j24 TOOLS=1 NOWERROR=1 SOURCES=src/mame/flyingpig/wholehog2.cpp
```

Add `REGENIE=1` after changing `mame.lst` or adding includes.

## Run

```bash
cd ~/hog2run
DISPLAY=:0 ~/mame/mame wholehog2 -rompath /mnt/d/games/hog2/mame/roms -pluginspath ~/mame/plugins -plugin layout -window -nomaximize -sound none -view Console
```

The default "Console" view shows the two touch LCDs with a clickable console keypad below them (views "Touch LCDs",
"LCDs and monitors", "Left LCD", "Right LCD" are also
available in the video options). The LCDs are touch screens: click them with the mouse (the blue cross shows the
touch point). Touch is delivered by the layout script, so MAME's `layout` plugin must be enabled (`-plugin layout`,
with `-pluginspath` pointing at MAME's plugins directory); without it only the keypad buttons work. The
cursor
keys and right Ctrl also move and press it. The console boots to the
"Clean Start" dialog in about 12 seconds; touch "New Show" on the right LCD.

Art-Net destination (environment variables read at start):

| Variable | Default | Meaning |
|----------|---------|---------|
| `HOG2_ARTNET_HOST` | `127.0.0.1` | comma separated hosts; a `.255` address broadcasts |
| `HOG2_ARTNET_PORT` | `6454` | UDP port |
| `HOG2_ARTNET_UNIVERSE` | `0` | Art-Net port address of DMX output 1; outputs 2-4 follow |

Under WSL2, use the Windows host's address (the default gateway inside WSL) or a LAN node address; packets go out
at most 40 per second per universe, immediately on a change and once a second otherwise.

LCD rendering: the controllers run in VGA 16-colour planar mode; the driver decodes the four planes into 16 grey
levels (index 0 light ... 15 dark, as on the backlit STN panels). "LCD polarity" in MAME's Machine Configuration menu
inverts it.

## Controls

The front panel is emulated as `hog2_panel` on the LINK serial port. Its inputs (MAME Input Assignments menu,
Tab) are the 104 console keys, 10 faders (8 masters, crossfader, grand master), 4 wheels and the two touch
screens. Named keys (others are `Panel key N` and did nothing visible in the patch window):

| Console key | Panel key / default host key |
|-------------|------------------------------|
| 0-9 | keys 85, 100-102, 92-94, 76-78 / numeric keypad |
| Thru, @, Full, Set | 79 (keypad /), 103 (keypad *), 95 (keypad Enter), 99 (=) |
| . , Backspace, Clear | 86 (keypad Del), 68, 98 (Backspace) |
| + - / | 71, 70, 69 |
| Record, Load, Copy, Move, Delete, Active, Goto, Next | 75 R, 73 L, 64 C, 65 M, 66 D, 72 A, 81 G, 61 N |
| Group, Position, Colour, Beam | 36-39 / F1-F4 |
| Macro, Page, List | 88 F5, 89 F6, 91 F8 |
| Master 1-8 rows | keys 0-31 (four rows of eight), Master lists 40-47 |

A PC AT keyboard is also emulated (the OS supports one): letters type text and the handbook's shortcuts work
(S = Setup, X = Thru, R = Record, L = Load, Enter, Backspace ...).

## Getting DMX out (as on the real desk)

1. Touch **New Show** on the Clean Start dialog.
2. Press **S** (Setup), touch **Patch** on the toolbar, touch **Add Fixtures**.
3. Touch a fixture type (e.g. Generic / Desk channel), press **Set** (panel key 99), type the quantity, **Enter**,
   touch **Okay**.
4. Type `1 Thru 8 @ 1 Enter` (Thru = key 79, @ = key 103).
5. `1 Thru 4 Full` then shows level 255 on Art-Net universe 0 channels 1-4 (with the grand master up; the
   console starts a new show with the grand master at 50 %, move the Grand Master fader once to sync it).

`tools/drive.lua` automates all of this, e.g.

```bash
HOG2_SCRIPT="wait 14;touch 2 151 138;wait 2;key S;wait 2;touch 2 37 221;wait 2;touch 2 10 26;wait 2;touch 2 113 113;wait 1;pkey Set;wait 1;key 8 *;wait 1;key Enter;wait 2;touch 2 10 41;wait 3;key 1 !;key X;key 8 *;pkey @;key 1 !;wait 1;key Enter;wait 3;key 1 !;key X;key 4 \$;pkey Full;wait 8;snap" \
~/mame/mame wholehog2 -rompath /mnt/d/games/hog2/mame/roms -autoboot_script /mnt/d/games/hog2/tools/drive.lua -autoboot_delay 0
```

## Art-Net on the Windows host (bridge)
Windows (Hyper-V firewall) silently drops UDP that WSL sends to the host, so the emulator cannot reach an Art-Net
tool on the PC directly. Instead the driver sends to 127.0.0.1:6454 *inside WSL*, `tools/artnet_relay_wsl.py`
picks the frames up there and serves them over TCP 6455, and `tools/artnet_bridge_win.py` (run on Windows, no admin
needed) connects into WSL and re-emits each frame as UDP to 127.0.0.1:6454 on Windows. ArtNetominator etc. then
see the four universes on the "127.0.0.1 (Inside this computer only)" adapter. `run_hog2.cmd` starts all three
(relay, MAME window, bridge). Pass another destination to the bridge to feed a real node:
`py toolsrtnet_bridge_win.py auto 2.255.255.255`.
Alternative without the bridge: `networkingMode=mirrored` in `%USERPROFILE%\.wslconfig` (then `wsl --shutdown`).

## Floppy disk (show save / load)
The back-panel 3.5" HD drive is emulated (82077 inside the FDC37C665 super I/O, driven by the OS in polled PIO mode,
no DMA).  `run_hog2.sh` mounts `disks/show1.img` (created from `disks/blank.img` on first run); any other image can be
given with `-flop <image>` or swapped at run time from MAME's File Manager (Tab menu).  Changes are written back to the
image file when the disk is ejected or MAME exits.

In the console: Setup -> Save Show (or Setup -> Change Show for Save/Load/Format/Verify).  Verified: Format (writes a
DOS 1.44 MB disk labelled `<showname>1`), Save Show ("Save finished okay", files _ZCAT.DAT, *.DAT, LIBRARY\,
SETUP\) and Load Show into a fresh console ("Load finished okay").  A blank disk shows "<No disk>" in Change Show
until a show has been saved on it - that is the OS looking for the catalogue file, not an error.

Disk images are plain FAT12, so shows can be moved in and out with mtools: `bash tools/hogdisk.sh new|ls|get|put`
(e.g. `put` a show folder unzipped from a real console onto an image, then Load Show).  `disks/example_show.img`
holds an empty show saved by the emulator.

Two fixes to MAME's upd765 core were needed for the polled driver (`tools/patch_upd765.py`, apply to
`src/devices/machine/upd765.cpp`): report the non-DMA execution bit in MSR during FORMAT TRACK, and do not ask for
the first sector's ID bytes twice.

## Theatrical Extra Lights fixture library
`disks/tel_fixtures.img` is a show disk whose `LIBRARY\_LIB.LIB` carries a Hog II personality for every fixture of the
Minecraft mod Theatrical Extra Lights (github.com/dumann089/TheatricalExtraLights, branch ver/1.20.1): 143 Hog
fixtures for the 123 mod fixtures, one per DMX personality ("MovingScan 7ch", "MovingScan 10ch", ...).  Load it with
Setup -> Change Show -> Load Show; the fixtures then appear in Patch -> Add Fixtures under manufacturer *Generic*,
sorted by name.  Colour channels default to 255 (white), pan/tilt/focus to 128, intensity to 0, all 8-bit; comment
names from the Java sources are used for prism/gobo/zoom/laser channels; multi-cell fixtures get numbered
parameters (Red 2, Int 3, Tube 7...).

`tools/tel_library.py` regenerates the library from `tools/tel_personalities.txt` (a grep of the mod's
`fixtures/*.java`; regenerate that with the shell loop in the script header if the mod changes).  Put the result on a
clean show disk with `mcopy -i disk.img -o _LIB.LIB ::/LIBRARY/_LIB.LIB`.  Two limits found the hard way: the
`product` code is stored in 8 bits (values above 255 spill into the manufacturer byte and the fixtures show up under
the wrong manufacturer), fixture labels are cut to 8 characters and names to 15.

## What is emulated and what is not

Working: i960CF OS, synthetic boot ROM (system calls, interrupt install, timer tick, touch calibration),
16C550 UARTs, super I/O serial ports, front panel protocol (keys, faders, wheels, touch, LEDs), AT keyboard through
the PLD, LCD frame buffers, DMX output FIFOs decoded to Art-Net.

Not yet: the Cirrus VGA outputs (the OS initialises them, nothing is drawn yet), floppy disk-change detection (untested), MIDI, DMX input, LTC, printer, expansion wing, flash programming (the flash is
read-only), the LCD/LED text commands 08/09 sent to the panel.
