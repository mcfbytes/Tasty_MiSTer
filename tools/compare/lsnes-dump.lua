-- lsnes dump for tasty-compare (SNES). Desktop Linux only.
--
-- Load this script, then play the movie. Writes emu.hashes.tsv and optional
-- PNGs. If screenshot is unavailable, dump a lossless AVI from lsnes
-- (File -> Dump video) from power-on and pass that AVI to tasty-compare.

local outdir = os.getenv("TASTY_DUMP_DIR") or "."
local want_png = os.getenv("TASTY_DUMP_PNG") == "1"
os.execute('mkdir -p "' .. outdir .. '"')
local log = io.open(outdir .. "/emu.hashes.tsv", "w")
log:write("# movie_frame\tlag\thash\tkind\n")

function on_frame()
    local fc = movie.currentframe()
    local lag = 0
    local kind = "png"
    if want_png and gui and gui.screenshot then
        gui.screenshot(string.format("%s/frame-%06d.png", outdir, fc))
    end
    log:write(tostring(fc) .. "\t" .. tostring(lag) .. "\t0\t" .. kind .. "\n")
    log:flush()
end
