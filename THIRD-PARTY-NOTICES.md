# Third-party notices

The release binary, `tasty`, is linked statically. Everything below is inside it; this repository's
own code is GPL-3.0-or-later (`LICENSE`).

| component | version | licence | where |
|---|---|---|---|
| Slint (C++ runtime, software renderer) | 1.18.1, patched | GPL-3.0-only (one of Slint's three licences) | see "Slint" below |
| Rust crates under Slint | see the list | MIT, Apache-2.0, BSD, Zlib, and others | `licenses/slint-crates.md` |
| Rust standard library (core, alloc, std, compiler_builtins) | 1.97.0 | MIT OR Apache-2.0 | `licenses/rust-LICENSE-MIT.txt`, `licenses/rust-LICENSE-APACHE.txt`, `licenses/rust-COPYRIGHT.txt` |
| Noto Sans Regular and Bold | 2.015 | OFL-1.1 | `licenses/NotoSans-OFL.txt` |
| GNU C Library (glibc) | 2.44-36-g2d5421ff (Bootlin armv7-eabihf glibc stable-2026.08-1) | LGPL-2.1-or-later | `licenses/glibc-LGPL-2.1.txt`, `licenses/glibc-LICENSES.txt`; see "glibc" below |
| libstdc++, libgcc | the same toolchain | GPL-3.0 with the GCC Runtime Library Exception | https://gcc.gnu.org |
| zlib-ng (zlib-compatible) | 2.3.3 | Zlib | https://github.com/zlib-ng/zlib-ng |
| minizip (zlib contrib) | 1.3.2 | Zlib | https://github.com/madler/zlib |
| LZO | 2.10 | GPL-2.0-or-later | https://www.oberhumer.com/opensource/lzo/ |
| libchdr | 607694ca | BSD-3-Clause | `licenses/libchdr-LICENSE.txt`; https://github.com/rtissera/libchdr |
| LZMA SDK (inside libchdr) | 26.02 | public domain | https://www.7-zip.org/sdk.html |
| Zstandard decoder (inside libchdr) | 1.5.7 | BSD-3-Clause | `licenses/zstd-LICENSE.txt`; https://github.com/facebook/zstd |

`src/tools/build-static-deps.py` fetches every C library and the fonts by URL and SHA-256, and
`src/tools/build-static-slint.sh` builds Slint; both are how the release is built.

## Slint

Slint is used under the GNU GPL, version 3 (`GPL-3.0-only`), one of the three licences it is offered
under (`licenses/Slint-LICENSE.md`). The GPL's text is this repository's `LICENSE`. The binary
links a modified Slint. Its complete corresponding source is:

- Slint 1.18.1's release source, https://github.com/slint-ui/slint/archive/v1.18.1/slint-1.18.1.tar.gz
  (SHA-256 in `src/tools/build-static-slint.sh`);
- patches 0001-0003, from the MiSTer Linux image's Slint package:
  https://github.com/mcfbytes/Buildroot_MiSTer/tree/1e774eefd15e25b536176f5393894308d2b72a78/package/slint
  (an ARGB8888 software-renderer target, a caller-driven animation clock, fontconfig loaded at run time);
- patches 0004-0005 in `src/tools/slint/` (fontconfig left out entirely, and the complex-script
  line-breaking dictionaries left out);
- the build itself, `src/tools/build-static-slint.sh`, applying them in that order.

The C++ for the `.slint` files is generated at build time by the Slint 1.18.1 release's `slint-compiler` (fetched
by SHA-256 in the same script); the generated code is part of this repository's build, not of Slint.

The Rust crates it is built from are listed, with their licence texts, in `licenses/slint-crates.md`
(`src/tools/slint-notices.py` writes it).

## glibc

The GNU C Library is linked statically, under the GNU Lesser General Public License, version 2.1 or
later. Its text is `licenses/glibc-LGPL-2.1.txt`; the notices of the permissively licensed parts of
glibc are `licenses/glibc-LICENSES.txt`.

- Source: glibc 2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3, as built into Bootlin's
  armv7-eabihf glibc stable-2026.08-1 toolchain, unmodified (Buildroot applies no glibc patches):
  https://toolchains.bootlin.com/downloads/releases/sources/glibc-2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3/
  (`glibc-2.44-36-g2d5421ffca8893534d5e02ad38c28acd8e778fa3-git4.tar.gz`, SHA-256
  39f6808e31a02da42f774912c6754ea22d5f076c4e935b1e233f3602d4d772c0). The toolchain's own recipe is
  https://github.com/bootlin/buildroot-toolchains at tag `toolchains.bootlin.com-2026.08-1`.

### Relinking

LGPL-2.1 section 6 lets a work that uses the library be linked with it statically, provided the user
can relink the work against a modified library. This repository is that provision: it holds the
program's complete source, and the scripts that fetch every library above by URL and SHA-256 and
build the binary (`CMakePresets.json` preset `armhf-static`,
`src/tools/build-static-deps.py`, `src/tools/build-static-slint.sh`, `.github/workflows/ci.yml`).
To relink against a modified glibc, build that glibc into the toolchain (or replace its `libc.a`),
point `TASTY_TOOLCHAIN_ROOT` at it, and run the same steps.

## Noto Sans

The binary embeds NotoSans-Regular.ttf and NotoSans-Bold.ttf from the Noto Sans 2.015 release
(https://github.com/notofonts/latin-greek-cyrillic), unmodified, under the SIL Open Font License 1.1
(`licenses/NotoSans-OFL.txt`).
