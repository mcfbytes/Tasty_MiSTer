using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using TriCNES;

namespace TricnesDump
{
    // Headless TriCNES dump. See tools/compare/README.md, TriCNES (NES).
    static class Program
    {
        const int Width = 256;
        const int Height = 240;
        const int CropY0 = 8;
        const int CropH = 224;

        static int Main(string[] args)
        {
            if (!CrcSelfCheck())
            {
                Console.Error.WriteLine("crc32 self-check failed");
                return 2;
            }
            string rom = null;
            string movie = null;
            string outDir = ".";
            int limit = 34100;
            int ppuPhase = 0;
            int cpuPhase = 0;
            bool fceuxFrame0 = true;
            bool ppuReset = false;
            string pngDir = null;
            string pngSpec = null;
            for (int i = 0; i < args.Length; i++)
            {
                string a = args[i];
                if (a == "--rom") rom = Need(args, ref i);
                else if (a == "--movie") movie = Need(args, ref i);
                else if (a == "--out") outDir = Need(args, ref i);
                else if (a == "--limit") limit = IntArg(args, ref i);
                else if (a == "--ppu-phase") ppuPhase = IntArg(args, ref i);
                else if (a == "--cpu-phase") cpuPhase = IntArg(args, ref i);
                else if (a == "--fceux-frame0") fceuxFrame0 = BoolArg(args, ref i);
                else if (a == "--ppu-reset") ppuReset = BoolArg(args, ref i);
                else if (a == "--png") pngDir = Need(args, ref i);
                else if (a == "--png-spec") pngSpec = Need(args, ref i);
                else
                {
                    Console.Error.WriteLine("unknown argument " + a);
                    return 2;
                }
            }
            if (rom == null || movie == null)
            {
                Console.Error.WriteLine("need --rom and --movie");
                return 2;
            }
            if (ppuPhase < 0 || ppuPhase > 3 || cpuPhase < 0 || cpuPhase > 11)
            {
                Console.Error.WriteLine("ppu-phase is 0..3 and cpu-phase is 0..11");
                return 2;
            }
            if (!File.Exists(rom) || !File.Exists(movie))
            {
                Console.Error.WriteLine("rom or movie is missing");
                return 2;
            }

            ushort[] inputs;
            bool[] resets;
            ParseFm2(movie, out inputs, out resets);
            Console.Error.WriteLine(
                "fm2 inputs " + inputs.Length
                + " ppu " + ppuPhase
                + " cpu " + cpuPhase
                + " fceux-frame0 " + (fceuxFrame0 ? 1 : 0)
                + " ppu-reset " + (ppuReset ? 1 : 0));

            var emu = new Emulator();
            emu.PPU_DecodeSignal = false;
            emu.PPU_ShowRawNTSCSignal = false;
            emu.PPU_ShowScreenBorders = false;
            var cart = new Cartridge(rom);
            emu.Cart = cart;
            cart.Emu = emu;
            emu.TAS_ReadingTAS = true;
            emu.TAS_InputLog = inputs;
            emu.TAS_ResetLog = resets;
            emu.ClockFiltering = false;
            emu.PPUClock = (byte)ppuPhase;
            emu.CPUClock = (byte)cpuPhase;
            emu.TAS_InputSequenceIndex = 0;
            emu.PPU_RESET = ppuReset;
            if (fceuxFrame0)
            {
                emu.PPU_Scanline = 239;
                emu.PPU_Dot = 312;
                emu.SyncFM2 = true;
                emu.TAS_InputSequenceIndex--;
            }
            else
            {
                emu.TAS_InputSequenceIndex++;
                emu.PPU_Dot = 0;
            }
            for (int i = 0; i < emu.RAM.Length; i++)
            {
                emu.RAM[i] = (byte)(((i & 7) > 4) ? 0xFF : 0x00);
            }

            Directory.CreateDirectory(outDir);
            bool[] keepPng = null;
            byte[] pngScan = null;
            if (pngDir != null)
            {
                Directory.CreateDirectory(pngDir);
                keepPng = KeepMask(pngSpec, limit);
                pngScan = new byte[CropH * (Width * 3 + 1)];
            }
            string note = "ppu_phase=" + ppuPhase
                + " cpu_phase=" + cpuPhase
                + " fceux_frame0=" + (fceuxFrame0 ? 1 : 0)
                + " ppu_reset=" + (ppuReset ? 1 : 0);
            string hashPath = Path.Combine(outDir, "emu.hashes.tsv");
            string edgePath = Path.Combine(outDir, "emu.edges.tsv");
            int pngCount = 0;
            using (var hashes = new StreamWriter(hashPath))
            using (var edges = new StreamWriter(edgePath))
            {
                hashes.Write("# movie_frame\tlag\thash\tkind\n");
                hashes.Write("# tricnes 256x240 rgb " + note + "\n");
                edges.Write("# movie_frame\tlag\thash\tkind\n");
                edges.Write("# tricnes edge_map rows 8..231 " + note + "\n");
                var rgb = new byte[Width * Height * 3];
                var edge = new byte[Width * CropH];
                var crop = new byte[Width * CropH * 3];
                long t0 = Environment.TickCount64;
                for (int frame = 1; frame <= limit; frame++)
                {
                    emu._CoreFrameAdvance();
                    int lag = emu.LagFrame ? 1 : 0;
                    HashPicture(emu, rgb, crop, edge);
                    if (keepPng != null && keepPng[frame])
                    {
                        string pngPath = Path.Combine(
                            pngDir, "frame-" + frame.ToString("D6", CultureInfo.InvariantCulture) + ".png");
                        WriteRgbPng(pngPath, crop, Width, CropH, pngScan);
                        pngCount++;
                    }
                    hashes.Write(frame.ToString(CultureInfo.InvariantCulture));
                    hashes.Write('\t');
                    hashes.Write(lag.ToString(CultureInfo.InvariantCulture));
                    hashes.Write('\t');
                    hashes.Write(Hex8(Crc32(rgb, rgb.Length)));
                    hashes.Write("\trgb\n");
                    edges.Write(frame.ToString(CultureInfo.InvariantCulture));
                    edges.Write('\t');
                    edges.Write(lag.ToString(CultureInfo.InvariantCulture));
                    edges.Write('\t');
                    edges.Write(Hex8(Crc32(edge, edge.Length)));
                    edges.Write("\tedges\n");
                    if ((frame % 500) == 0)
                    {
                        hashes.Flush();
                        edges.Flush();
                        double sec = (Environment.TickCount64 - t0) / 1000.0;
                        Console.Error.WriteLine(
                            "frame " + frame + " " + (sec > 0 ? (frame / sec).ToString("0.0", CultureInfo.InvariantCulture) : "?") + "/s");
                    }
                }
            }
            emu.Dispose();
            Console.Error.WriteLine("wrote " + hashPath);
            if (pngDir != null)
            {
                Console.Error.WriteLine("png " + pngCount + " " + pngDir);
            }
            return 0;
        }

