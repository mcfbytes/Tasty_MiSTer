<p align="center"><img src="art/banner.png" alt="MiSTer Tasty" width="100%"></p>

# MiSTer Tasty

**Tool-assisted replays on real MiSTer FPGA cores.**

MiSTer Tasty plays published TAS movies back on a MiSTer, right from the DE10-Nano's own ARM side, with no replay
bot on the controller port and no extra hardware. Each input is fed to the core on the frame the movie asks for.
Watch a run play out on the real gateware, or use it to check a core's timing against the emulator it was
recorded on.

The command-line tool is `tasty`.

> **Status:** early and hungry. No code has been published yet; this repository is the table being set.

## The menu (planned CLI)

Subject to change until the first release.

```text
tasty play <movie> [options]      load the core and ROM, then replay the movie
tasty info <movie>                format, system, frame count, rerecords, ROM checksum
tasty check <movie> --rom <file>  does this ROM match the movie's checksum?
tasty status                      what is playing or recording, frame N of M, sync counters
tasty stop                        stop playback and any recording
tasty rec start|stop [options]    record whatever is on screen, no movie needed
```

**Playback**

| option | meaning |
|---|---|
| `--rom <file>` | the ROM to boot (default: look it up by the movie's checksum) |
| `--core <rbf>` | the core to load (default: the one for the movie's system) |
| `--lead <frames>` | shift the whole movie by N frames (default: the core's measured power-on delay) |
| `--ram-init zero\|ff\|random` | power-on RAM fill, where the core supports it |
| `--stop-at <frame>` | stop at this movie frame |
| `--loop` | start over when the movie ends |

**Recording** (works with `tasty play` and `tasty rec start`)

| option | meaning |
|---|---|
| `--record <dir>` | record to this directory; a local path or a mounted network share |
| `--codec cscd\|zmbv` | `cscd` (default): lossless and cheapest on the CPU. `zmbv`: smaller files |
| `--scale auto\|native\|half` | `auto` (default) records at native resolution and drops to half scale only if the CPU can't keep up |
| `--every <n>` | keep every Nth frame, for long runs or tight disks |
| `--from <frame>` `--to <frame>` | record only this slice of the movie |
| `--segment <size>` | split files at this size (default `1G`, FAT32-safe) |
| `--hashes` | also write `<name>.frames.tsv`: one line per frame with the core frame, the movie frame and a pixel hash |
| `--hashes-only` | write the hash log only, no video; the fastest way to find the exact frame where a replay desyncs |

Recordings are the core's own pixels, as it hands them to the scaler: native resolution, no filters, in plain AVI that
`ffmpeg` reads directly:

```sh
tasty play smb3.fm2 --record /media/fat/recordings --hashes
ffmpeg -i smb3_000.avi -c:v libx264 -crf 16 -pix_fmt yuv420p smb3.mp4
```

Recording is best effort. If the CPU or storage can't keep up, a frame is repeated and counted, and the replay never
waits for the recording.

## The art

Meet Tasty Kun: MiSTer Kun, sticking his tongue out.

| | | |
|:-:|:-:|:-:|
| <img src="art/tasty-kun.png" width="200" alt="Tasty Kun"> | <img src="art/tasty-kun-donut.png" width="200" alt="Tasty Kun with a donut"> | <img src="art/tasty-kun-pizza.png" width="200" alt="Tasty Kun with pizza"> |
| **Plain** | **Donut** | **Pizza** |
| <img src="art/tasty-kun-onigiri.png" width="200" alt="Tasty Kun with onigiri"> | <img src="art/tasty-kun-icecream.png" width="200" alt="Tasty Kun with ice cream"> | <img src="art/tasty-kun-sushi.png" width="200" alt="Tasty Kun with sushi"> |
| **Onigiri** | **Ice cream** | **Sushi** |

There is a pixel version too: <img src="art/tasty-kun-8bit-32x32.png" width="32" alt="8-bit Tasty Kun"> at 32x32
([big](art/tasty-kun-8bit.png)). Every variant comes as SVG and PNG in [`art/`](art/), along with the
[banner](art/banner.png) and the [social preview](art/social-preview.png).

## Credits and licence

- MiSTer Kun was created by [HeWhoisRed](https://github.com/Hewhoisred) as a gift to the MiSTer community and
  remastered by [baxysquare](https://github.com/baxysquare/mister_kun). The artwork here is derived from that
  remaster and shared on the same terms: use it and remix it freely, and credit the original creator where you can.
- The code will be released under the [GNU General Public License v3.0](LICENSE).
- MiSTer Tasty is an independent community project, not affiliated with the MiSTer FPGA project.
