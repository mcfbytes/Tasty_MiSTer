# tasty-compare

Find the first movie frame where a MiSTer core's picture differs from an
emulator dump (FCEUX, BizHawk, lsnes, or a lossless AVI).

**This tool runs on a regular desktop Linux machine (x86-64 or arm64), not on
the DE10-Nano.** Only `tasty` itself runs on the MiSTer. Copy the recording off
the SD card (or record straight to a network share) and compare on the PC.

Exit codes: `0` identical, `1` first difference found, `2` usage or input error.

## Dependencies

| tool | needs | Debian/Ubuntu | Fedora | Arch |
|---|---|---|---|---|
| tasty-compare | Python >= 3.9 | `sudo apt install python3` | `sudo dnf install python3` | `sudo pacman -S python` |
| video compare | ffmpeg and ffprobe >= 4.2, with the CamStudio (`cscd`) decoder | `sudo apt install ffmpeg` | `sudo dnf install ffmpeg` | `sudo pacman -S ffmpeg` |
| faster `--tolerance` | numpy >= 1.20 (optional; a pure-Python path is always there) | `sudo apt install python3-numpy` or `pip install -r requirements.txt` | `sudo dnf install python3-numpy` | `sudo pacman -S python-numpy` |
| FCEUX dump | FCEUX >= 2.6 with Lua | `sudo apt install fceux` | `sudo dnf install fceux` | `sudo pacman -S fceux` |
| BizHawk dump | BizHawk / EmuHawk 2.8 or newer | unpack the official zip; not in apt | same | same |
| lsnes dump | lsnes rr2, or a lossless AVI from power-on | unpack the lsnes build; not in apt | same | same |

Check what this machine has:

```sh
tools/compare/tasty-compare --check-deps
```

`requirements.txt` lists only the optional pip package (`numpy`). ffmpeg is an
OS package, not a pip package. A clean `python3 -m venv` with no pip installs
is enough for `--hashes-only` and the unit tests.

## Three commands

On the MiSTer:

```sh
tasty play klmz3-smb.fm2 --rom "Super Mario Bros. (World).nes" \
  --record /media/fat/recordings --hashes
```

That writes `klmz3-smb.frames.tsv` and `klmz3-smb_000.avi` (CSCD, native
pixels). Copy the directory to the PC.

On the PC, dump FCEUX from power-on (same movie, your own ROM):

```sh
export TASTY_DUMP_DIR=./emu-dump
mkdir -p "$TASTY_DUMP_DIR"
fceux --sound 0 --playmov klmz3-smb.fm2 --loadlua tools/compare/fceux-dump.lua \
  "Super Mario Bros. (World).nes"
```

Compare hashes (two MiSTer runs, or MiSTer vs a hash log):

```sh
tools/compare/tasty-compare \
  --mister recordings/klmz3-smb.frames.tsv \
  --emu emu-dump/emu.hashes.tsv \
  --hashes-only --auto-offset --out ./compare-out
```

Compare pictures (MiSTer AVI vs emulator AVI or PNG sequence):

```sh
tools/compare/tasty-compare \
  --mister recordings/klmz3-smb.frames.tsv \
  --emu emu-dump \
  --system nes --auto-offset --out ./compare-out
```

`--system nes` crops 8 lines only if a side is 256x240. FCEUX Qt dumps at
256x224 are already 1:1 with the core (shift 0,0). Colour: each frame maps
every emulator RGB to the majority core RGB; leftover ≤ `--pixel-slop` (32)
is the same picture (sprite specks). `--offset N` is `emu = mister + N`.

Output: `frames.csv` (movie frame, match, metric); if they differ, PNGs named
`frame-NNNNNN-{mister,emu,diff,sheet}.png` for the first difference and the
`--around` neighbors (sheet is MiSTer | emulator | red heatmap).

## Sidecar columns

`*.frames.tsv` is one row per core frame:

`core_frame`, `header_ctr`, `capture_ns`, `dup_reason`, `movie_frame`, `hash`
(CRC-32 of native RGB, 8 hex digits), `width`, `height`, `segment`, `avi_frame`
(ffmpeg frame index in `<stem>_<segment>.avi`; `-1` in hashes-only recordings).

`--hashes-only` compares two of these (or an emulator hash log) by movie frame
and needs no video. Two MiSTer builds or two runs of the same movie are the
fast path.

## Emulator recipes

**FCEUX (NES):** `fceux-dump.lua` as above. Default dump is a CRC-32 of RGB
pixels cropped to 256x224. Palettes differ, so compare pictures with
`--system nes` (colour map) rather than `--hashes-only`. `TASTY_DUMP_INDEX=1`
hashes palette indices when that return is live. `TASTY_DUMP_FULL=1` keeps
256x240. `TASTY_DUMP_LIMIT=N` stops after frame N (headless Qt may ignore
`emu.exit()`, so wrap with `timeout -k 2 20`). `--palette file.pal` overrides
the shipped Kitrinx 34 file.

**BizHawk (NES/SNES/Genesis/PSX):** open the ROM in EmuHawk 2.8+, load
`bizhawk-dump.lua`, play the movie. Set `TASTY_DUMP_PNG=1` to write PNGs, then
pass that directory as `--emu`. A `png` row's hash column is `-`: such a log
alone is refused, not compared.

