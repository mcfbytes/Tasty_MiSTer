#!/usr/bin/env bash
# Build Slint's C++ runtime (software renderer only) as a static archive for the static armhf build.
#
# Installs a CMake package into $TASTY_STATIC_DEPS/slint (default $TASTY_TOOLCHAIN_ROOT/opt/slint), the
# directory the armhf-static preset looks in. The source is Slint's own release tarball plus five
# patches, applied in order: 0001-0003 are the MiSTer Linux image's (Buildroot_MiSTer, package/slint,
# fetched at a pinned commit), 0004-0005 are in src/tools/slint/. Rust is pinned below.
#
#   TASTY_TOOLCHAIN_ROOT   the Bootlin armv7-eabihf glibc toolchain (required)
#   TASTY_STATIC_DEPS      install root (default $TASTY_TOOLCHAIN_ROOT/opt)
#   TASTY_STATIC_DEPS_SRC  download and build directory (default <install root>/../tasty-static-deps-src)
#   SLINT_SOURCE_TARBALL   a local copy of the release tarball, or of one with its crates vendored
#   SLINT_COMPILER         a host slint-compiler (default: the release's x86_64 one, fetched by SHA-256)
#   Corrosion_DIR          an installed Corrosion (default: Slint's CMake fetches it)
set -euo pipefail

SLINT_VERSION=1.18.1
SLINT_URL="https://github.com/slint-ui/slint/archive/v${SLINT_VERSION}/slint-${SLINT_VERSION}.tar.gz"
SLINT_SHA256=fe485305ed303215e76c04918ee9aefbffbe229f18f979098ec36c7fa1dab28b
PATCH_REPO="https://raw.githubusercontent.com/mcfbytes/Buildroot_MiSTer/1e774eefd15e25b536176f5393894308d2b72a78/package/slint"
IMAGE_PATCHES=(
  "0001-api-cpp-dlopen-libfontconfig-instead-of-linking-it.patch 3688b77bf01d1e8fdf96a72308b990e88e75a093ef5a28becebcf42f913598de"
  "0002-api-cpp-add-an-ARGB8888-software-renderer-target.patch 0edf25c64c078594b542373e315c03a97845b5fa6884e6265fce8b817af0b3eb"
  "0003-api-cpp-let-the-caller-drive-the-animation-clock.patch 990d1a97cce8537fd72ee40094b96bd7a5a2c9caf77cb6693c8f20b8007f3fe5"
)
# The host compiler that turns the HD menu's .slint into C++: installed as $prefix/bin/slint-compiler.
COMPILER_URL="https://github.com/slint-ui/slint/releases/download/v${SLINT_VERSION}/slint-compiler-Linux-x86_64.tar.gz"
COMPILER_SHA256=543091e6102a02db3403aa77c774a11d9f383b31cb03e25cddad63269a1fb779
# slint-notices.py reads this pin; the two move together.
RUST_TOOLCHAIN=${RUST_TOOLCHAIN:-1.97.0}
RUST_TARGET=armv7-unknown-linux-gnueabihf

here=$(cd "$(dirname "$0")" && pwd)
tree=$(cd "$here/../.." && pwd)
TC=${TASTY_TOOLCHAIN_ROOT:?set TASTY_TOOLCHAIN_ROOT to the Bootlin armv7 glibc toolchain}
deps=${TASTY_STATIC_DEPS:-$TC/opt}
prefix=$deps/slint
work=${TASTY_STATIC_DEPS_SRC:-$(dirname "$deps")/tasty-static-deps-src}/slint
X=$TC/bin/arm-buildroot-linux-gnueabihf-
mkdir -p "$work"

fetch() {  # url dest sha256
  if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status -; then return 0; fi
  echo "fetch $1"
  curl -fsSL --retry 3 -o "$2.part" "$1"
  echo "$3  $2.part" | sha256sum -c --quiet - || { rm -f "$2.part"; echo "sha256 mismatch: $1" >&2; exit 1; }
  mv "$2.part" "$2"
}

tarball=${SLINT_SOURCE_TARBALL:-$work/slint-$SLINT_VERSION.tar.gz}
[ -n "${SLINT_SOURCE_TARBALL:-}" ] || fetch "$SLINT_URL" "$tarball" "$SLINT_SHA256"
patches=()
for row in "${IMAGE_PATCHES[@]}"; do
  set -- $row
  fetch "$PATCH_REPO/$1" "$work/$1" "$2"
  patches+=("$work/$1")
done
patches+=("$here"/slint/0004-*.patch "$here"/slint/0005-*.patch)
compiler=${SLINT_COMPILER:-}
if [ -z "$compiler" ]; then
  fetch "$COMPILER_URL" "$work/slint-compiler-$SLINT_VERSION.tar.gz" "$COMPILER_SHA256"
  rm -rf "$work/slint-compiler-$SLINT_VERSION"
  mkdir -p "$work/slint-compiler-$SLINT_VERSION"
  tar xzf "$work/slint-compiler-$SLINT_VERSION.tar.gz" -C "$work/slint-compiler-$SLINT_VERSION" slint-compiler
  compiler=$work/slint-compiler-$SLINT_VERSION/slint-compiler
fi

# A fresh tree every run, so the patched source is exactly the tarball plus the five patches.
src=$work/slint-$SLINT_VERSION
rm -rf "$src"
tar xzf "$tarball" -C "$work"
for p in "${patches[@]}"; do
  echo "patch $(basename "$p")"
  patch -d "$src" -p1 --forward --batch --no-backup-if-mismatch < "$p"
done

