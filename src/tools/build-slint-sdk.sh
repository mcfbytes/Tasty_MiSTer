#!/usr/bin/env bash

set -euo pipefail

SLINT_VERSION=1.19.0
SLINT_URL="https://github.com/slint-ui/slint/archive/v${SLINT_VERSION}/slint-${SLINT_VERSION}.tar.gz"
SLINT_SHA256=9c15a9f6dfe94fbe96cb97dcb38207907403a67c9b23d8a93468ea1bb132246c
PATCH_REPO="https://raw.githubusercontent.com/mcfbytes/Buildroot_MiSTer/eae07567a1349037fed4f723bec145c55ca9025a/package/slint"
P0001=0001-api-cpp-dlopen-libfontconfig-instead-of-linking-it.patch
P0001_SHA256=34b07553dd659dd4fb6dee944eb0c6d090b6edf12609ca71a54e488734dcd1e6
COMPILER_URL="https://github.com/slint-ui/slint/releases/download/v${SLINT_VERSION}/slint-compiler-Linux-x86_64.tar.gz"
COMPILER_SHA256=40f50891de8b78c59cd04158c4f55d3f9ee45f8908aacc22192d76ffc0b7693a
RUST_TOOLCHAIN=${RUST_TOOLCHAIN:-1.97.0}
RUST_TARGET=armv7-unknown-linux-gnueabihf
TRIPLE=armv7-linux-gnueabihf
HOST_RUST_TARGET=x86_64-unknown-linux-gnu
HOST_TRIPLE=x86_64-linux-gnu
A64_RUST_TARGET=aarch64-unknown-linux-gnu
A64_TRIPLE=aarch64-linux-gnu
TRIPLES=${SLINT_SDK_TRIPLES:-"$TRIPLE $HOST_TRIPLE $A64_TRIPLE"}
want() { case " $TRIPLES " in *" $1 "*) return 0;; esac; return 1; }

here=$(cd "$(dirname "$0")" && pwd)
top=$(git -C "$here" rev-parse --show-toplevel 2>/dev/null || pwd)
desk=$(dirname "$top")
repo=$top

