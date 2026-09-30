<p align="center"><img src="art/banner.png" alt="MiSTer Tasty" width="100%"></p>

# MiSTer Tasty

**Tool-assisted replays on real MiSTer FPGA cores.**

MiSTer Tasty plays published TAS movies back on a MiSTer, right from the DE10-Nano's own ARM side, with no replay
bot on the controller port and no extra hardware. Each input is fed to the core on the frame the movie asks for.
Watch a run play out on the real gateware, or use it to check a core's timing against the emulator it was
recorded on.

The command-line tool is `tasty`.

> **Status:** early and hungry. The first release: NES runs play frame-exact on a DE10-Nano; other systems are experimental.

## <img src="art/icons/taco-48.png" width="24" alt=""> Try it

**Quick start.** On the MiSTer (SSH in as `root`), with your own Super Mario Bros. ROM somewhere under
`/media/fat/games/NES`:

```sh
ssh root@<your-mister>
curl -fsSL https://raw.githubusercontent.com/mcfbytes/Tasty_MiSTer/main/test-tasty.sh | sh
```

[`test-tasty.sh`](test-tasty.sh) is short; read it before you pipe it to a shell. It never downloads a ROM: it finds
your copy, checks its checksum, and only then downloads `tasty` and the movie into `/media/fat/tasty`.

It takes three steps and one command. The binary is on the [releases page](https://github.com/mcfbytes/Tasty_MiSTer/releases/latest).

<img src="art/icons/donut-48.png" width="24" align="top" alt=""> Get `tasty` onto the SD card:

```sh
mkdir -p /media/fat/tasty && cd /media/fat/tasty
curl -fLO https://github.com/mcfbytes/Tasty_MiSTer/releases/latest/download/tasty && chmod +x tasty
```

<img src="art/icons/pizza-48.png" width="24" align="top" alt=""> Get a movie, here klmz's Super Mario Bros. run from
TASVideos. You also need your own dump of the game; the file names below are the No-Intro ones.

```sh
curl -fL -o 1330M.zip 'https://tasvideos.org/1330M?handler=Download' && unzip -o 1330M.zip
```

<img src="art/icons/onigiri-48.png" width="24" align="top" alt=""> Play it:

```sh
./tasty play klmz3-smb.fm2 --rom "/media/fat/games/NES/Super Mario Bros. (World).nes"
```

**What you'll see:** the MiSTer menu goes away (`tasty` needs the FPGA to itself, so it stops the running MiSTer
binary), a short Tasty Kun splash, then the NES core boots and Mario runs the game in just under five minutes. If
your NES core settings differ from the movie's, `tasty` prints each one it uses for this run, such as
`RAM Clear: using $00 for this run (your setting: No)`; your saved settings are not touched. 30 seconds after the
last input it restarts `/media/fat/MiSTer` and you are back at the menu. `--linger <seconds>` changes that wait,
`--stay` keeps the core running, and `tasty stop` ends early. `--no-splash` skips the logo. Your saves are left
alone too: on the NES, SNES and Genesis the game powers on with no save loaded, as the movie expects, and the file
in `saves/` is never opened.

**Known-good runs** (played to the end, in sync, on a DE10-Nano):