        // spec is comma-separated a-b:stride. Empty spec keeps every frame.
        static bool[] KeepMask(string spec, int limit)
        {
            var keep = new bool[limit + 1];
            if (string.IsNullOrEmpty(spec))
            {
                for (int f = 1; f <= limit; f++)
                {
                    keep[f] = true;
                }
                return keep;
            }
            foreach (string piece in spec.Split(','))
            {
                string p = piece.Trim();
                if (p.Length == 0)
                {
                    continue;
                }
                int stride = 1;
                int colon = p.IndexOf(':');
                if (colon >= 0)
                {
                    stride = int.Parse(p.Substring(colon + 1), CultureInfo.InvariantCulture);
                    p = p.Substring(0, colon);
                }
                if (stride < 1)
                {
                    throw new ArgumentException("png-spec stride");
                }
                int dash = p.IndexOf('-');
                int from;
                int to;
                if (dash < 0)
                {
                    from = to = int.Parse(p, CultureInfo.InvariantCulture);
                }
                else
                {
                    from = int.Parse(p.Substring(0, dash), CultureInfo.InvariantCulture);
                    to = int.Parse(p.Substring(dash + 1), CultureInfo.InvariantCulture);
                }
                if (from < 1)
                {
                    from = 1;
                }
                if (to > limit)
                {
                    to = limit;
                }
                for (int f = from; f <= to; f += stride)
                {
                    keep[f] = true;
                }
            }
            return keep;
        }

