#!/usr/bin/env python3
"""Unit tests for tasty-compare. Stdlib only; ffmpeg is optional."""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import tasty_compare as tc  # noqa: E402


def write_tsv(path: Path, rows: list[tuple[int, int, str]]) -> None:
    """rows: (core_frame, movie_frame, hash)."""
    lines = [
        "core_frame\theader_ctr\tcapture_ns\tdup_reason\tmovie_frame\thash\t"
        "width\theight\tsegment\tavi_frame\n"
    ]
    for i, (core, movie, h) in enumerate(rows):
        lines.append(f"{core}\t0\t{i}\tnone\t{movie}\t{h}\t16\t16\t0\t{i}\n")
    path.write_text("".join(lines))


def solid(w: int, h: int, rgb: tuple[int, int, int]) -> bytes:
    return bytes(rgb) * (w * h)


def gradient(w: int, h: int, seed: int = 0) -> bytes:
    out = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 3
            out[i] = (x * 7 + seed) & 255
            out[i + 1] = (y * 5 + seed) & 255
            out[i + 2] = (x + y + seed) & 255
    return bytes(out)


class TestHashes(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="tasty-cmp-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_identical_tsv(self) -> None:
        rows = [(100 + i, i, f"{i * 17:08x}") for i in range(20)]
        a = self.tmp / "a.frames.tsv"
        b = self.tmp / "b.frames.tsv"
        write_tsv(a, rows)
        write_tsv(b, rows)
        rc = tc.main(
            [
                "--mister",
                str(a),
                "--emu",
                str(b),
                "--hashes-only",
                "--out",
                str(self.tmp / "out"),
            ]
        )
        self.assertEqual(rc, 0)

    def test_one_frame_difference(self) -> None:
        rows = [(100 + i, i, f"{i * 17:08x}") for i in range(20)]
        a = self.tmp / "a.frames.tsv"
        b = self.tmp / "b.frames.tsv"
        write_tsv(a, rows)
        rows[7] = (107, 7, "deadbeef")
        write_tsv(b, rows)
        out = self.tmp / "out"
        rc = tc.main(
            ["--mister", str(a), "--emu", str(b), "--hashes-only", "--out", str(out)]
        )
        self.assertEqual(rc, 1)
        csv = (out / "frames.csv").read_text()
        self.assertIn("7,0,hash-mismatch", csv.replace(" ", ""))

    def test_offset(self) -> None:
        a_rows = [(100 + i, i, f"{(i + 3) * 17:08x}") for i in range(40)]
        b_rows = [(200 + i, i, f"{i * 17:08x}") for i in range(40)]
        a = self.tmp / "a.frames.tsv"
        b = self.tmp / "b.frames.tsv"
        write_tsv(a, a_rows)
        write_tsv(b, b_rows)
        rc0 = tc.main(
            [
                "--mister",
                str(a),
                "--emu",
                str(b),
                "--hashes-only",
                "--offset",
                "0",
                "--overlap",
                "--out",
                str(self.tmp / "o0"),
            ]
        )
        self.assertEqual(rc0, 1)
        rc = tc.main(
            [
                "--mister",
                str(a),
                "--emu",
                str(b),
                "--hashes-only",
                "--auto-offset",
                "--overlap",
                "--out",
                str(self.tmp / "o1"),
            ]
        )
        self.assertEqual(rc, 0)

    def test_missing_file(self) -> None:
        rc = tc.main(
            [
                "--mister",
                str(self.tmp / "no.tsv"),
                "--emu",
                str(self.tmp / "no2.tsv"),
                "--hashes-only",
            ]
        )
        self.assertEqual(rc, 2)

    def test_usage(self) -> None:
        self.assertEqual(tc.main([]), 2)

    def test_missed_dup_skipped(self) -> None:
        a = self.tmp / "a.frames.tsv"
        b = self.tmp / "b.frames.tsv"
        lines = [
            "core_frame\theader_ctr\tcapture_ns\tdup_reason\tmovie_frame\thash\t"
            "width\theight\tsegment\tavi_frame\n"
        ]
        for i in range(10):
            dup = "missed" if i == 4 else "none"
            h = f"{i * 17:08x}"
            lines.append(f"{100 + i}\t0\t{i}\t{dup}\t{i}\t{h}\t16\t16\t0\t{i}\n")
        a.write_text("".join(lines))
        lines_b = [
            "core_frame\theader_ctr\tcapture_ns\tdup_reason\tmovie_frame\thash\t"
            "width\theight\tsegment\tavi_frame\n"
        ]
        for i in range(10):
            lines_b.append(
                f"{200 + i}\t0\t{i}\tnone\t{i}\t{i * 17:08x}\t16\t16\t0\t{i}\n"
            )
        b.write_text("".join(lines_b))
        rc = tc.main(
            [
                "--mister",
                str(a),
                "--emu",
                str(b),
                "--hashes-only",
                "--out",
                str(self.tmp / "out"),
            ]
        )
        self.assertEqual(rc, 0)

    def test_one_frame_slip(self) -> None:
        a_rows = [(100 + i, i, f"{i * 17:08x}") for i in range(80)]
        b_rows = []
        for i in range(80):
            src = i if i < 40 else i - 1
            b_rows.append((200 + i, i, f"{src * 17:08x}"))
        a = self.tmp / "a.frames.tsv"
        b = self.tmp / "b.frames.tsv"
        write_tsv(a, a_rows)
        write_tsv(b, b_rows)
        rc = tc.main(
            [
                "--mister",
                str(a),
                "--emu",
                str(b),
                "--hashes-only",
                "--out",
                str(self.tmp / "slip"),
            ]
        )
        self.assertEqual(rc, 1)
        ha = {i: f"{i * 17:08x}" for i in range(80)}
        hb = {i: f"{(i if i < 40 else i - 1) * 17:08x}" for i in range(80)}
        slip, new_off, _uniq = tc.detect_slip(ha, hb, 0, 40)
        self.assertEqual(new_off, 1)
        self.assertIsNotNone(slip)
        self.assertLessEqual(slip, 40)


class TestPixels(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="tasty-pix-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_refine_slip_unique_is_first_no_offset(self) -> None:
        """Unique picture is the first frame leftover>slop at every offset."""
        w = h = 8
        crop = (0, 0, 0, 0)
        bg, fg = (10, 20, 30), (200, 10, 10)

        def sprite(x0: int, rgb: tuple[int, int, int] = fg) -> bytes:
            out = bytearray(solid(w, h, bg))
            for y in range(2, 6):
                for x in range(x0, x0 + 2):
                    i = (y * w + x) * 3
                    out[i : i + 3] = bytes(rgb)
            return bytes(out)

        def pos(i: int) -> int:
            return 1 + (i % 4)

        mpix = {i: sprite(pos(i)) for i in range(1, 40)}
        epix = {i: sprite(pos(i if i < 20 else i - 1)) for i in range(1, 45)}
        mpix[30] = sprite(0)
        self.assertGreater(tc.leftover_count(epix[20], mpix[20]), 0)
        self.assertEqual(tc.leftover_count(epix[21], mpix[20]), 0)
        slip, off, unique = tc.refine_slip(
            mpix, epix, w, h, w, h, crop, crop, 0, 20, slop=0
        )
        self.assertEqual(off, 1)
        self.assertEqual(slip, 20)
        self.assertEqual(unique, 30)

    def test_leftover_majority_slop(self) -> None:
        w, h = 8, 8
        a = solid(w, h, (10, 20, 30))
        b = solid(w, h, (200, 10, 10))
        self.assertEqual(tc.leftover_count(b, a), 0)
        c = bytearray(a)
        c[0:3] = bytes((255, 255, 255))
        self.assertEqual(tc.leftover_count(b, bytes(c)), 1)
        d1, d2 = self.tmp / "m", self.tmp / "e"
        d1.mkdir()
        d2.mkdir()
        tc.write_png(d1 / "frame-000001.png", bytes(c), w, h)
        tc.write_png(d2 / "frame-000001.png", b, w, h)
        rc = tc.main(
            [
                "--mister",
                str(d1),
                "--emu",
                str(d2),
                "--pixel-slop",
                "32",
                "--out",
                str(self.tmp / "out"),
                "--around",
                "0",
            ]
        )
        self.assertEqual(rc, 0)

    def test_edge_map_palette_free(self) -> None:
        w, h = 8, 8
        a = solid(w, h, (10, 20, 30))
        b = solid(w, h, (200, 10, 10))
        self.assertEqual(tc.edge_hash(a, w, h), tc.edge_hash(b, w, h))
        c = bytearray(a)
        c[3 * (4 * w + 4)] ^= 0xFF
        self.assertNotEqual(tc.edge_hash(a, w, h), tc.edge_hash(bytes(c), w, h))

    def test_palette_bijection(self) -> None:
        w, h = 8, 8
        mister = solid(w, h, (10, 20, 30))
        emu = solid(w, h, (200, 10, 10))
        bij, conflicts = tc.learn_bijection([(emu, mister)])
        self.assertEqual(conflicts, [])
        mapped = tc.apply_bijection(emu, bij)
        self.assertEqual(mapped, mister)

    def test_overscan_crop(self) -> None:
        inner = gradient(16, 16, seed=3)
        tall = bytearray(16 * 32 * 3)
        # 8 blank lines, 16 content, 8 blank
        tall[8 * 16 * 3 : 24 * 16 * 3] = inner
        cropped, nw, nh = tc.crop_bytes(bytes(tall), 16, 32, (8, 8, 0, 0))
        self.assertEqual((nw, nh), (16, 16))
        self.assertEqual(cropped, inner)
        c1, c2 = tc.auto_crop(16, 32, 16, 16)
        self.assertEqual(c1, (8, 8, 0, 0))
        self.assertEqual(c2, (0, 0, 0, 0))
        c_hi, c_lo = tc.auto_crop(512, 224, 256, 224)
        self.assertEqual(c_hi, (0, 0, 128, 128))
        self.assertEqual(c_lo, (0, 0, 0, 0))

    def test_png_roundtrip_and_diff(self) -> None:
        w, h = 16, 16
        a = gradient(w, h, 1)
        b = bytearray(a)
        b[50] ^= 0xFF
        pa = self.tmp / "a.png"
        pb = self.tmp / "b.png"
        tc.write_png(pa, a, w, h)
        tc.write_png(pb, bytes(b), w, h)
        ra, wa, ha = tc.read_png(pa)
        self.assertEqual((wa, ha), (w, h))
        self.assertEqual(ra, a)
        da, db = self.tmp / "emu_a", self.tmp / "emu_b"
        da.mkdir()
        db.mkdir()
        shutil.copy(pa, da / "frame-000000.png")
        shutil.copy(pb, db / "frame-000000.png")
        out = self.tmp / "out"
        rc = tc.main(
            [
                "--mister",
                str(da),
                "--emu",
                str(db),
                "--no-colour-map",
                "--out",
                str(out),
                "--around",
                "0",
            ]
        )
        self.assertEqual(rc, 1)
        self.assertTrue((out / "frame-000000-sheet.png").is_file())

    def test_png_named_movie_frames(self) -> None:
        w, h = 8, 8
        a = gradient(w, h, 1)
        b = bytearray(a)
        b[20] ^= 0xFF
        d1, d2 = self.tmp / "m", self.tmp / "e"
        d1.mkdir()
        d2.mkdir()
        tc.write_png(d1 / "frame-000010.png", a, w, h)
        tc.write_png(d1 / "frame-000011.png", a, w, h)
        tc.write_png(d2 / "frame-000010.png", a, w, h)
        tc.write_png(d2 / "frame-000011.png", bytes(b), w, h)
        out = self.tmp / "out"
        rc = tc.main(
            [
                "--mister",
                str(d1),
                "--emu",
                str(d2),
                "--no-colour-map",
                "--out",
                str(out),
                "--around",
                "0",
            ]
        )
        self.assertEqual(rc, 1)
        csv = (out / "frames.csv").read_text()
        self.assertIn("11,0,", csv.replace(" ", ""))
        self.assertTrue((out / "frame-000011-sheet.png").is_file())

    def test_png_identical(self) -> None:
        w, h = 8, 8
        pix = gradient(w, h, 9)
        d1, d2 = self.tmp / "m", self.tmp / "e"
        d1.mkdir()
        d2.mkdir()
        tc.write_png(d1 / "frame-000000.png", pix, w, h)
        tc.write_png(d2 / "frame-000000.png", pix, w, h)
        rc = tc.main(
            [
                "--mister",
                str(d1),
                "--emu",
                str(d2),
                "--no-colour-map",
                "--out",
                str(self.tmp / "out"),
            ]
        )
        self.assertEqual(rc, 0)

    def test_crc32_rgb(self) -> None:
        pix = solid(4, 2, (1, 2, 3))
        h = tc.crc32_rgb(pix, 4, 2)
        self.assertEqual(len(h), 8)

    def test_check_deps(self) -> None:
        rc = tc.main(["--check-deps"])
        self.assertIn(rc, (0, 2))

    def test_raw_avi_without_sidecar(self) -> None:
        ffmpeg = shutil.which("ffmpeg")
        ffprobe = shutil.which("ffprobe")
        if not ffmpeg or not ffprobe:
            self.skipTest("ffmpeg/ffprobe not on PATH")
        w, h = 8, 8
        pix = gradient(w, h, 4)
        raw = self.tmp / "frame.rgb"
        raw.write_bytes(pix + pix)
        a_avi = self.tmp / "a.avi"
        b_avi = self.tmp / "b.avi"
        for dest in (a_avi, b_avi):
            r = subprocess.run(
                [
                    ffmpeg,
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-f",
                    "rawvideo",
                    "-pix_fmt",
                    "rgb24",
                    "-s",
                    f"{w}x{h}",
                    "-r",
                    "1",
                    "-i",
                    str(raw),
                    "-frames:v",
                    "2",
                    "-c:v",
                    "rawvideo",
                    str(dest),
                ],
                capture_output=True,
                text=True,
            )
            if r.returncode != 0 or not dest.is_file():
                self.skipTest(f"ffmpeg could not write AVI: {r.stderr}")
        rc = tc.main(
            [
                "--mister",
                str(a_avi),
                "--emu",
                str(b_avi),
                "--no-colour-map",
                "--out",
                str(self.tmp / "out"),
            ]
        )
        self.assertEqual(rc, 0)


class TestRecordings(unittest.TestCase):
    """Real SMB1 sidecars when TASTY_COMPARE_RECORDINGS points at them."""

    @classmethod
    def setUpClass(cls) -> None:
        raw = os.environ.get("TASTY_COMPARE_RECORDINGS", "")
        cls.root = Path(raw) if raw else None
        if cls.root and not (cls.root / "smb1-avi.frames.tsv").is_file():
            cls.root = None

    def test_avi_vs_hash_run_identical(self) -> None:
        if not self.root:
            self.skipTest("TASTY_COMPARE_RECORDINGS not set")
        a = self.root / "smb1-avi.frames.tsv"
        b = self.root / "r2" / "smb1-hash-1.frames.tsv"
        tmp = Path(tempfile.mkdtemp(prefix="tasty-smb-"))
        try:
            rc = tc.main(
                [
                    "--mister",
                    str(a),
                    "--emu",
                    str(b),
                    "--hashes-only",
                    "--out",
                    str(tmp),
                ]
            )
            self.assertEqual(rc, 0)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_altered_frame_reported(self) -> None:
        if not self.root:
            self.skipTest("TASTY_COMPARE_RECORDINGS not set")
        src = (self.root / "smb1-avi.frames.tsv").read_text().splitlines(True)
        # Flip a hash on movie frame 100 (header + 101st data line is not guaranteed).
        out_lines = []
        flipped = None
        for line in src:
            if flipped is None and not line.startswith("core_frame") and "\t" in line:
                cols = line.rstrip("\n").split("\t")
                if len(cols) > 5 and cols[4] == "100":
                    cols[5] = "ffffffff"
                    line = "\t".join(cols) + "\n"
                    flipped = 100
            out_lines.append(line)
        self.assertEqual(flipped, 100)
        tmp = Path(tempfile.mkdtemp(prefix="tasty-alt-"))
        try:
            bad = tmp / "bad.frames.tsv"
            bad.write_text("".join(out_lines))
            rc = tc.main(
                [
                    "--mister",
                    str(self.root / "smb1-avi.frames.tsv"),
                    "--emu",
                    str(bad),
                    "--hashes-only",
                    "--out",
                    str(tmp / "out"),
                ]
            )
            self.assertEqual(rc, 1)
            csv = (tmp / "out" / "frames.csv").read_text()
            self.assertIn("100,0,", csv.replace(" ", ""))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()