| game | movie | length | ROM (No-Intro) |
|---|---|---|---|
| Super Mario Bros. | ["warps" by klmz](https://tasvideos.org/1330M) | 4:57 | `Super Mario Bros. (World).nes` |
| Mike Tyson's Punch-Out!! | [by adelikat](https://tasvideos.org/1695M) | 17:47 | `Mike Tyson's Punch-Out!! (Japan, USA) (En) (Rev 1).nes` |

**Known to desync:** Super Mario Bros. 3, ["warps" by Lord_Tom, Maru & Tompa](https://tasvideos.org/3922M). The
movie is console-verified. On MiSTer it runs in sync for about nine minutes, then falls one frame behind during World
8's tank fight; under investigation. `tasty check` confirms your ROM matches a movie before you start.

## <img src="art/icons/pizza-48.png" width="24" alt=""> What it plays

| system | core | movie formats | status |
|---|---|---|---|
| NES | NES | `.fm2` (FCEUX) | **works**: see the known-good runs below |
| SNES | SNES | `.lsmv` (lsnes), `.bk2` (BizHawk) | **experimental**: plays, but the SNES is sensitive to exactly when in the frame input arrives, and no run has stayed in sync to the end yet |
| Genesis / Mega Drive | MegaDrive | `.gmv` (Gens), `.bk2` (BizHawk) | **experimental**: replays cleanly, but the one movie tried doesn't reach gameplay yet |
| PlayStation | PSX | `.bk2` (BizHawk) | **experimental**: shows off CD-ROM loading; CD timing on the core differs from the emulator, so expect desyncs. Discs as `.cue`/`.bin` (CHD not yet) |

**Tested movies**

| game | movie | result |
|---|---|---|
| Super Mario Bros. | [1330M](https://tasvideos.org/1330M) | in sync to the end |
| Mike Tyson's Punch-Out!! | [1695M](https://tasvideos.org/1695M) | in sync to the end |
| Tetris | [1502M](https://tasvideos.org/1502M) | in sync to the end |
| Super Mario Bros. 3 | [3922M](https://tasvideos.org/3922M) | console-verified; on MiSTer it slips one frame behind in World 8's tank fight, under investigation (see "For core developers") |
| Rockman / Mega Man | [2601M](https://tasvideos.org/2601M) | desyncs |
| Kiwi Kraze | [4438M](https://tasvideos.org/4438M) | desyncs |
| Super Mario World | [3019M](https://tasvideos.org/3019M) | partly in sync |
| Marble Madness | [939M](https://tasvideos.org/939M) | replays cleanly, doesn't reach gameplay yet |
| Tekken 3 | BizHawk `.bk2` | loads and plays; CD timing desyncs |

**What a movie needs**
- It starts from power-on (movies that start from a savestate or saved game are refused), with standard pads: no Zapper, Four Score, Famicom Disk System,
  DualShock analog or mid-movie resets.
- Your own ROM or disc image that matches the movie's checksum (`tasty check` tells you).
- The core settings the movie was recorded with (NES: NTSC and RAM Clear $00; SNES: NTSC; Genesis: 6 Buttons Mode and
  the region). You don't change anything: tasty sets them in memory for the run, prints each one it changed, and
  leaves your saved settings alone. `--strict` refuses instead. PSX needs the same BIOS as the movie; that is a file,
  not a setting, and tasty names the one it needs.

## <img src="art/icons/ramen-48.png" width="24" alt=""> Command line

```text
tasty play <movie> [options]      load the core and ROM, then replay the movie
tasty info <movie>                system name and whether tasty plays it
tasty check <movie> --rom <file>  does this ROM match the movie's checksum?
tasty status                      what is playing or recording
tasty stop                        stop playback and return to the menu
tasty rec start|stop [options]    record whatever is on screen, no movie needed
```

**Playback (shipped)**

| option | meaning |
|---|---|
| `--rom <file>` | the ROM to boot (required for `play` in this cut) |
| `--core <rbf>` | the core to load (default: the one for the movie's system) |
| `--lead <frames>` | shift the whole movie by N frames |
| `--stop-at <frame>` | play movie frames 0 to N-1, then end there |
| `--linger <seconds>` | wait after the last input before returning to the menu (default `30`) |
| `--stay` | never return on its own; the core keeps running until `tasty stop` |
| `--strict` | refuse when a core setting differs from the movie's, instead of setting it for this run |
| `--no-splash` | skip the logo before the core load |
| `--vsync-adjust 0\|1` | scaler mode forced for the session (disk INI is left alone) |

**Playback (planned)**

| option | meaning |
|---|---|
| `--rom` default | look the ROM up by the movie's checksum |
| `--ram-init zero\|ff\|random` | power-on RAM fill, where the core supports it |
| `--loop` | start over when the movie ends |

**Recording (shipped)** — works with `tasty play` and `tasty rec start`

| option | meaning |
|---|---|
| `--record <dir>` | record to this directory |
| `--hashes` | also write a per-frame hash log |
| `--hashes-only` | hash log only, no video |

**Recording (planned)**

| option | meaning |
|---|---|
| `--codec cscd\|zmbv` | `cscd` is what this cut records; `zmbv` is planned |
| `--scale auto\|native\|half` | this cut records native |
| `--every <n>` | keep every Nth frame |
| `--from <frame>` `--to <frame>` | record only this slice |
| `--segment <size>` | split files at this size |

Recordings are the core's own pixels, as it hands them to the scaler: native resolution, no filters, in plain AVI that
`ffmpeg` reads directly:

```sh
tasty play smb3.fm2 --record /media/fat/recordings --hashes
ffmpeg -i smb3_000.avi -c:v libx264 -crf 16 -pix_fmt yuv420p smb3.mp4
```

Recording is best effort. If the CPU or storage can't keep up, a frame is repeated and counted, and the replay never
waits for the recording.

`info` prints the movie system and whether tasty plays it. Frame count and rerecords are planned.

## <img src="art/icons/sushi-48.png" width="24" alt=""> Build

Host (x86-64 tests):

```sh
cmake --preset host
cmake --build --preset host
ctest --preset host
```

You need CMake 3.21, Ninja, g++ 15, zlib, and (optional) libminizip and liblzo2. Host tests do not
need libchdr; the static armhf binary builds it from the same commit the MiSTer image uses, so PSX
`.chd` discs work.

A static armhf binary for a stock MiSTer SD image uses a Bootlin `armv7-eabihf` glibc GCC 15
toolchain (`armv7-eabihf--glibc--stable-2026.08-1`, glibc 2.44). glibc is linked in whole, so the binary
runs on any image whatever its own libc. Set `TASTY_TOOLCHAIN_ROOT` to that toolchain and
`TASTY_STATIC_DEPS` to a prefix for static zlib-ng (compat), lzo, minizip, and libchdr, then:

```sh
python3 src/tools/build-static-deps.py
cmake --preset armhf-static
cmake --build --preset armhf-static
file build/armhf-static/src/firmware/tasty
```

GitHub Actions builds the host tests on every push and uploads a static armhf artifact. A tag release job is
written; publishing is manual.

## <img src="art/icons/icecream-48.png" width="24" alt=""> The art

Meet Tasty Kun: MiSTer Kun, sticking his tongue out.

| | | |
|:-:|:-:|:-:|
| <img src="art/tasty-kun.png" width="200" alt="Tasty Kun"> | <img src="art/tasty-kun-donut.png" width="200" alt="Tasty Kun with a donut"> | <img src="art/tasty-kun-pizza.png" width="200" alt="Tasty Kun with pizza"> |
| **Plain** | **Donut** | **Pizza** |
| <img src="art/tasty-kun-onigiri.png" width="200" alt="Tasty Kun with onigiri"> | <img src="art/tasty-kun-icecream.png" width="200" alt="Tasty Kun with ice cream"> | <img src="art/tasty-kun-sushi.png" width="200" alt="Tasty Kun with sushi"> |
| **Onigiri** | **Ice cream** | **Sushi** |
| <img src="art/tasty-kun-taco.png" width="200" alt="Tasty Kun with a taco"> | <img src="art/tasty-kun-ramen.png" width="200" alt="Tasty Kun with ramen"> | <img src="art/tasty-kun-holubtsi.png" width="200" alt="Tasty Kun with holubtsi"> |
| **Taco** | **Ramen** | **Holubtsi** |

There is a pixel version too: <img src="art/tasty-kun-8bit-32x32.png" width="32" alt="8-bit Tasty Kun"> at 32x32
([big](art/tasty-kun-8bit.png)). Every variant comes as SVG and PNG in [`art/`](art/), along with the
[banner](art/banner.png) and the [social preview](art/social-preview.png). Heading icons live in [`art/icons/`](art/icons/).

## <img src="art/icons/donut-48.png" width="24" alt=""> For core developers: find the first different frame

`tasty-compare` runs on a **desktop Linux PC**, not on the DE10-Nano. Copy the
recording off the SD card (or record to a network share) and compare there.
Dependencies and emulator dumpers: [`tools/compare/README.md`](tools/compare/README.md).

```sh
# 1. On the MiSTer: play and record hashes + AVI
tasty play klmz3-smb.fm2 --rom "Super Mario Bros. (World).nes" \
  --record /media/fat/recordings --hashes

# 2. On the PC: dump FCEUX from power-on (your ROM, same movie)
TASTY_DUMP_DIR=./emu-dump fceux --sound 0 --playmov klmz3-smb.fm2 \
  --loadlua tools/compare/fceux-dump.lua "Super Mario Bros. (World).nes"

# 3. On the PC: first different movie frame, CSV, and side-by-side PNGs
tools/compare/tasty-compare --mister recordings/klmz3-smb.frames.tsv \
  --emu emu-dump --system nes --auto-offset --out ./compare-out
```

Exit `0` means every aligned movie frame matched. Exit `1` prints the first
different movie frame (and the core frame) and writes `frames.csv` plus
`frame-NNNNNN-{mister,emu,diff,sheet}.png` (MiSTer | emulator | heatmap).
`--hashes-only` compares two `*.frames.tsv` files with no video (two MiSTer
runs or two core builds). Per-frame majority leftover is palette-free.
`tools/compare/tasty-compare --check-deps` lists what the machine is missing.

Worked example (TASVideos [3922M](https://tasvideos.org/3922M) “warps” by
Lord_Tom, Maru and Tompa): NES core NTSC, RAM Clear `$00`. Record on the
MiSTer, dump FCEUX on the PC, then:

```sh
tools/compare/tasty-compare --mister recordings/smb3-warps.frames.tsv \
  --emu emu-smb3 --system nes --auto-offset --out ./compare-out
```

Expected: offset +1 from movie 33878, first unique picture 33905. Full
steps: [`tools/compare/README.md`](tools/compare/README.md).

## <img src="art/icons/holubtsi-48.png" width="24" alt=""> Credits and licence

- MiSTer Kun was created by [HeWhoisRed](https://github.com/Hewhoisred) as a gift to the MiSTer community and
  remastered by [baxysquare](https://github.com/baxysquare/mister_kun). The artwork here is derived from that
  remaster and shared on the same terms: use it and remix it freely, and credit the original creator where you can.
- The code is released under the [GNU General Public License v3.0](LICENSE).
- MiSTer Tasty is an independent community project, not affiliated with the MiSTer FPGA project.
