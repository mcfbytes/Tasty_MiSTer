#!/usr/bin/env python3
"""Compare a MiSTer recording to an emulator dump on a desktop Linux PC.

tasty itself is the only program that runs on the DE10-Nano. Copy the
recording off the SD card (or record to a network share) and run this tool
on x86-64 or arm64 Linux.

Exit: 0 identical, 1 first difference found, 2 usage or input error.
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Optional

try:
    import numpy as np  # type: ignore

    HAS_NUMPY = True
except ImportError:
    np = None  # type: ignore
    HAS_NUMPY = False

HERE = Path(__file__).resolve().parent
DEFAULT_NES_PAL = HERE / "palettes" / "nes-kitrinx34.pal"
FFMPEG_MIN = "4.2"
PYTHON_MIN = (3, 9)

# TSV columns written by the recorder sidecar.
TSV_COLS = (
    "core_frame",
    "header_ctr",
    "capture_ns",
    "dup_reason",
    "movie_frame",
    "hash",
    "width",
    "height",
    "segment",
    "avi_frame",
)

PROFILES = {
    "nes": {"crop": (8, 8, 0, 0), "hint": (256, 240)},
    "snes": {"crop": (0, 0, 0, 0), "hint": (256, 224)},
    "md": {"crop": (0, 0, 0, 0), "hint": (320, 224)},
    "psx": {"crop": (0, 0, 0, 0), "hint": None},
    "auto": {"crop": None, "hint": None},
}


class UsageError(Exception):
    pass


@dataclass
class Row:
    core_frame: int
    header_ctr: int
    capture_ns: int
    dup: str
    movie_frame: int
    hash: str
    width: int
    height: int
    segment: int
    avi_frame: int


@dataclass
class Side:
    kind: str  # tsv, avi, pngs, log
    path: Path
    tsv: Optional[Path] = None
    avi: Optional[Path] = None
    png_dir: Optional[Path] = None
    log: Optional[Path] = None
    rows: list[Row] | None = None
    by_movie: dict[int, Row] | None = None
    skipped_missed: list[int] | None = None
    width: int = 0
    height: int = 0


def which(name: str) -> Optional[str]:
    return shutil.which(name)


def find_ffmpeg() -> Optional[str]:
    return which("ffmpeg")


def find_ffprobe() -> Optional[str]:
    return which("ffprobe")


def crc32_bytes(data: bytes) -> str:
    return f"{zlib.crc32(data) & 0xFFFFFFFF:08x}"


def crc32_rgb(pixels: bytes, width: int, height: int, stride: Optional[int] = None) -> str:
    row = width * 3
    if stride is None:
        stride = row
    crc = zlib.crc32(b"") & 0xFFFFFFFF
    for y in range(height):
        off = y * stride
        crc = zlib.crc32(pixels[off : off + row], crc) & 0xFFFFFFFF
    return f"{crc:08x}"


def load_palette(path: Path) -> list[tuple[int, int, int]]:
    data = path.read_bytes()
    if len(data) < 192:
        raise UsageError(f"palette {path} is {len(data)} bytes, need 192 (64 RGB triples)")
    out = []
    for i in range(64):
        out.append((data[i * 3], data[i * 3 + 1], data[i * 3 + 2]))
    return out


def rgb_to_index(pixels: bytes, pal: list[tuple[int, int, int]]) -> bytes:
    lookup = {pal[i]: i for i in range(len(pal))}
    n = len(pixels) // 3
    out = bytearray(n)
    for i in range(n):
        rgb = (pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2])
        idx = lookup.get(rgb)
        if idx is None:
            best = 0
            best_d = 1 << 30
            for j, (r, g, b) in enumerate(pal):
                d = (rgb[0] - r) ** 2 + (rgb[1] - g) ** 2 + (rgb[2] - b) ** 2
                if d < best_d:
                    best_d = d
                    best = j
            idx = best
        out[i] = idx
    return bytes(out)


def parse_tsv(path: Path) -> list[Row]:
    rows: list[Row] = []
    with path.open(newline="") as f:
        reader = csv.reader(f, delimiter="\t")
        header = next(reader, None)
        if not header or "movie_frame" not in header:
            raise UsageError(f"{path} is not a frames.tsv sidecar")
        idx = {name: i for i, name in enumerate(header)}
        for rec in reader:
            if not rec or rec[0].startswith("#"):
                continue
            try:
                rows.append(
                    Row(
                        core_frame=int(rec[idx["core_frame"]]),
                        header_ctr=int(rec[idx.get("header_ctr", 1)]),
                        capture_ns=int(rec[idx.get("capture_ns", 2)]),
                        dup=rec[idx.get("dup_reason", 3)],
                        movie_frame=int(rec[idx["movie_frame"]]),
                        hash=rec[idx["hash"]].lower(),
                        width=int(rec[idx.get("width", 6)]),
                        height=int(rec[idx.get("height", 7)]),
                        segment=int(rec[idx.get("segment", 8)]),
                        avi_frame=int(rec[idx.get("avi_frame", 9)]),
                    )
                )
            except (KeyError, ValueError, IndexError) as e:
                raise UsageError(f"{path}: bad row {rec[:6]!r}: {e}") from e
    return rows


def parse_emu_log(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    with path.open() as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.replace(",", "\t").split()
            if len(parts) < 2:
                continue
            try:
                mf = int(parts[0])
            except ValueError:
                continue
            # movie_frame  lag  hash  [kind]
            if len(parts) >= 3 and all(c in "0123456789abcdefABCDEF" for c in parts[2]):
                try:
                    if int(parts[1]) == 1:
                        continue
                except ValueError:
                    pass
                out[mf] = parts[2].lower()
            elif all(c in "0123456789abcdefABCDEF" for c in parts[1]):
                out[mf] = parts[1].lower()
    if not out:
        raise UsageError(f"{path} has no movie_frame/hash rows")
    return out


def movie_index(rows: list[Row]) -> tuple[dict[int, Row], list[int]]:
    """Index dup==none rows. Flagged dup/missed rows are skipped, never a difference."""
    out: dict[int, Row] = {}
    skipped: list[int] = []
    for r in rows:
        if r.movie_frame < 0:
            continue
        if r.dup not in ("", "none"):
            skipped.append(r.movie_frame)
            continue
        out[r.movie_frame] = r
    skipped = [m for m in skipped if m not in out]
    return out, skipped


def sidecar_avi(tsv: Path, segment: int = 0) -> Path:
    name = tsv.name
    if name.endswith(".frames.tsv"):
        base = name[: -len(".frames.tsv")]
    elif name.endswith(".tsv"):
        base = name[: -len(".tsv")]
    else:
        base = tsv.stem
    return tsv.parent / f"{base}_{segment:03d}.avi"


def detect_side(path: Path) -> Side:
    p = path.expanduser().resolve()
    if not p.exists():
        raise UsageError(f"missing input {path}")
    if p.is_dir():
        pngs = sorted(p.glob("*.png"))
        sidecars = list(p.glob("*.frames.tsv"))
        avis = list(p.glob("*.avi"))
        logs = [x for x in p.glob("*.tsv") if x.name.endswith(".tsv")]
        if sidecars:
            return detect_side(sidecars[0])
        if avis:
            return detect_side(avis[0])
        if pngs:
            return Side(kind="pngs", path=p, png_dir=p)
        if logs:
            return detect_side(logs[0])
        raise UsageError(f"{p} has no sidecar, AVI, or PNG frames")
    name = p.name.lower()
    if name.endswith(".frames.tsv") or name.endswith(".tsv"):
        with p.open() as f:
            head = f.readline()
        if "movie_frame" in head and "core_frame" in head:
            rows = parse_tsv(p)
            by, skipped = movie_index(rows)
            w = h = 0
            if rows:
                w, h = rows[0].width, rows[0].height
            avi = sidecar_avi(p)
            return Side(
                kind="tsv",
                path=p,
                tsv=p,
                avi=avi if avi.is_file() else None,
                rows=rows,
                by_movie=by,
                skipped_missed=skipped,
                width=w,
                height=h,
            )
        return Side(kind="log", path=p, log=p)
    if name.endswith(".avi"):
        tsv = p.with_name(p.stem.rsplit("_", 1)[0] + ".frames.tsv")
        if not tsv.is_file():
            tsv = p.with_suffix(".frames.tsv")
        side = Side(kind="avi", path=p, avi=p)
        if tsv.is_file():
            rec = detect_side(tsv)
            rec.kind = "tsv"
            rec.avi = p
            return rec
        return side
    if name.endswith(".png"):
        return Side(kind="pngs", path=p.parent, png_dir=p.parent)
    raise UsageError(f"cannot classify {p}")


def probe_size(avi: Path) -> tuple[int, int]:
    ffprobe = find_ffprobe()
    if not ffprobe:
        raise UsageError("ffprobe is not on PATH; install ffmpeg (see --check-deps)")
    r = subprocess.run(
        [
            ffprobe,
            "-v",
            "error",
            "-select_streams",
            "v:0",
            "-show_entries",
            "stream=width,height",
            "-of",
            "csv=p=0",
            str(avi),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if r.returncode != 0:
        raise UsageError(f"ffprobe failed on {avi}: {r.stderr.strip()}")
    line = r.stdout.strip().split(",")
    if len(line) < 2:
        raise UsageError(f"ffprobe gave no size for {avi}")
    return int(line[0]), int(line[1])


def iter_avi_frames(avi: Path, width: int, height: int) -> Iterator[bytes]:
    ffmpeg = find_ffmpeg()
    if not ffmpeg:
        raise UsageError("ffmpeg is not on PATH; install ffmpeg (see --check-deps)")
    cmd = [
        ffmpeg,
        "-hide_banner",
        "-loglevel",
        "error",
        "-i",
        str(avi),
        "-f",
        "rawvideo",
        "-pix_fmt",
        "rgb24",
        "-",
    ]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    assert proc.stdout is not None
    nbytes = width * height * 3
    try:
        while True:
            buf = proc.stdout.read(nbytes)
            if len(buf) < nbytes:
                break
            yield buf
    finally:
        proc.stdout.close()
        if proc.stderr is not None:
            proc.stderr.close()
        proc.kill()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()


def png_chunk(tag: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)


def write_png(path: Path, pixels: bytes, width: int, height: int) -> None:
    raw = b"".join(b"\x00" + pixels[y * width * 3 : (y + 1) * width * 3] for y in range(height))
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    body = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(raw, 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(body)


def read_png(path: Path) -> tuple[bytes, int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise UsageError(f"{path} is not a PNG")
    off = 8
    width = height = 0
    idat = b""
    plte = b""
    bit_depth = color_type = 0
    while off + 8 <= len(data):
        (n,) = struct.unpack(">I", data[off : off + 4])
        tag = data[off + 4 : off + 8]
        chunk = data[off + 8 : off + 8 + n]
        off += 12 + n
        if tag == b"IHDR":
            width, height, bit_depth, color_type, *_ = struct.unpack(">IIBBBBB", chunk)
        elif tag == b"PLTE":
            plte = chunk
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    if width <= 0 or bit_depth != 8 or color_type not in (2, 3, 6):
        raise UsageError(f"{path}: need 8-bit RGB/indexed PNG, got {bit_depth}/{color_type} {width}x{height}")
    raw = zlib.decompress(idat)
    bpp = {2: 3, 3: 1, 6: 4}[color_type]
    stride = 1 + width * bpp
    out = bytearray(width * height * 3)
    prev = bytearray(width * bpp)
    src = 0
    for y in range(height):
        filt = raw[src]
        row = bytearray(raw[src + 1 : src + stride])
        src += stride
        if filt == 1:
            for i in range(len(row)):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + left) & 255
        elif filt == 2:
            for i in range(len(row)):
                row[i] = (row[i] + prev[i]) & 255
        elif filt == 3:
            for i in range(len(row)):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) // 2)) & 255
        elif filt == 4:
            for i in range(len(row)):
                a = row[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[i] = (row[i] + pr) & 255
        elif filt != 0:
            raise UsageError(f"{path}: unsupported PNG filter {filt}")
        prev = row
        dst = y * width * 3
        if bpp == 3:
            out[dst : dst + width * 3] = row
        elif bpp == 1:
            if len(plte) < 3:
                raise UsageError(f"{path}: indexed PNG missing PLTE")
            for x in range(width):
                i = row[x] * 3
                out[dst + x * 3 : dst + x * 3 + 3] = plte[i : i + 3]
        else:
            for x in range(width):
                out[dst + x * 3 : dst + x * 3 + 3] = row[x * 4 : x * 4 + 3]
    return bytes(out), width, height


_FRAME_NUM = re.compile(r"(\d+)")


def png_movie_frame(path: Path, fallback: int) -> int:
    m = _FRAME_NUM.search(path.stem)
    return int(m.group(1)) if m else fallback


def iter_png_frames(folder: Path) -> Iterator[tuple[int, bytes, int, int]]:
    files = sorted(folder.glob("*.png"))
    if not files:
        raise UsageError(f"{folder} has no PNG frames")
    for i, p in enumerate(files):
        pix, w, h = read_png(p)
        yield png_movie_frame(p, i), pix, w, h


def crop_bytes(pixels: bytes, width: int, height: int, crop: tuple[int, int, int, int]) -> tuple[bytes, int, int]:
    top, bottom, left, right = crop
    nw = width - left - right
    nh = height - top - bottom
    if nw <= 0 or nh <= 0:
        raise UsageError(f"crop {crop} emptied {width}x{height}")
    if top == bottom == left == right == 0:
        return pixels, width, height
    out = bytearray(nw * nh * 3)
    for y in range(nh):
        src = ((y + top) * width + left) * 3
        dst = y * nw * 3
        out[dst : dst + nw * 3] = pixels[src : src + nw * 3]
    return bytes(out), nw, nh


def auto_crop(w1: int, h1: int, w2: int, h2: int) -> tuple[tuple[int, int, int, int], tuple[int, int, int, int]]:
    """Return (crop_a, crop_b) so both sides share the smaller picture."""
    c1 = (0, 0, 0, 0)
    c2 = (0, 0, 0, 0)
    if w1 == w2 and abs(h1 - h2) == 16:
        if h1 > h2:
            c1 = (8, 8, 0, 0)
        else:
            c2 = (8, 8, 0, 0)
    elif h1 == h2 and abs(w1 - w2) in (16, 64, 256):
        d = abs(w1 - w2) // 2
        if w1 > w2:
            c1 = (0, 0, d, abs(w1 - w2) - d)
        else:
            c2 = (0, 0, d, abs(w1 - w2) - d)
    return c1, c2


def mad_and_changed(a: bytes, b: bytes) -> tuple[float, float]:
    n = min(len(a), len(b))
    if n == 0:
        return 0.0, 0.0
    if HAS_NUMPY:
        aa = np.frombuffer(a[:n], dtype=np.uint8).astype(np.int16)
        bb = np.frombuffer(b[:n], dtype=np.uint8).astype(np.int16)
        d = np.abs(aa - bb)
        mad = float(d.mean())
        pix = n // 3
        ch = int((d.reshape(-1, 3).max(axis=1) > 0).sum()) if pix else 0
        return mad, (ch / pix if pix else 0.0)
    total = 0
    changed = 0
    pix = n // 3
    for i in range(pix):
        o = i * 3
        dr = abs(a[o] - b[o])
        dg = abs(a[o + 1] - b[o + 1])
        db = abs(a[o + 2] - b[o + 2])
        total += dr + dg + db
        if dr or dg or db:
            changed += 1
    return total / n, (changed / pix if pix else 0.0)


def learn_bijection(
    pairs: Iterable[tuple[bytes, bytes]],
) -> tuple[dict[tuple[int, int, int], tuple[int, int, int]], list[str]]:
    counts: dict[tuple[int, int, int], dict[tuple[int, int, int], int]] = {}
    for emu, mister in pairs:
        n = min(len(emu), len(mister)) // 3
        for i in range(n):
            e = (emu[i * 3], emu[i * 3 + 1], emu[i * 3 + 2])
            m = (mister[i * 3], mister[i * 3 + 1], mister[i * 3 + 2])
            bucket = counts.setdefault(e, {})
            bucket[m] = bucket.get(m, 0) + 1
    bij: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    conflicts: list[str] = []
    for e, bucket in counts.items():
        ranked = sorted(bucket.items(), key=lambda kv: -kv[1])
        bij[e] = ranked[0][0]
        if len(ranked) > 1 and ranked[1][1] * 4 > ranked[0][1]:
            conflicts.append(
                f"emu RGB {e[0]:02x}{e[1]:02x}{e[2]:02x} maps to "
                f"{ranked[0][0]} ({ranked[0][1]}) and {ranked[1][0]} ({ranked[1][1]})"
            )
    return bij, conflicts


def apply_bijection(pixels: bytes, bij: dict[tuple[int, int, int], tuple[int, int, int]]) -> bytes:
    n = len(pixels) // 3
    out = bytearray(pixels)
    for i in range(n):
        e = (pixels[i * 3], pixels[i * 3 + 1], pixels[i * 3 + 2])
        m = bij.get(e)
        if m is None:
            continue
        out[i * 3 : i * 3 + 3] = bytes(m)
    return bytes(out)


def leftover_count(emu: bytes, mister: bytes) -> int:
    """Pixels that disagree after a per-frame majority emu→mister RGB map."""
    n = min(len(emu), len(mister)) // 3
    if n == 0:
        return 0
    if HAS_NUMPY:
        e = np.frombuffer(emu, dtype=np.uint8, count=n * 3).reshape(n, 3)
        m = np.frombuffer(mister, dtype=np.uint8, count=n * 3).reshape(n, 3)
        ek = (e[:, 0].astype(np.uint32) << 16) | (e[:, 1].astype(np.uint32) << 8) | e[:, 2]
        mk = (m[:, 0].astype(np.uint32) << 16) | (m[:, 1].astype(np.uint32) << 8) | m[:, 2]
        bij: dict[int, int] = {}
        for ev in np.unique(ek):
            sub = mk[ek == ev]
            vals, cnts = np.unique(sub, return_counts=True)
            bij[int(ev)] = int(vals[int(cnts.argmax())])
        mapped = np.array([bij[int(v)] for v in ek], dtype=np.uint32)
        return int(np.count_nonzero(mapped != mk))
    votes: dict[bytes, dict[bytes, int]] = {}
    for i in range(n):
        e = emu[i * 3 : i * 3 + 3]
        m = mister[i * 3 : i * 3 + 3]
        bucket = votes.get(e)
        if bucket is None:
            votes[e] = {m: 1}
        else:
            bucket[m] = bucket.get(m, 0) + 1
    bijb = {e: max(b.items(), key=lambda kv: kv[1])[0] for e, b in votes.items()}
    bad = 0
    for i in range(n):
        if bijb[emu[i * 3 : i * 3 + 3]] != mister[i * 3 : i * 3 + 3]:
            bad += 1
    return bad


def frame_leftover(
    mpix: dict[int, bytes],
    epix: dict[int, bytes],
    mf: int,
    off: int,
    mw: int,
    mh: int,
    ew: int,
    eh: int,
    crop_m: tuple[int, int, int, int],
    crop_e: tuple[int, int, int, int],
) -> Optional[int]:
    mp = mpix.get(mf)
    ep = epix.get(mf + off)
    if mp is None or ep is None:
        return None
    mp, _, _ = crop_bytes(mp, mw, mh, crop_m)
    ep, _, _ = crop_bytes(ep, ew, eh, crop_e)
    if len(mp) != len(ep):
        return None
    return leftover_count(ep, mp)


def auto_offset_leftover(
    mpix: dict[int, bytes],
    epix: dict[int, bytes],
    mw: int,
    mh: int,
    ew: int,
    eh: int,
    crop_m: tuple[int, int, int, int],
    crop_e: tuple[int, int, int, int],
    slop: int,
    lo: int = -3,
    hi: int = 3,
) -> int:
    keys = [k for k in sorted(mpix) if k >= 200][::8][:80]
    if not keys:
        keys = sorted(mpix)[:80]
    best = (-1, 0)
    for off in range(lo, hi + 1):
        hits = 0
        n = 0
        for mf in keys:
            bad = frame_leftover(mpix, epix, mf, off, mw, mh, ew, eh, crop_m, crop_e)
            if bad is None:
                continue
            n += 1
            if bad <= slop:
                hits += 1
        if n and hits > best[0]:
            best = (hits, off)
    return best[1]


def refine_slip(
    mpix: dict[int, bytes],
    epix: dict[int, bytes],
    mw: int,
    mh: int,
    ew: int,
    eh: int,
    crop_m: tuple[int, int, int, int],
    crop_e: tuple[int, int, int, int],
    start_off: int,
    first: int,
    slop: int,
    lo: int = -3,
    hi: int = 3,
) -> tuple[Optional[int], int, Optional[int]]:
    """After a leftover miss, see if a nearby offset restores a match streak."""
    keys = [k for k in sorted(mpix) if first - 40 <= k <= first + 120]
    best_off = start_off
    best_hits = -1
    for off in range(lo, hi + 1):
        hits = 0
        n = 0
        for mf in keys:
            bad = frame_leftover(mpix, epix, mf, off, mw, mh, ew, eh, crop_m, crop_e)
            if bad is None:
                continue
            n += 1
            if bad <= slop:
                hits += 1
        if n >= 8 and hits > best_hits:
            best_hits = hits
            best_off = off
    slip_at: Optional[int] = None
    if best_off != start_off:
        for mf in keys:
            old = frame_leftover(mpix, epix, mf, start_off, mw, mh, ew, eh, crop_m, crop_e)
            new = frame_leftover(mpix, epix, mf, best_off, mw, mh, ew, eh, crop_m, crop_e)
            if old is not None and new is not None and old > slop and new <= slop:
                slip_at = mf
                break
        if slip_at is None:
            slip_at = first
    unique: Optional[int] = None
    start = slip_at if slip_at is not None else first
    for mf in sorted(mpix):
        if mf < start:
            continue
        present = False
        hit = False
        for o in range(lo, hi + 1):
            bad = frame_leftover(mpix, epix, mf, o, mw, mh, ew, eh, crop_m, crop_e)
            if bad is None:
                continue
            present = True
            if bad <= slop:
                hit = True
                break
        if present and not hit:
            unique = mf
            break
    return slip_at, best_off, unique


def edge_map(pixels: bytes, width: int, height: int) -> bytes:
    """1 if the pixel differs from its right or lower neighbour (palette-free)."""
    n = width * height
    out = bytearray(n)
    if width < 2 or height < 2 or len(pixels) < n * 3:
        return bytes(out)
    if HAS_NUMPY:
        img = np.frombuffer(pixels, dtype=np.uint8, count=n * 3).reshape(height, width, 3)
        right = np.any(img[:, :-1] != img[:, 1:], axis=2)
        down = np.any(img[:-1, :] != img[1:, :], axis=2)
        e = np.zeros((height, width), dtype=np.uint8)
        e[:-1, :-1] = (right[:-1, :] | down[:, :-1]).astype(np.uint8)
        return e.tobytes()
    row = width * 3
    for y in range(height - 1):
        for x in range(width - 1):
            i = y * row + x * 3
            d = y * row + (x + 1) * 3
            b = (y + 1) * row + x * 3
            if (
                pixels[i] != pixels[d]
                or pixels[i + 1] != pixels[d + 1]
                or pixels[i + 2] != pixels[d + 2]
                or pixels[i] != pixels[b]
                or pixels[i + 1] != pixels[b + 1]
                or pixels[i + 2] != pixels[b + 2]
            ):
                out[y * width + x] = 1
    return bytes(out)


def edge_hash(pixels: bytes, width: int, height: int) -> str:
    return crc32_bytes(edge_map(pixels, width, height))


def matching_offsets(
    a: dict[int, str], b: dict[int, str], mf: int, lo: int, hi: int
) -> list[int]:
    ha = a.get(mf)
    if ha is None:
        return []
    out: list[int] = []
    for o in range(lo, hi + 1):
        hb = b.get(mf + o)
        if hb is not None and ha == hb:
            out.append(o)
    return out


def vote_offset(a: dict[int, str], b: dict[int, str], keys: list[int], lo: int, hi: int) -> int:
    votes: dict[int, int] = {}
    for k in keys:
        offs = matching_offsets(a, b, k, lo, hi)
        if len(offs) != 1:
            continue
        votes[offs[0]] = votes.get(offs[0], 0) + 1
    if not votes:
        return 0
    return max(votes.items(), key=lambda kv: kv[1])[0]


def detect_slip(
    a: dict[int, str],
    b: dict[int, str],
    start_off: int,
    first: Optional[int],
    lo: int = -3,
    hi: int = 3,
    confirm: int = 12,
) -> tuple[Optional[int], int, Optional[int]]:
    """Find a sustained offset change, then the first uniquely different picture."""
    if first is None:
        return None, start_off, None
    keys = [k for k in sorted(a) if k >= 0 and any((k + o) in b for o in range(lo, hi + 1))]
    if not keys:
        return None, start_off, first
    head = [k for k in keys if first is None or k < first + 80][:80]
    if not head:
        head = keys[:80]
    base = vote_offset(a, b, head, lo, hi)
    slip_at: Optional[int] = None
    new_off = base
    win = confirm
    for i in range(len(keys) - win):
        chunk = keys[i : i + win]
        off = vote_offset(a, b, chunk, lo, hi)
        if off != base and sum(1 for k in chunk if off in matching_offsets(a, b, k, lo, hi)) >= win // 2:
            slip_at = chunk[0]
            new_off = off
            break
    unique: Optional[int] = None
    start = slip_at if slip_at is not None else (first if first is not None else keys[0])
    streak = 0
    mark: Optional[int] = None
    for k in keys:
        if k < start:
            continue
        if matching_offsets(a, b, k, lo, hi):
            streak = 0
            mark = None
            continue
        streak += 1
        if mark is None:
            mark = k
        if streak >= 3:
            unique = mark
            break
    return slip_at, new_off, unique


def heatmap(a: bytes, b: bytes) -> bytes:
    n = min(len(a), len(b))
    out = bytearray(n)
    for i in range(0, n, 3):
        d = max(abs(a[i] - b[i]), abs(a[i + 1] - b[i + 1]), abs(a[i + 2] - b[i + 2]))
        out[i] = min(255, d * 2)
        out[i + 1] = 0
        out[i + 2] = 0
    return bytes(out)


def hstack(left: bytes, mid: bytes, right: bytes, w: int, h: int) -> tuple[bytes, int, int]:
    nw = w * 3
    out = bytearray(nw * h * 3)
    for y in range(h):
        row = y * w * 3
        dst = y * nw * 3
        out[dst : dst + w * 3] = left[row : row + w * 3]
        out[dst + w * 3 : dst + 2 * w * 3] = mid[row : row + w * 3]
        out[dst + 2 * w * 3 : dst + 3 * w * 3] = right[row : row + w * 3]
    return bytes(out), nw, h


def auto_offset_hashes(
    a: dict[int, str], b: dict[int, str], window: int = 600, lo: int = -60, hi: int = 60
) -> int:
    keys: list[int] = []
    for k in sorted(a):
        if k < 0:
            continue
        if any((k + o) in b for o in range(lo, hi + 1)):
            keys.append(k)
        if len(keys) >= window:
            break
    if not keys:
        keys = [k for k in sorted(a) if k >= 0][:window]
    best_off = 0
    best = (-1.0, 0, 0)  # score, -abs, prefer 0
    for off in range(lo, hi + 1):
        n = 0
        hits = 0
        for k in keys:
            kb = k + off
            if kb not in b:
                continue
            n += 1
            if a[k] == b[kb]:
                hits += 1
        if n < 8:
            continue
        score = hits / n
        key = (score, -abs(off), 1 if off == 0 else 0)
        if key > best:
            best = key
            best_off = off
    return best_off


def hashes_of(side: Side) -> dict[int, str]:
    if side.by_movie is not None:
        return {mf: r.hash for mf, r in side.by_movie.items()}
    if side.log is not None:
        return parse_emu_log(side.log)
    raise UsageError(f"{side.path} has no hash log or frames.tsv")


def check_deps(stream) -> int:
    def line(tool: str, need: str, got: str, hint: str) -> None:
        status = "ok" if got else "MISSING"
        stream.write(f"  {tool:12} need {need:20} {status:8} {got or hint}\n")

    stream.write("tasty-compare runs on a desktop Linux PC, not on the DE10-Nano.\n")
    py = sys.version.split()[0]
    py_ok = sys.version_info >= PYTHON_MIN
    stream.write(
        f"python        need >={PYTHON_MIN[0]}.{PYTHON_MIN[1]:<17} "
        f"{'ok':8} {py}\n"
    )
    ff = find_ffmpeg()
    fp = find_ffprobe()
    ff_ver = ""
    if ff:
        r = subprocess.run([ff, "-version"], capture_output=True, text=True)
        ff_ver = (r.stdout.splitlines() or [""])[0]
    line("ffmpeg", f">={FFMPEG_MIN} (cscd)", ff_ver, "apt install ffmpeg")
    line("ffprobe", f">={FFMPEG_MIN}", fp or "", "apt install ffmpeg")
    line("numpy", "optional, faster MAD", "present" if HAS_NUMPY else "", "pip install numpy")
    stream.write("dumpers (optional, only if you capture from an emulator):\n")
    stream.write("  fceux        >=2.6                 %s\n" % (which("fceux") or "apt install fceux"))
    stream.write("  EmuHawk      BizHawk 2.8+          unpack the BizHawk zip; not in apt\n")
    stream.write("  lsnes        rr2                   AVI dump or lsnes-dump.lua; not in apt\n")
    if not py_ok:
        stream.write("python is too old\n")
        return 2
    return 0


def compare_hashes(
    mister: dict[int, str],
    emu: dict[int, str],
    offset: int,
    overlap_only: bool,
    max_frames: Optional[int] = None,
) -> tuple[Optional[int], list[tuple[int, bool, str]]]:
    rows: list[tuple[int, bool, str]] = []
    first: Optional[int] = None
    keys = sorted(set(mister) | {k - offset for k in emu})
    for mf in keys:
        if mf < 0:
            continue
        if max_frames is not None and len(rows) >= max_frames:
            break
        mh = mister.get(mf)
        eh = emu.get(mf + offset)
        if mh is None or eh is None:
            if overlap_only:
                continue
            match = False
            metric = "missing-mister" if mh is None else "missing-emu"
        else:
            match = mh == eh
            metric = "hash" if match else "hash-mismatch"
        rows.append((mf, match, metric))
        if not match and first is None:
            first = mf
    return first, rows


def core_frame_of(side: Side, movie_frame: int) -> int:
    if side.by_movie and movie_frame in side.by_movie:
        return side.by_movie[movie_frame].core_frame
    return -1


def write_csv(path: Path, rows: list[tuple[int, bool, str]]) -> None:
    with path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["movie_frame", "match", "metric"])
        for mf, match, metric in rows:
            w.writerow([mf, "1" if match else "0", metric])


def load_movie_pixels(side: Side, max_frames: Optional[int] = None) -> dict[int, bytes]:
    """Load pixels keyed by movie frame. AVI uses sidecar avi_frame, or 0..n."""
    out: dict[int, bytes] = {}
    if side.avi is not None:
        w, h = side.width, side.height
        if not w:
            w, h = probe_size(side.avi)
            side.width, side.height = w, h
        by_avi: dict[int, Row] = {}
        if side.by_movie is not None:
            by_avi = {r.avi_frame: r for r in side.by_movie.values() if r.avi_frame >= 0}
        for i, pix in enumerate(iter_avi_frames(side.avi, w, h)):
            if by_avi:
                r = by_avi.get(i)
                if r is None:
                    continue
                out[r.movie_frame] = pix
            else:
                out[i] = pix
            if max_frames is not None and len(out) >= max_frames:
                break
        if out:
            return out
        raise UsageError(f"{side.avi} decoded no frames")
    if side.png_dir is not None:
        for mf, pix, w, h in iter_png_frames(side.png_dir):
            side.width, side.height = w, h
            out[mf] = pix
            if max_frames is not None and len(out) >= max_frames:
                break
        return out
    raise UsageError(f"{side.path} has no video (AVI or PNG sequence)")


def compare_pixels(
    mister_pix: dict[int, bytes],
    emu_pix: dict[int, bytes],
    mw: int,
    mh: int,
    ew: int,
    eh: int,
    crop_m: tuple[int, int, int, int],
    crop_e: tuple[int, int, int, int],
    offset: int,
    bijection: bool,
    pal: Optional[list[tuple[int, int, int]]],
    tolerance: Optional[tuple[float, float]],
    learn_n: int,
    pixel_slop: int = 16,
) -> tuple[Optional[int], list[tuple[int, bool, str]], dict[int, tuple[bytes, bytes, int, int]]]:
    keys = sorted(set(mister_pix) & {k - offset for k in emu_pix})
    aligned: list[tuple[int, bytes, bytes]] = []
    kept: dict[int, tuple[bytes, bytes, int, int]] = {}
    for mf in keys:
        if mf < 0:
            continue
        mp = mister_pix.get(mf)
        ep = emu_pix.get(mf + offset)
        if mp is None or ep is None:
            continue
        mp, mw2, mh2 = crop_bytes(mp, mw, mh, crop_m)
        ep, ew2, eh2 = crop_bytes(ep, ew, eh, crop_e)
        if (mw2, mh2) != (ew2, eh2):
            raise UsageError(
                f"frame {mf}: size {mw2}x{mh2} vs {ew2}x{eh2} after crop; pass --crop"
            )
        aligned.append((mf, mp, ep))
        kept[mf] = (mp, ep, mw2, mh2)
        mw, mh, ew, eh = mw2, mh2, ew2, eh2
    if not aligned:
        raise UsageError("no overlapping movie frames after offset/crop")
    if pal is None and bijection:
        print(
            f"colour map: per-frame majority RGB; leftover ≤ {pixel_slop} is the same picture"
        )
    rows: list[tuple[int, bool, str]] = []
    for mf, mp, ep in aligned:
        if pal is not None:
            mi = rgb_to_index(mp, pal)
            ei = rgb_to_index(ep, pal)
            match = mi == ei
            metric = "index" if match else "index-mismatch"
        else:
            if bijection:
                bad = leftover_count(ep, mp)
                match = bad <= pixel_slop
                metric = f"leftover={bad}"
            else:
                ep2 = ep
                if tolerance:
                    mad, frac = mad_and_changed(mp, ep2)
                    match = mad <= tolerance[0] and frac <= tolerance[1]
                    metric = f"mad={mad:.4f},changed={frac:.4f}"
                else:
                    match = mp == ep2
                    metric = "exact" if match else "pixel-mismatch"
        rows.append((mf, match, metric))
    streak = 3 if len(rows) >= 100 else 1
    first = first_mismatch_streak(rows, streak)
    return first, rows, kept


def first_mismatch_streak(rows: list[tuple[int, bool, str]], n: int) -> Optional[int]:
    """First leftover miss that holds for n movie frames (ignore 1-frame spikes)."""
    run = 0
    mark: Optional[int] = None
    for mf, match, _metric in rows:
        if match:
            run = 0
            mark = None
            continue
        run += 1
        if mark is None:
            mark = mf
        if run >= n:
            return mark
    return None


def parse_crop(text: Optional[str]) -> Optional[tuple[int, int, int, int]]:
    if not text:
        return None
    parts = [int(x) for x in text.replace("x", ",").split(",")]
    if len(parts) != 4:
        raise UsageError("--crop wants top,bottom,left,right")
    return parts[0], parts[1], parts[2], parts[3]


def parse_tolerance(text: Optional[str]) -> Optional[tuple[float, float]]:
    if not text:
        return None
    parts = [float(x) for x in text.split(",")]
    if len(parts) == 1:
        return parts[0], 0.01
    if len(parts) != 2:
        raise UsageError("--tolerance wants mad[,changed_fraction]")
    return parts[0], parts[1]


def print_skipped(side: Side) -> None:
    n = len(side.skipped_missed or [])
    if n:
        print(f"skipped {n} missed sidecar rows")


def report_slip(
    mister: Side,
    first: Optional[int],
    off: int,
    ha: dict[int, str],
    hb: dict[int, str],
    kept: Optional[dict[int, tuple[bytes, bytes, int, int]]],
    out_dir: Path,
    around: int,
    n_rows: int,
) -> int:
    print_skipped(mister)
    slip_at, new_off, unique = detect_slip(ha, hb, off, first)
    headline = unique or slip_at or first
    if headline is None:
        print(f"identical ({n_rows} movie frames, offset {off})")
        return 0
    cf = core_frame_of(mister, headline)
    print(f"first difference: movie frame {headline} (core frame {cf}), offset {off}")
    if slip_at is not None and new_off != off:
        delta = new_off - off
        sc = core_frame_of(mister, slip_at)
        print(
            f"from movie frame {slip_at} (core frame {sc}) the pictures match again "
            f"at offset {new_off:+d} (was {off:+d}, delta {delta:+d})"
        )
    if unique is not None and unique != first:
        uc = core_frame_of(mister, unique)
        print(f"first unique picture: movie frame {unique} (core frame {uc})")
    dump_at = unique if unique is not None else first
    if kept:
        dump_around(out_dir, dump_at, kept, around)
        if slip_at is not None and slip_at != dump_at:
            dump_around(out_dir, slip_at, kept, around)
        print(f"PNGs and frames.csv in {out_dir}")
    return 1


def dump_around(
    out_dir: Path,
    first: int,
    kept: dict[int, tuple[bytes, bytes, int, int]],
    around: int,
) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    for mf in range(first - around, first + around + 1):
        trip = kept.get(mf)
        if trip is None:
            continue
        mp, ep, w, h = trip
        hm = heatmap(mp, ep)
        sheet, sw, sh = hstack(mp, ep, hm, w, h)
        write_png(out_dir / f"frame-{mf:06d}-mister.png", mp, w, h)
        write_png(out_dir / f"frame-{mf:06d}-emu.png", ep, w, h)
        write_png(out_dir / f"frame-{mf:06d}-diff.png", hm, w, h)
        write_png(out_dir / f"frame-{mf:06d}-sheet.png", sheet, sw, sh)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="tasty-compare",
        description=(
            "Find the first movie frame where a MiSTer recording differs from an "
            "emulator dump. Run this on a desktop Linux PC (x86-64 or arm64), not "
            "on the DE10-Nano. Copy the recording off the MiSTer first."
        ),
    )
    p.add_argument("--mister", help="MiSTer side: frames.tsv, AVI, or a directory")
    p.add_argument("--emu", help="emulator side: AVI, PNG directory, hash log, or frames.tsv")
    p.add_argument("--offset", type=int, default=None, help="emu_frame = mister_frame + N")
    p.add_argument("--auto-offset", action="store_true", help="learn N from the first 600 frames")
    p.add_argument("--system", choices=sorted(PROFILES), default="auto")
    p.add_argument("--crop", help="top,bottom,left,right applied to the taller/wider side")
    p.add_argument("--palette", type=Path, help="64-colour .pal (192 bytes RGB); NES default shipped")
    p.add_argument("--indices", action="store_true", help="compare NES palette indices, not RGB")
    p.add_argument("--no-colour-map", action="store_true", help="do not learn an RGB bijection")
    p.add_argument("--tolerance", help="mad[,changed_fraction] instead of exact pixels")
    p.add_argument(
        "--pixel-slop",
        type=int,
        default=32,
        help="majority colour-map leftover pixels still counted as the same picture",
    )
    p.add_argument(
        "--mode",
        choices=("pixels", "edges", "hashes"),
        default="pixels",
        help="pixels: RGB (colour map). edges: palette-free edge map (SMB3). hashes: sidecar only",
    )
    p.add_argument("--hashes-only", action="store_true", help="compare two frames.tsv / hash logs")
    p.add_argument("--overlap", action="store_true", help="ignore movie frames present on only one side")
    p.add_argument("--out", type=Path, default=Path("compare-out"))
    p.add_argument("--around", type=int, default=2, help="PNG neighbors around the first difference")
    p.add_argument("--max-frames", type=int, default=None, help="stop after this many movie frames")
    p.add_argument("--check-deps", action="store_true")
    return p


def main(argv: Optional[list[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.check_deps:
        return check_deps(sys.stdout)
    if not args.mister or not args.emu:
        print("tasty-compare: need --mister and --emu (or --check-deps)", file=sys.stderr)
        print("This tool runs on a desktop Linux PC, not on the DE10-Nano.", file=sys.stderr)
        return 2
    try:
        mister = detect_side(Path(args.mister))
        emu = detect_side(Path(args.emu))
        want_hashes = args.hashes_only or (
            mister.by_movie is not None
            and (emu.by_movie is not None or emu.log is not None)
            and emu.avi is None
            and emu.png_dir is None
        )
        if args.indices and mister.avi is not None and (emu.log is not None or emu.kind == "log"):
            pal = load_palette(args.palette or DEFAULT_NES_PAL)
            pix = load_movie_pixels(mister, args.max_frames)
            crop = parse_crop(args.crop) or (0, 0, 0, 0)
            mh: dict[int, str] = {}
            for mf, p in pix.items():
                p2, w, h = crop_bytes(p, mister.width, mister.height, crop)
                mh[mf] = crc32_bytes(rgb_to_index(p2, pal))
            eh = hashes_of(emu)
            off = args.offset
            if off is None:
                off = auto_offset_hashes(mh, eh) if args.auto_offset else 0
            first, rows = compare_hashes(mh, eh, off, True, args.max_frames)
            args.out.mkdir(parents=True, exist_ok=True)
            write_csv(args.out / "frames.csv", rows)
            if first is None:
                print(f"identical ({len(rows)} movie frames, offset {off}, palette indices)")
                return 0
            cf = core_frame_of(mister, first)
            print(
                f"first difference: movie frame {first} (core frame {cf}), offset {off}"
            )
            return 1
        if args.mode == "hashes":
            args.hashes_only = True
        if args.hashes_only or want_hashes:
            mh = hashes_of(mister)
            eh = hashes_of(emu)
            off = args.offset
            if off is None:
                off = auto_offset_hashes(mh, eh) if args.auto_offset else 0
            overlap = args.overlap or bool(mister.skipped_missed)
            first, rows = compare_hashes(mh, eh, off, overlap, args.max_frames)
            args.out.mkdir(parents=True, exist_ok=True)
            write_csv(args.out / "frames.csv", rows)
            return report_slip(
                mister, first, off, mh, eh, None, args.out, args.around, len(rows)
            )

        mpix = load_movie_pixels(mister, args.max_frames)
        epix = load_movie_pixels(emu, args.max_frames)
        mw, mh = mister.width, mister.height
        ew, eh = emu.width, emu.height
        crop_arg = parse_crop(args.crop)
        if crop_arg:
            crop_m, crop_e = (0, 0, 0, 0), crop_arg
            if mh < eh or (mh == eh and mw < ew):
                crop_m, crop_e = crop_arg, (0, 0, 0, 0)
        elif args.system == "nes":
            crop_m = (8, 8, 0, 0) if mh >= 240 else (0, 0, 0, 0)
            crop_e = (8, 8, 0, 0) if eh >= 240 else (0, 0, 0, 0)
        elif args.system != "auto" and PROFILES[args.system]["crop"]:
            crop_e = PROFILES[args.system]["crop"]
            crop_m = (0, 0, 0, 0)
            if mh > eh:
                crop_m, crop_e = crop_e, (0, 0, 0, 0)
        else:
            crop_m, crop_e = auto_crop(mw, mh, ew, eh)
        slop = args.pixel_slop
        off = args.offset
        if off is None:
            if args.auto_offset:
                off = auto_offset_leftover(
                    mpix, epix, mw, mh, ew, eh, crop_m, crop_e, slop
                )
            else:
                off = 0
        pal = None
        if args.indices:
            pal_path = args.palette or DEFAULT_NES_PAL
            pal = load_palette(pal_path)
        first, rows, kept = compare_pixels(
            mpix,
            epix,
            mw,
            mh,
            ew,
            eh,
            crop_m,
            crop_e,
            off,
            bijection=not args.no_colour_map and not args.indices,
            pal=pal,
            tolerance=parse_tolerance(args.tolerance),
            learn_n=30,
            pixel_slop=slop,
        )
        args.out.mkdir(parents=True, exist_ok=True)
        write_csv(args.out / "frames.csv", rows)
        print_skipped(mister)
        if first is None:
            print(f"identical ({len(rows)} movie frames, offset {off})")
            return 0
        slip_at, new_off, unique = refine_slip(
            mpix, epix, mw, mh, ew, eh, crop_m, crop_e, off, first, slop
        )
        headline = unique or slip_at or first
        cf = core_frame_of(mister, headline)
        print(f"first difference: movie frame {headline} (core frame {cf}), offset {off}")
        if slip_at is not None and new_off != off:
            sc = core_frame_of(mister, slip_at)
            print(
                f"from movie frame {slip_at} (core frame {sc}) the pictures match again "
                f"at offset {new_off:+d} (was {off:+d}, delta {new_off - off:+d})"
            )
        if unique is not None:
            uc = core_frame_of(mister, unique)
            print(f"first unique picture: movie frame {unique} (core frame {uc})")
        dump_at = unique if unique is not None else headline
        dump_around(args.out, dump_at, kept, args.around)
        if slip_at is not None and slip_at != dump_at:
            dump_around(args.out, slip_at, kept, args.around)
        print(f"PNGs and frames.csv in {args.out}")
        return 1
    except UsageError as e:
        print(f"tasty-compare: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