export PATH=$HOME/.cargo/bin:$PATH
if command -v rustup >/dev/null; then
  rustup toolchain install "$RUST_TOOLCHAIN" --profile minimal --target "$RUST_TARGET" >/dev/null
fi
export RUSTUP_TOOLCHAIN=$RUST_TOOLCHAIN
export CARGO_BUILD_JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}
if [ -f "$src/.cargo/config.toml" ] && grep -q vendored-sources "$src/.cargo/config.toml"; then
  export CARGO_NET_OFFLINE=true
fi
T=ARMV7_UNKNOWN_LINUX_GNUEABIHF
export CARGO_TARGET_${T}_LINKER=${X}gcc
export CC_armv7_unknown_linux_gnueabihf=${X}gcc CXX_armv7_unknown_linux_gnueabihf=${X}g++
export AR_armv7_unknown_linux_gnueabihf=${X}ar
# The MiSTer image's target flags, as build-static-deps.py.
A9="-mcpu=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=hard -marm"
cargo_home=${CARGO_HOME:-$HOME/.cargo}
# Build paths (the builder's home, this work tree) are mapped to neutral prefixes: a panic location
# or __FILE__ names /cargo/... or /build/..., and the archive does not depend on where it was built.
BRF="-ffile-prefix-map=$work=/build -O2 -g0 -D_FORTIFY_SOURCE=1 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64"
export CFLAGS_armv7_unknown_linux_gnueabihf="$A9 $BRF" CXXFLAGS_armv7_unknown_linux_gnueabihf="$A9 $BRF"
# crt-static: std unwinds through libgcc_eh, not libgcc_s, so the archive links under -static.
export CARGO_TARGET_${T}_RUSTFLAGS="-C target-cpu=cortex-a9 -C target-feature=+neon,+crt-static \
--remap-path-prefix=$cargo_home=/cargo --remap-path-prefix=$work=/build"
# One fat-LTO unit that aborts on panic: about a third of the archive the default profile makes.
export CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1 CARGO_PROFILE_RELEASE_PANIC=abort

bld=$work/build
rm -rf "$bld" "$prefix"
install -D -m 755 "$compiler" "$prefix/bin/slint-compiler"
cmake -S "$src/api/cpp" -B "$bld" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$tree/cmake/arm-linux-gnueabihf-static.cmake" -DTASTY_TOOLCHAIN_ROOT="$TC" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="$BRF" -DCMAKE_CXX_FLAGS_RELEASE="$BRF" \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF \
  ${Corrosion_DIR:+-DCorrosion_DIR="$Corrosion_DIR" -DFETCHCONTENT_FULLY_DISCONNECTED=ON} \
  -DRust_TOOLCHAIN="$RUST_TOOLCHAIN" -DRust_CARGO_TARGET="$RUST_TARGET" \
  -DSLINT_COMPILER="$prefix/bin/slint-compiler" -DSLINT_LIBRARY_CARGO_FLAGS=--locked \
  -DSLINT_BUILD_TESTING=OFF -DSLINT_BUILD_EXAMPLES=OFF \
  -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DSLINT_FEATURE_EXPERIMENTAL=OFF \
  -DSLINT_FEATURE_BACKEND_WINIT=OFF -DSLINT_FEATURE_RENDERER_FEMTOVG=OFF -DSLINT_FEATURE_RENDERER_SKIA=OFF \
  -DSLINT_FEATURE_BACKEND_QT=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBSEAT=OFF \
  -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBINPUT=OFF -DSLINT_FEATURE_INTERPRETER=OFF -DSLINT_FEATURE_LIVE_PREVIEW=OFF \
  -DSLINT_FEATURE_ACCESSIBILITY=OFF -DSLINT_FEATURE_SYSTEM_TRAY=OFF -DSLINT_FEATURE_GETTEXT=OFF \
  -DSLINT_FEATURE_TESTING=OFF
cmake --build "$bld"
cmake --install "$bld"

# The archive carries Rust's compiler_builtins (__aeabi_*, cbrt, ceilf, fmaf, ...). Left global, the
# linker takes them for the C++ code too, and with them Rust's unwinder into binaries that never call
# Slint. A shared library hides them; a static archive must localize every one libc/libm/libgcc defines.
a=$prefix/lib/libslint_cpp.a
sr=$("${X}gcc" -print-sysroot)
lg=$("${X}gcc" -print-libgcc-file-name)
comm -12 \
  <("${X}nm" -g --defined-only "$a" 2>/dev/null | awk 'NF==3{print $3}' | sort -u) \
  <("${X}nm" -g --defined-only "$sr/usr/lib/libc.a" "$sr/usr/lib/libm.a" "$lg" "$(dirname "$lg")/libgcc_eh.a" \
      2>/dev/null | awk 'NF==3{print $3}' | sort -u) > "$bld/localize.txt"
"${X}objcopy" --localize-symbols="$bld/localize.txt" "$a" "$a.tmp"
mv "$a.tmp" "$a"
{
  echo "slint $SLINT_VERSION"
  echo "rust $RUST_TOOLCHAIN $RUST_TARGET"
  echo "slint-compiler $(sha256sum < "$prefix/bin/slint-compiler" | cut -d' ' -f1)"
  for p in "${patches[@]}"; do sha256sum "$p" | awk -v n="$(basename "$p")" '{print "patch", n, $1}'; done
  echo "localized $(wc -l < "$bld/localize.txt")"
} > "$prefix/BUILD-INFO"
cp "$src/LICENSE.md" "$prefix/LICENSE.md"
echo "static Slint in $prefix ($(stat -c%s "$a") bytes)"
