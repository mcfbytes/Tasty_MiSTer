-- Per-frame dump for tasty-compare. Desktop Linux only (not the DE10-Nano).
--
--   TASTY_DUMP_DIR=./emu-dump TASTY_DUMP_PNG=1 \
--     fceux --sound 0 --playmov movie.fm2 --loadlua fceux-dump.lua rom.nes
--
-- Writes emu.hashes.tsv (movie_frame, lag, crc, kind). PNG names use
-- emu.framecount(); that is MiSTer movie_frame m at offset 0. Qt 256x224 is
-- getscreenpixel rows 8..231, 1:1 with the core. TASTY_DUMP_LIMIT=N then
-- os.exit (Qt ignores emu.exit). Wrap headless runs with timeout -k 2.

emu.speedmode("maximum")

local outdir = os.getenv("TASTY_DUMP_DIR") or "."
local want_png = os.getenv("TASTY_DUMP_PNG") == "1"
local want_edges = os.getenv("TASTY_DUMP_EDGES") == "1"
local no_hash = os.getenv("TASTY_DUMP_NOHASH") == "1"
local limit = tonumber(os.getenv("TASTY_DUMP_LIMIT") or "0") or 0
local png_from = tonumber(os.getenv("TASTY_DUMP_PNG_FROM") or "0") or 0
local png_to = tonumber(os.getenv("TASTY_DUMP_PNG_TO") or "0") or 0
local full = os.getenv("TASTY_DUMP_FULL") == "1"
local y0, y1 = 8, 231
local x0, x1 = 0, 255
if full then
    y0, y1 = 0, 239
end
local done = false
local w = x1 - x0 + 1
local h = y1 - y0 + 1

local function u32(n)
    n = n % 4294967296
    if n < 0 then
        n = n + 4294967296
    end
    return n
end

local crc_table
local function crc32_init()
    crc_table = {}
    for i = 0, 255 do
        local c = i
        for _ = 1, 8 do
            if c % 2 == 1 then
                c = u32(bit.bxor(math.floor(c / 2), 0xEDB88320))
            else
                c = math.floor(c / 2)
            end
        end
        crc_table[i] = c
    end
end

local function crc32(str)
    local crc = 4294967295
    for i = 1, #str do
        local b = string.byte(str, i)
        crc = u32(bit.bxor(crc_table[u32(bit.bxor(crc, b)) % 256], math.floor(crc / 256)))
    end
    return u32(bit.bxor(crc, 4294967295))
end

local function hex8(n)
    return string.format("%08x", u32(n))
end

crc32_init()
os.execute('mkdir -p "' .. outdir .. '"')
local log = io.open(outdir .. "/emu.hashes.tsv", "w")
log:write("# movie_frame\tlag\thash\tkind\n")

local function on_frame()
    if done or log == nil then
        return
    end
    local fc = emu.framecount()
    local lag = 0
    if emu.lagged and emu.lagged() then
        lag = 1
    end
    local pix = {}
    local blob = ""
    if not no_hash then
    for y = y0, y1 do
        for x = x0, x1 do
            local r, g, b, p = emu.getscreenpixel(x, y, true)
            if r == nil then
                r, g, b = 0, 0, 0
            end
            if os.getenv("TASTY_DUMP_INDEX") == "1" and p ~= nil then
                pix[#pix + 1] = string.char(p)
            else
                pix[#pix + 1] = string.char(r, g, b)
            end
        end
    end
    blob = table.concat(pix)
    end
    local kind = no_hash and "png" or "rgb"
    local hashed = blob
    if os.getenv("TASTY_DUMP_INDEX") == "1" and #blob == w * h then
        kind = "index"
    elseif want_edges and #blob == w * h * 3 then
        local e = {}
        for y = 0, h - 2 do
            for x = 0, w - 2 do
                local i = (y * w + x) * 3
                local r = blob:byte(i + 1)
                local g = blob:byte(i + 2)
                local b = blob:byte(i + 3)
                local rr, rg, rb = blob:byte(i + 4, i + 6)
                local di = i + w * 3
                local dr, dg, db = blob:byte(di + 1, di + 3)
                if r ~= rr or g ~= rg or b ~= rb or r ~= dr or g ~= dg or b ~= db then
                    e[#e + 1] = string.char(1)
                else
                    e[#e + 1] = string.char(0)
                end
            end
            e[#e + 1] = string.char(0)
        end
        for _ = 1, w do
            e[#e + 1] = string.char(0)
        end
        hashed = table.concat(e)
        kind = "edges"
    end
    local hash = no_hash and "-" or hex8(crc32(hashed))
    log:write(fc .. "\t" .. lag .. "\t" .. hash .. "\t" .. kind .. "\n")
    log:flush()
    local do_png = want_png
    if png_to > 0 then
        do_png = want_png and fc >= png_from and fc <= png_to
    elseif png_from > 0 then
        do_png = want_png and fc >= png_from
    end
    if do_png and gui.savescreenshotas then
        gui.savescreenshotas(string.format("%s/frame-%06d.png", outdir, fc))
    end
    local over = (limit > 0 and fc >= limit)
    local movie_done = false
    if movie and movie.mode then
        local m = movie.mode()
        if m == "finished" then
            movie_done = true
        end
    end
    if over or movie_done then
        done = true
        log:close()
        log = nil
        os.exit(0)
    end
end

emu.registerafter(on_frame)