        static void HashPicture(Emulator emu, byte[] rgb, byte[] crop, byte[] edge)
        {
            int[] bits = emu.Screen.Bits;
            int n = 0;
            for (int i = 0; i < Width * Height; i++)
            {
                int c = bits[i];
                rgb[n++] = (byte)((c >> 16) & 255);
                rgb[n++] = (byte)((c >> 8) & 255);
                rgb[n++] = (byte)(c & 255);
            }
            int cropN = 0;
            for (int y = 0; y < CropH; y++)
            {
                int src = ((y + CropY0) * Width) * 3;
                for (int x = 0; x < Width * 3; x++)
                {
                    crop[cropN++] = rgb[src + x];
                }
            }
            Array.Clear(edge, 0, edge.Length);
            int row = Width * 3;
            for (int y = 0; y < CropH - 1; y++)
            {
                for (int x = 0; x < Width - 1; x++)
                {
                    int i = y * row + x * 3;
                    int right = i + 3;
                    int down = i + row;
                    if (crop[i] != crop[right] || crop[i + 1] != crop[right + 1] || crop[i + 2] != crop[right + 2]
                        || crop[i] != crop[down] || crop[i + 1] != crop[down + 1] || crop[i + 2] != crop[down + 2])
                    {
                        edge[y * Width + x] = 1;
                    }
                }
            }
        }

        static void ParseFm2(string path, out ushort[] inputs, out bool[] resets)
        {
            byte[] file = File.ReadAllBytes(path);
            bool port0 = false;
            bool port1 = false;
            int i = 0;
            while (i + 8 < file.Length)
            {
                if (file[i] == 0x0A
                    && file[i + 1] == 0x70 && file[i + 2] == 0x6F && file[i + 3] == 0x72
                    && file[i + 4] == 0x74 && file[i + 5] == 0x30 && file[i + 6] == 0x20)
                {
                    port0 = file[i + 7] == 0x31;
                }
                if (file[i] == 0x0A
                    && file[i + 1] == 0x70 && file[i + 2] == 0x6F && file[i + 3] == 0x72
                    && file[i + 4] == 0x74 && file[i + 5] == 0x31 && file[i + 6] == 0x20)
                {
                    port1 = file[i + 7] == 0x31;
                }
                if (file[i] == 0x0A && file[i + 1] == 0x7C)
                {
                    break;
                }
                i++;
            }
            var list = new List<ushort>(40000);
            var resetList = new List<bool>(40000);
            while (i < file.Length)
            {
                if (file[i] == 0x0A)
                {
                    if (i == file.Length - 1)
                    {
                        break;
                    }
                    if (file[i + 1] == 0x0A)
                    {
                        i++;
                        continue;
                    }
                    if (file[i + 1] == 0x23)
                    {
                        i++;
                        continue;
                    }
                    bool reset = (file[i + 2] & 1) == 1;
                    int port0Index = 0;
                    int port1Index = 0;
                    if (port0)
                    {
                        port0Index = i + 4;
                        if (port1)
                        {
                            port1Index = i + 0xD;
                        }
                    }
                    else if (port1)
                    {
                        port1Index = i + 0x6;
                    }
                    ushort u = 0;
                    if (port0)
                    {
                        u |= (ushort)(file[port0Index] == 0x2E ? 0 : 1);
                        u |= (ushort)(file[port0Index + 1] == 0x2E ? 0 : 2);
                        u |= (ushort)(file[port0Index + 2] == 0x2E ? 0 : 4);
                        u |= (ushort)(file[port0Index + 3] == 0x2E ? 0 : 8);
                        u |= (ushort)(file[port0Index + 4] == 0x2E ? 0 : 0x10);
                        u |= (ushort)(file[port0Index + 5] == 0x2E ? 0 : 0x20);
                        u |= (ushort)(file[port0Index + 6] == 0x2E ? 0 : 0x40);
                        u |= (ushort)(file[port0Index + 7] == 0x2E ? 0 : 0x80);
                    }
                    if (port1)
                    {
                        u |= (ushort)(file[port1Index] == 0x2E ? 0 : 0x100);
                        u |= (ushort)(file[port1Index + 1] == 0x2E ? 0 : 0x200);
                        u |= (ushort)(file[port1Index + 2] == 0x2E ? 0 : 0x400);
                        u |= (ushort)(file[port1Index + 3] == 0x2E ? 0 : 0x800);
                        u |= (ushort)(file[port1Index + 4] == 0x2E ? 0 : 0x1000);
                        u |= (ushort)(file[port1Index + 5] == 0x2E ? 0 : 0x2000);
                        u |= (ushort)(file[port1Index + 6] == 0x2E ? 0 : 0x4000);
                        u |= (ushort)(file[port1Index + 7] == 0x2E ? 0 : 0x8000);
                    }
                    list.Add(u);
                    resetList.Add(reset);
                }
                i++;
            }
            inputs = list.ToArray();
            resets = resetList.ToArray();
        }