**lsnes (SNES):** load `lsnes-dump.lua`, or File → Dump video from power-on
(lossless) and pass the AVI as `--emu`.

**TriCNES (NES):** `tricnes-dump/` is a headless runner for an unmodified
checkout of [TriCNES](https://github.com/100thCoin/TriCNES) (WinForms, so the
GUI does not run here). It needs the .NET 8 SDK (`DOTNET_ROOT` if the
SDK is not on the default linker path). It does not need libgdiplus.
The measured SMB3 run used TriCNES `94f1b1178057e84c56e4c2410c8391eee4d78975`.

```sh
dotnet build -c Release -p:TriCNESRoot=/path/to/TriCNES \
  tools/compare/tricnes-dump/tricnes-dump.csproj
tools/compare/tricnes-dump/bin/Release/net8.0/tricnes-dump \
  --rom "Super Mario Bros. 3 (USA).nes" --movie smb3-warps.fm2 \
  --out ./tricnes-smb3 --limit 34100
```

`emu.hashes.tsv` is `movie_frame`, `lag`, CRC-32 of the 256x240 RGB picture
(`kind` `rgb`). `emu.edges.tsv` is the same columns for the palette-free edge
map of rows 8..231 (`kind` `edges`). Exact edge CRCs miss sprite flicker: on
SMB3 they match FCEUX on the title and then almost never, so they do not
reproduce the published slip. The slip metric is colour-map leftover ≤ 32
(`tasty-compare --system nes`). `--png DIR --png-spec 1-1600:1,33000-34100:1`
writes that crop as `frame-NNNNNN.png` (256x224 RGB, rows 8..231). Pass
that directory as `--emu`. The full RGB hash will not match a MiSTer
sidecar: palettes differ, and the sidecar is 256x224.

`--ppu-phase` is 0..3 and `--cpu-phase` is 0..11 (the TAS dialog's two
alignment boxes). `--fceux-frame0 true` is the default, and it is the checked
box for an `.fm2`. `--ppu-reset` is the power-on flag the GUI leaves false.
An `.fm2` also starts from CPU phase 0 and the RAM pattern
`00 00 00 00 00 FF FF FF`. Frame numbers count frame advances from 1. With
frame-0 timing on, advance 1 is the short pre-frame, so TriCNES frame N is
FCEUX `emu.framecount()` N−1. On SMB3 3922M, CPU phase 0 leaves FCEUX by
frame 612. Phases 8, 9, 10 and 11 stay with it through the opening. Phase
10, run to frame 34100, takes the same one-frame step as MiSTer at 33878.

**Any emulator:** dump a lossless AVI or a PNG sequence from power-on, then
`--emu path`. RGB bijection covers palette differences; `--crop` and
`--system` cover geometry.

A 3-frame leftover streak is the first difference (1-frame HUD spikes are
ignored). Then offsets −3..+3 are tried for a slip. `missed` sidecar rows
are skipped. FCEUX `lag=1` rows stay (they are the previous picture).

## Reproduce the SMB3 desync

TASVideos [3922M](https://tasvideos.org/3922M) “warps” by Lord_Tom, Maru and
Tompa (37,522 frames). On the NES core (NTSC, RAM Clear `$00`) the game slips
one frame behind FCEUX in the World 8 tank fight.

On the MiSTer (your ROM, never committed):

```sh
tasty play smb3-warps.fm2 --rom "Super Mario Bros. 3 (USA).nes" \
  --record /media/fat/recordings --hashes
```

Copy the recording to the PC. Dump FCEUX from power-on (`os.exit` finishes a
headless dump; wrap with `timeout -k 2` if Qt hangs):

```sh
export TASTY_DUMP_DIR=./emu-smb3 TASTY_DUMP_PNG=1 TASTY_DUMP_NOHASH=1
export TASTY_DUMP_LIMIT=37525
fceux --sound 0 --playmov smb3-warps.fm2 --loadlua tools/compare/fceux-dump.lua \
  "Super Mario Bros. 3 (USA).nes"
```

```sh
tools/compare/tasty-compare --mister recordings/smb3-warps.frames.tsv \
  --emu emu-smb3 --system nes --auto-offset --out ./compare-out
```

Expected:

```
skipped 61 missed sidecar rows
first difference: movie frame 33905 (core frame 34208), offset 0
from movie frame 33878 (core frame 34181) the pictures match again at offset +1 (was +0, delta +1)
first unique picture: movie frame 33905 (core frame 34208)
```

33878 is the first frame that needs offset +1 (FCEUX lagged 33869–33878
in the black pipe). A dump named `emu.framecount()−1` reports the same
slip as offset −1→0; this dumper names `emu.framecount()`, so 0→+1.
33905 leftover is 122 at every offset −3..+3. SMB1 vs FCEUX is leftover 0
on every frame.

## Tests

```sh
python3 tools/compare/test_compare.py
```

Set `TASTY_COMPARE_RECORDINGS` to a directory of `smb1-avi.frames.tsv` (and
`r2/smb1-hash-1.frames.tsv`) to include the real-recording checks.
