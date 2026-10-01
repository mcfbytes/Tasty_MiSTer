-- BizHawk / EmuHawk dump for tasty-compare (NES, SNES, Genesis, PSX).
-- Desktop Linux only. Open the ROM, load this script, play the movie.
--
--   TASTY_DUMP_DIR  output directory (default .)
--   TASTY_DUMP_PNG  1 = also write frame-NNNNNN.png via client.screenshot
--
-- Writes emu.hashes.tsv (movie_frame, lag, hash, kind). The hash column is
-- "-": this script computes no pixel hash, so compare the PNG directory
-- (TASTY_DUMP_PNG=1), never the log alone.

local outdir = os.getenv("TASTY_DUMP_DIR") or "."
local want_png = os.getenv("TASTY_DUMP_PNG") == "1"

if not os.execute then
    outdir = "."
end
os.execute('mkdir -p "' .. outdir .. '"')
local log = io.open(outdir .. "/emu.hashes.tsv", "w")
log:write("# movie_frame\tlag\thash\tkind\n")

local function hex8(n)
    if n < 0 then
        n = n + 4294967296
    end
    return string.format("%08x", n)
end

event.onframestart(function()
    local fc = emu.framecount()
    local lag = 0
    if emu.islagged and emu.islagged() then
        lag = 1
    end
    local kind = "png"
    local hash = "-"
    if want_png and client and client.screenshot then
        local png = string.format("%s/frame-%06d.png", outdir, fc)
        client.screenshot(png)
        kind = "png"
    end
    log:write(fc .. "\t" .. lag .. "\t" .. hash .. "\t" .. kind .. "\n")
    log:flush()
end)
