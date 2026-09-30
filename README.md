<p align="center"><img src="art/banner.png" alt="MiSTer Tasty" width="100%"></p>

# MiSTer Tasty

**Tool-assisted replays on real MiSTer FPGA cores.**

MiSTer Tasty plays published TAS movies back on a MiSTer, right from the DE10-Nano's own ARM side, with no replay
bot on the controller port and no extra hardware. Each input is fed to the core on the frame the movie asks for.
Watch a run play out on the real gateware, or use it to check a core's timing against the emulator it was
recorded on.

The command-line tool is `tasty`.

> **Status:** early and hungry. No code has been published yet; this repository is the table being set.

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