        static readonly uint[] CrcTable = BuildCrc();

        static uint[] BuildCrc()
        {
            var table = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i;
                for (int k = 0; k < 8; k++)
                {
                    c = (c & 1) != 0 ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            return table;
        }

        static uint Crc32Feed(uint crc, byte[] data, int length)
        {
            for (int i = 0; i < length; i++)
            {
                crc = CrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
            }
            return crc;
        }

        static uint Crc32(byte[] data, int length)
        {
            return Crc32Feed(0xFFFFFFFFu, data, length) ^ 0xFFFFFFFFu;
        }

        static uint Crc32Parts(byte[] a, byte[] b)
        {
            return Crc32Feed(Crc32Feed(0xFFFFFFFFu, a, a.Length), b, b.Length) ^ 0xFFFFFFFFu;
        }

        static readonly byte[] PngSig = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        static readonly byte[] IhdrTag = { 0x49, 0x48, 0x44, 0x52 };
        static readonly byte[] IdatTag = { 0x49, 0x44, 0x41, 0x54 };
        static readonly byte[] IendTag = { 0x49, 0x45, 0x4E, 0x44 };
        static readonly byte[] Empty = Array.Empty<byte>();

        // RGB8, filter None, one IDAT. scan is height * (width * 3 + 1).
        static void WriteRgbPng(string path, byte[] rgb, int width, int height, byte[] scan)
        {
            int stride = width * 3;
            int need = height * (stride + 1);
            int o = 0;
            for (int y = 0; y < height; y++)
            {
                scan[o++] = 0;
                Buffer.BlockCopy(rgb, y * stride, scan, o, stride);
                o += stride;
            }
            byte[] idat;
            using (var ms = new MemoryStream())
            {
                using (var zlib = new ZLibStream(ms, CompressionLevel.Fastest, true))
                {
                    zlib.Write(scan, 0, need);
                }
                idat = ms.ToArray();
            }
            var ihdr = new byte[13];
            WriteBe32(ihdr, 0, width);
            WriteBe32(ihdr, 4, height);
            ihdr[8] = 8;
            ihdr[9] = 2;
            using (var fs = File.Create(path))
            {
                fs.Write(PngSig, 0, PngSig.Length);
                WriteChunk(fs, IhdrTag, ihdr);
                WriteChunk(fs, IdatTag, idat);
                WriteChunk(fs, IendTag, Empty);
            }
        }

        static void WriteChunk(Stream stream, byte[] tag, byte[] data)
        {
            var len = new byte[4];
            var crc = new byte[4];
            WriteBe32(len, 0, data.Length);
            WriteBe32(crc, 0, (int)Crc32Parts(tag, data));
            stream.Write(len, 0, 4);
            stream.Write(tag, 0, 4);
            if (data.Length != 0)
            {
                stream.Write(data, 0, data.Length);
            }
            stream.Write(crc, 0, 4);
        }

        static void WriteBe32(byte[] dest, int at, int value)
        {
            uint u = (uint)value;
            dest[at] = (byte)(u >> 24);
            dest[at + 1] = (byte)(u >> 16);
            dest[at + 2] = (byte)(u >> 8);
            dest[at + 3] = (byte)u;
        }

        static bool CrcSelfCheck()
        {
            byte[] known = System.Text.Encoding.ASCII.GetBytes("123456789");
            return Crc32(known, known.Length) == 0xCBF43926u;
        }

        static string Hex8(uint n)
        {
            return n.ToString("x8", CultureInfo.InvariantCulture);
        }

        static string Need(string[] args, ref int i)
        {
            if (i + 1 >= args.Length)
            {
                throw new ArgumentException("missing value after " + args[i]);
            }
            i++;
            return args[i];
        }

        static int IntArg(string[] args, ref int i)
        {
            return int.Parse(Need(args, ref i), CultureInfo.InvariantCulture);
        }

        static bool BoolArg(string[] args, ref int i)
        {
            string v = Need(args, ref i);
            if (v == "1" || v == "true" || v == "yes") return true;
            if (v == "0" || v == "false" || v == "no") return false;
            throw new ArgumentException("expected true/false, got " + v);
        }
    }
}