patchdir=${SLINT_PATCH_DIR:-}
if [ -z "$patchdir" ]; then
  for d in "$here/slint" "$repo"/*/tools/tasty-export/overlay/src/tools/slint; do
    [ -d "$d" ] && { patchdir=$d; break; }
  done
fi
[ -d "$patchdir" ] || { echo "no directory with patches 0004 and 0005 (set SLINT_PATCH_DIR)" >&2; exit 1; }
out=${1:-$desk/slint-sdk-out}
rev=${SLINT_SDK_REV:-1}
TC=${SLINT_TOOLCHAIN_ROOT:-$desk/tools/armv7-eabihf--glibc--stable-2026.08-1}
work=${SLINT_SDK_WORK:-$desk/tools/slint-sdk-work}
[ -n "${SLINT_SDK_ANYWHERE:-}" ] || case "$out$work" in "$HOME"/*|/tmp/*) echo "refusing: output and work stay off ~ and /tmp (SLINT_SDK_ANYWHERE=1 overrides)" >&2; exit 1;; esac
want "$TRIPLE" && { [ -x "$TC/bin/arm-buildroot-linux-gnueabihf-gcc" ] || { echo "no Bootlin toolchain at $TC" >&2; exit 1; }; }
X=$TC/bin/arm-buildroot-linux-gnueabihf-
mkdir -p "$work" "$out"

fetch() {
  if [ -f "$2" ] && echo "$3  $2" | sha256sum -c --status -; then return 0; fi
  echo "fetch $1"
  curl -fsSL --retry 3 -o "$2.part" "$1"
  echo "$3  $2.part" | sha256sum -c --quiet - || { rm -f "$2.part"; echo "sha256 mismatch: $1" >&2; exit 1; }
  mv "$2.part" "$2"
}

tarball=${SLINT_SOURCE_TARBALL:-$work/slint-$SLINT_VERSION.tar.gz}
if [ -n "${SLINT_SOURCE_TARBALL:-}" ]; then echo "$SLINT_SHA256  $tarball" | sha256sum -c --quiet -
else fetch "$SLINT_URL" "$tarball" "$SLINT_SHA256"; fi
fetch "$PATCH_REPO/$P0001" "$work/$P0001" "$P0001_SHA256"
patches=("$work/$P0001" "$patchdir"/0004-*.patch "$patchdir"/0005-*.patch)
fetch "$COMPILER_URL" "$work/slint-compiler-$SLINT_VERSION.tar.gz" "$COMPILER_SHA256"
rm -rf "$work/compiler"; mkdir -p "$work/compiler"
tar xzf "$work/slint-compiler-$SLINT_VERSION.tar.gz" -C "$work/compiler" slint-compiler

src=$work/slint-$SLINT_VERSION
rm -rf "$src"
tar xzf "$tarball" -C "$work"
for p in "${patches[@]}"; do
  echo "patch $(basename "$p")"
  patch -d "$src" -p1 --forward --batch --no-backup-if-mismatch < "$p"
done

export CARGO_HOME=${CARGO_HOME:-$work/cargo-home}
export PATH=$HOME/.cargo/bin:$PATH
if command -v rustup >/dev/null && ! rustup toolchain list | grep -q "^$RUST_TOOLCHAIN"; then
  rustup toolchain install "$RUST_TOOLCHAIN" --profile minimal --target "$RUST_TARGET" --target "$HOST_RUST_TARGET" \
    --target "$A64_RUST_TARGET" >/dev/null
elif command -v rustup >/dev/null; then
  rustup target add --toolchain "$RUST_TOOLCHAIN" "$RUST_TARGET" "$HOST_RUST_TARGET" "$A64_RUST_TARGET" >/dev/null
fi
export RUSTUP_TOOLCHAIN=$RUST_TOOLCHAIN
export CARGO_BUILD_JOBS=${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}
T=ARMV7_UNKNOWN_LINUX_GNUEABIHF
export CARGO_TARGET_${T}_LINKER=${X}gcc
export CC_armv7_unknown_linux_gnueabihf=${X}gcc CXX_armv7_unknown_linux_gnueabihf=${X}g++
export AR_armv7_unknown_linux_gnueabihf=${X}ar
A9="-mcpu=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=hard -marm"
BRF="-ffile-prefix-map=$work=/build -O2 -g0 -D_FORTIFY_SOURCE=1 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_TIME_BITS=64"
export CFLAGS_armv7_unknown_linux_gnueabihf="$A9 $BRF" CXXFLAGS_armv7_unknown_linux_gnueabihf="$A9 $BRF"

export CARGO_TARGET_${T}_RUSTFLAGS="-C target-cpu=cortex-a9 -C target-feature=+neon,+crt-static \
--remap-path-prefix=$CARGO_HOME=/cargo --remap-path-prefix=$work=/build"
export CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1 CARGO_PROFILE_RELEASE_PANIC=abort

stage=$work/stage; prefix=$stage/$TRIPLE; bld=$work/build
rm -rf "$stage" "$bld"; mkdir -p "$stage/bin"
install -m 755 "$work/compiler/slint-compiler" "$stage/bin/slint-compiler"
if want "$TRIPLE"; then
cat > "$work/toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER ${X}gcc)
set(CMAKE_CXX_COMPILER ${X}g++)
set(CMAKE_FIND_ROOT_PATH $TC/arm-buildroot-linux-gnueabihf/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
add_compile_options($A9)
TC
cmake -S "$src/api/cpp" -B "$bld" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$work/toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="$BRF" -DCMAKE_CXX_FLAGS_RELEASE="$BRF" \
  -DCMAKE_INSTALL_PREFIX="$prefix" -DBUILD_SHARED_LIBS=OFF \
  -DRust_TOOLCHAIN="$RUST_TOOLCHAIN" -DRust_CARGO_TARGET="$RUST_TARGET" \
  -DSLINT_COMPILER="$stage/bin/slint-compiler" -DSLINT_LIBRARY_CARGO_FLAGS=--locked \
  -DSLINT_BUILD_TESTING=OFF -DSLINT_BUILD_EXAMPLES=OFF \
  -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DSLINT_FEATURE_EXPERIMENTAL=OFF \
  -DSLINT_FEATURE_BACKEND_WINIT=OFF -DSLINT_FEATURE_RENDERER_FEMTOVG=OFF -DSLINT_FEATURE_RENDERER_SKIA=OFF \
  -DSLINT_FEATURE_BACKEND_QT=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBSEAT=OFF \
  -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBINPUT=OFF -DSLINT_FEATURE_INTERPRETER=OFF -DSLINT_FEATURE_LIVE_PREVIEW=OFF \
  -DSLINT_FEATURE_ACCESSIBILITY=OFF -DSLINT_FEATURE_SYSTEM_TRAY=OFF -DSLINT_FEATURE_GETTEXT=OFF \
  -DSLINT_FEATURE_TESTING=OFF
cmake --build "$bld"
cmake --install "$bld"

a=$prefix/lib/libslint_cpp.a
sr=$("${X}gcc" -print-sysroot)
lg=$("${X}gcc" -print-libgcc-file-name)
comm -12 \
  <("${X}nm" -g --defined-only "$a" 2>/dev/null | awk 'NF==3{print $3}' | sort -u) \
  <("${X}nm" -g --defined-only "$sr/usr/lib/libc.a" "$sr/usr/lib/libm.a" "$lg" "$(dirname "$lg")/libgcc_eh.a" \
      2>/dev/null | awk 'NF==3{print $3}' | sort -u) > "$bld/localize.txt"
"${X}objcopy" --localize-symbols="$bld/localize.txt" "$a" "$a.tmp"
mv "$a.tmp" "$a"
fi

if want "$HOST_TRIPLE"; then
hprefix=$stage/$HOST_TRIPLE; hbld=$work/build-host
rm -rf "$hbld"
env -u CARGO_TARGET_${T}_RUSTFLAGS \
cmake -S "$src/api/cpp" -B "$hbld" -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="-ffile-prefix-map=$work=/build -O2 -g0" \
  -DCMAKE_CXX_FLAGS_RELEASE="-ffile-prefix-map=$work=/build -O2 -g0" \
  -DCMAKE_INSTALL_PREFIX="$hprefix" -DBUILD_SHARED_LIBS=OFF \
  -DRust_TOOLCHAIN="$RUST_TOOLCHAIN" -DRust_CARGO_TARGET="$HOST_RUST_TARGET" \
  -DSLINT_COMPILER="$stage/bin/slint-compiler" -DSLINT_LIBRARY_CARGO_FLAGS=--locked \
  -DSLINT_BUILD_TESTING=OFF -DSLINT_BUILD_EXAMPLES=OFF \
  -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DSLINT_FEATURE_EXPERIMENTAL=OFF \
  -DSLINT_FEATURE_BACKEND_WINIT=OFF -DSLINT_FEATURE_RENDERER_FEMTOVG=OFF -DSLINT_FEATURE_RENDERER_SKIA=OFF \
  -DSLINT_FEATURE_BACKEND_QT=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBSEAT=OFF \
  -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBINPUT=OFF -DSLINT_FEATURE_INTERPRETER=OFF -DSLINT_FEATURE_LIVE_PREVIEW=OFF \
  -DSLINT_FEATURE_ACCESSIBILITY=OFF -DSLINT_FEATURE_SYSTEM_TRAY=OFF -DSLINT_FEATURE_GETTEXT=OFF \
  -DSLINT_FEATURE_TESTING=OFF
cmake --build "$hbld"
cmake --install "$hbld"
ha=$hprefix/lib/libslint_cpp.a
hl=$(gcc -print-libgcc-file-name)
comm -12 \
  <(nm -g --defined-only "$ha" 2>/dev/null | awk 'NF==3{print $3}' | sort -u) \
  <(nm -g --defined-only "$(gcc -print-file-name=libc.a)" "$(gcc -print-file-name=libm.a)" "$hl" "$(dirname "$hl")/libgcc_eh.a" \
      2>/dev/null | awk 'NF==3{print $3}' | sort -u) > "$hbld/localize.txt"
objcopy --localize-symbols="$hbld/localize.txt" "$ha" "$ha.tmp"
mv "$ha.tmp" "$ha"
fi

if want "$A64_TRIPLE"; then
A=${SLINT_AARCH64_PREFIX:-aarch64-linux-gnu-}
command -v "${A}g++" >/dev/null || { echo "no aarch64 cross g++ (${A}g++)" >&2; exit 1; }
asr=$("${A}gcc" -print-sysroot); case "$asr" in ""|/) asr=/usr/aarch64-linux-gnu;; esac; [ -f "$asr/lib/libc.a" ] || { echo "no aarch64 libc.a under $asr" >&2; exit 1; }
aprefix=$stage/$A64_TRIPLE; abld=$work/build-aarch64
rm -rf "$abld"
U=AARCH64_UNKNOWN_LINUX_GNU
export CARGO_TARGET_${U}_LINKER=${A}gcc
export CC_aarch64_unknown_linux_gnu=${A}gcc CXX_aarch64_unknown_linux_gnu=${A}g++ AR_aarch64_unknown_linux_gnu=${A}ar
ABRF="-ffile-prefix-map=$work=/build -O2 -g0 -D_FORTIFY_SOURCE=1"
export CFLAGS_aarch64_unknown_linux_gnu="$ABRF" CXXFLAGS_aarch64_unknown_linux_gnu="$ABRF"
export CARGO_TARGET_${U}_RUSTFLAGS="-C target-feature=+crt-static --remap-path-prefix=$CARGO_HOME=/cargo --remap-path-prefix=$work=/build"
cat > "$work/toolchain-aarch64.cmake" <<TC
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${A}gcc)
set(CMAKE_CXX_COMPILER ${A}g++)
set(CMAKE_FIND_ROOT_PATH $asr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
TC
env -u CARGO_TARGET_${T}_RUSTFLAGS \
cmake -S "$src/api/cpp" -B "$abld" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$work/toolchain-aarch64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE="$ABRF" -DCMAKE_CXX_FLAGS_RELEASE="$ABRF" \
  -DCMAKE_INSTALL_PREFIX="$aprefix" -DBUILD_SHARED_LIBS=OFF \
  -DRust_TOOLCHAIN="$RUST_TOOLCHAIN" -DRust_CARGO_TARGET="$A64_RUST_TARGET" \
  -DSLINT_COMPILER="$stage/bin/slint-compiler" -DSLINT_LIBRARY_CARGO_FLAGS=--locked \
  -DSLINT_BUILD_TESTING=OFF -DSLINT_BUILD_EXAMPLES=OFF \
  -DSLINT_FEATURE_RENDERER_SOFTWARE=ON -DSLINT_FEATURE_EXPERIMENTAL=OFF \
  -DSLINT_FEATURE_BACKEND_WINIT=OFF -DSLINT_FEATURE_RENDERER_FEMTOVG=OFF -DSLINT_FEATURE_RENDERER_SKIA=OFF \
  -DSLINT_FEATURE_BACKEND_QT=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS=OFF -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBSEAT=OFF \
  -DSLINT_FEATURE_BACKEND_LINUXKMS_LIBINPUT=OFF -DSLINT_FEATURE_INTERPRETER=OFF -DSLINT_FEATURE_LIVE_PREVIEW=OFF \
  -DSLINT_FEATURE_ACCESSIBILITY=OFF -DSLINT_FEATURE_SYSTEM_TRAY=OFF -DSLINT_FEATURE_GETTEXT=OFF \
  -DSLINT_FEATURE_TESTING=OFF
cmake --build "$abld"
cmake --install "$abld"
aa=$aprefix/lib/libslint_cpp.a
alg=$("${A}gcc" -print-libgcc-file-name)
comm -12 \
  <("${A}nm" -g --defined-only "$aa" 2>/dev/null | awk 'NF==3{print $3}' | sort -u) \
  <("${A}nm" -g --defined-only "$asr/lib/libc.a" "$asr/lib/libm.a" "$alg" "$(dirname "$alg")/libgcc_eh.a" \
      2>/dev/null | awk 'NF==3{print $3}' | sort -u) > "$abld/localize.txt"
"${A}objcopy" --localize-symbols="$abld/localize.txt" "$aa" "$aa.tmp"
mv "$aa.tmp" "$aa"
fi

{
  echo "slint $SLINT_VERSION"
  if want "$TRIPLE"; then
    glibc_max=$("${X}objdump" -T "$sr/usr/lib/libc.so.6" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1 || true)
    echo "triple $TRIPLE"
  fi
  if want "$HOST_TRIPLE"; then
    echo "triple $HOST_TRIPLE host gcc $(gcc -dumpfullversion) glibc $(ldd --version | head -1 | awk '{print $NF}')"
  fi
  if want "$A64_TRIPLE"; then
    echo "triple $A64_TRIPLE cross gcc $("${A}gcc" -dumpfullversion) glibc $("${A}objdump" -T "$asr/lib/libc.so.6" 2>/dev/null \
      | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1 | sed 's/GLIBC_//')"
  fi
  echo "rust $RUST_TOOLCHAIN $RUST_TARGET $HOST_RUST_TARGET $A64_RUST_TARGET"
  want "$TRIPLE" && echo "toolchain $(basename "$TC") ${glibc_max:-glibc}"
  echo "slint-compiler $(sha256sum < "$stage/bin/slint-compiler" | cut -d' ' -f1) x86_64"
  for p in "${patches[@]}"; do sha256sum "$p" | awk -v n="$(basename "$p")" '{print "patch", n, $1}'; done
  want "$TRIPLE" && echo "localized $TRIPLE $(wc -l < "$bld/localize.txt")"
  want "$HOST_TRIPLE" && echo "localized $HOST_TRIPLE $(wc -l < "$hbld/localize.txt")"
  want "$A64_TRIPLE" && echo "localized $A64_TRIPLE $(wc -l < "$abld/localize.txt")"
} > "$stage/BUILD-INFO"
cp "$src/LICENSE.md" "$stage/LICENSE.md"

name=slint-sdk-$SLINT_VERSION-$rev.tar.zst
( cd "$stage" && find . -mindepth 1 | LC_ALL=C sort | tar --no-recursion --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -T - -cf - ) | zstd -q -19 -T0 -f -o "$out/$name"
echo "$name $(stat -c%s "$out/$name") bytes ($(find "$stage" -name libslint_cpp.a -printf '%h ' ))"
sha256sum "$out/$name"
