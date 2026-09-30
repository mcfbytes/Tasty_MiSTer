#!/usr/bin/env python3
"""Build static zlib-ng (compat), minizip, lzo, and libchdr for the static glibc toolchain."""
from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

# Hashes from Buildroot package/*.hash (zlib-ng, minizip-zlib/libzlib, lzo, libchdr).
ZLIB_NG_VER = "2.3.3"
ZLIB_NG_URL = f"https://github.com/zlib-ng/zlib-ng/archive/{ZLIB_NG_VER}/zlib-ng-{ZLIB_NG_VER}.tar.gz"
ZLIB_NG_SHA = "f9c65aa9c852eb8255b636fd9f07ce1c406f061ec19a2e7d508b318ca0c907d1"

ZLIB_VER = "1.3.2"
ZLIB_URL = f"https://github.com/madler/zlib/releases/download/v{ZLIB_VER}/zlib-{ZLIB_VER}.tar.xz"
ZLIB_SHA = "d7a0654783a4da529d1bb793b7ad9c3318020af77667bcae35f95d0e42a792f3"

LZO_URL = "https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz"
LZO_SHA = "c0f892943208266f9b6543b3ae308fab6284c5c90e627931446fb49b4221a072"

LIBCHDR_REV = "8e7b8bd32bc676b7e5c6b42fe7d2daca986c4a0d"
LIBCHDR_URL = f"https://github.com/rtissera/libchdr/archive/{LIBCHDR_REV}.tar.gz"
LIBCHDR_SHA = "04d6c61946c95addb78f4554740283b93249b81d8437e3d8a58ca1899c824dcc"


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run(cmd: list[str], cwd: Path, env: dict[str, str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, env=env, check=True)


def fetch(url: str, dest: Path, expect: str) -> None:
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.is_file() and sha256_file(dest) == expect:
        return
    print("fetch", url, flush=True)
    urllib.request.urlretrieve(url, dest)
    got = sha256_file(dest)
    if got != expect:
        dest.unlink(missing_ok=True)
        raise SystemExit(f"sha256 mismatch for {dest.name}: {got} != {expect}")


def extract(archive: Path, dest_dir: Path, member_root: str) -> Path:
    out = dest_dir / member_root
    if out.is_dir():
        return out
    with tarfile.open(archive) as t:
        t.extractall(dest_dir)
    if not out.is_dir():
        raise SystemExit(f"extract missing {out}")
    return out


def cmake_build(src: Path, build: Path, env: dict[str, str], args: list[str]) -> None:
    build.mkdir(parents=True, exist_ok=True)
    run(["cmake", "-S", str(src), "-B", str(build), *args], src, env)
    run(["cmake", "--build", str(build)], src, env)


def merge_archives(ar: Path, out: Path, archives: list[Path]) -> None:
    lines = [f"CREATE {out}"]
    for a in archives:
        lines.append(f"ADDLIB {a}")
    lines.extend(["SAVE", "END"])
    print("+ ar -M merge", out.name, flush=True)
    subprocess.run([str(ar), "-M"], input="\n".join(lines) + "\n", text=True, check=True)


def main() -> int:
    root = Path(os.environ["TASTY_TOOLCHAIN_ROOT"])
    cc = root / "bin/arm-buildroot-linux-gnueabihf-gcc"
    ar = root / "bin/arm-buildroot-linux-gnueabihf-ar"
    prefix = Path(os.environ.get("TASTY_STATIC_DEPS", str(root / "opt")))
    prefix.mkdir(parents=True, exist_ok=True)
    work = Path(os.environ.get("TASTY_STATIC_DEPS_SRC", "/tmp/tasty-static-deps"))
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["CC"] = str(cc)
    env["AR"] = str(ar)
    cflags = "-O2 -fPIC -march=armv7-a -mfpu=neon -mfloat-abi=hard"
    env["CFLAGS"] = cflags
    env["PATH"] = str(root / "bin") + ":" + env.get("PATH", "")
    cmake_cc = [
        f"-DCMAKE_C_COMPILER={cc}",
        f"-DCMAKE_AR={ar}",
        "-DCMAKE_SYSTEM_NAME=Linux",
        "-DCMAKE_SYSTEM_PROCESSOR=arm",
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        f"-DCMAKE_C_FLAGS={cflags}",
        f"-DCMAKE_INSTALL_PREFIX={prefix}",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DBUILD_SHARED_LIBS=OFF",
    ]

    zng_tar = work / f"zlib-ng-{ZLIB_NG_VER}.tar.gz"
    fetch(ZLIB_NG_URL, zng_tar, ZLIB_NG_SHA)
    zng = extract(zng_tar, work, f"zlib-ng-{ZLIB_NG_VER}")
    cmake_build(
        zng,
        work / "build-zlib-ng",
        env,
        cmake_cc
        + [
            "-DZLIB_COMPAT=ON",
            "-DWITH_GZFILEOP=ON",
            "-DWITH_OPTIM=ON",
            "-DZLIB_ENABLE_TESTS=OFF",
            "-DWITH_ACLE=ON",
            "-DWITH_NEON=ON",
            "-DWITH_NATIVE_INSTRUCTIONS=OFF",
        ],
    )
    run(["cmake", "--install", str(work / "build-zlib-ng")], zng, env)

    ztar = work / f"zlib-{ZLIB_VER}.tar.xz"
    fetch(ZLIB_URL, ztar, ZLIB_SHA)
    zdir = extract(ztar, work, f"zlib-{ZLIB_VER}")
    mz = zdir / "contrib/minizip"
    objs: list[str] = []
    for src in ["ioapi.c", "unzip.c", "zip.c"]:
        obj = mz / (src[:-2] + ".o")
        run(
            [
                str(cc),
                "-c",
                "-O2",
                "-fPIC",
                "-march=armv7-a",
                "-mfpu=neon",
                "-mfloat-abi=hard",
                "-I" + str(prefix / "include"),
                src,
                "-o",
                str(obj),
            ],
            mz,
            env,
        )
        objs.append(str(obj))
    (prefix / "lib").mkdir(parents=True, exist_ok=True)
    run([str(ar), "rc", str(prefix / "lib/libminizip.a"), *objs], mz, env)
    (prefix / "include/minizip").mkdir(parents=True, exist_ok=True)
    for h in ["ioapi.h", "unzip.h", "zip.h", "crypt.h", "ints.h", "skipset.h"]:
        srcp = mz / h
        if srcp.is_file():
            shutil.copy2(srcp, prefix / "include/minizip" / h)

    ltar = work / "lzo-2.10.tar.gz"
    fetch(LZO_URL, ltar, LZO_SHA)
    ldir = extract(ltar, work, "lzo-2.10")
    run(
        [
            "./configure",
            "--host=arm-buildroot-linux-gnueabihf",
            "--enable-static",
            "--disable-shared",
            f"--prefix={prefix}",
        ],
        ldir,
        env,
    )
    run(["make", "-s"], ldir, env)
    run(["make", "-s", "install"], ldir, env)

    ctar = work / f"libchdr-{LIBCHDR_REV}.tar.gz"
    fetch(LIBCHDR_URL, ctar, LIBCHDR_SHA)
    cdir = extract(ctar, work, f"libchdr-{LIBCHDR_REV}")
    cbuild = work / "build-libchdr"
    cmake_build(
        cdir,
        cbuild,
        env,
        cmake_cc
        + [
            f"-DCMAKE_PREFIX_PATH={prefix}",
            f"-DZLIB_ROOT={prefix}",
            "-DWITH_SYSTEM_ZLIB=ON",
            "-DWITH_SYSTEM_ZSTD=OFF",
            "-DINSTALL_STATIC_LIBS=ON",
            "-DCHDR_WANT_TESTS=OFF",
        ],
    )
    stage = work / "chdr-stage"
    if stage.exists():
        shutil.rmtree(stage)
    run(["cmake", "--install", str(cbuild), "--prefix", str(stage)], cdir, env)
    libs = sorted(p for p in (stage / "lib").glob("*.a") if p.is_file())
    if not libs:
        libs = sorted(p for p in (stage / "lib64").glob("*.a") if p.is_file())
    if not libs:
        raise SystemExit("libchdr install produced no static archives")
    merge_archives(ar, prefix / "lib/libchdr.a", libs)
    ranlib = root / "bin/arm-buildroot-linux-gnueabihf-ranlib"
    run([str(ranlib), str(prefix / "lib/libchdr.a")], prefix, env)
    hdr_src = cdir / "include" / "libchdr"
    hdr_dst = prefix / "include" / "libchdr"
    hdr_dst.mkdir(parents=True, exist_ok=True)
    for h in hdr_src.glob("*.h"):
        shutil.copy2(h, hdr_dst / h.name)

    print("static deps in", prefix)
    for name in ["libz.a", "libminizip.a", "liblzo2.a", "libchdr.a"]:
        p = prefix / "lib" / name
        print(" ", p, "ok" if p.is_file() else "MISSING")
        if not p.is_file():
            return 1
    if not (prefix / "include/libchdr/chd.h").is_file():
        print("missing include/libchdr/chd.h", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
