// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neo_file.h"

#include "infra/byte_view.h"

namespace mister::cores::neogeo {

namespace {

std::uint32_t get_u32(std::span<const std::uint8_t> b, std::size_t off) noexcept {
    return static_cast<std::uint32_t>(b[off])
        | (static_cast<std::uint32_t>(b[off + 1]) << 8)
        | (static_cast<std::uint32_t>(b[off + 2]) << 16)
        | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

}

std::optional<NeoHeader> parse_neo_header(std::span<const std::uint8_t> buf) noexcept {
    if (buf.size() < kNeoHeaderBytes)
        return std::nullopt;

    NeoHeader h{};
    h.magic = {buf[0], buf[1], buf[2]};
    h.version = buf[3];
    h.p_size = get_u32(buf, 4);
    h.s_size = get_u32(buf, 8);
    h.m_size = get_u32(buf, 12);
    h.v1_size = get_u32(buf, 16);
    h.v2_size = get_u32(buf, 20);
    h.c_size = get_u32(buf, 24);
    h.year = get_u32(buf, 28);
    h.genre = get_u32(buf, 32);
    h.screenshot = get_u32(buf, 36);
    h.ngh = get_u32(buf, 40);
    for (std::size_t i = 0; i < h.name.size(); ++i)
        h.name[i] = buf[44 + i];
    for (std::size_t i = 0; i < h.manu.size(); ++i)
        h.manu[i] = buf[77 + i];
    return h;
}

std::string_view name_view(const NeoHeader& hdr) noexcept {
    std::size_t n = 0;
    while (n < hdr.name.size() && hdr.name[n] != 0)
        ++n;
    return infra::as_text(std::span(hdr.name).first(n));
}

const std::array<NeoQuirk, kNeoQuirkCount> kNeoQuirks{{
    {0x022, 0, 0, 0, 0, 0, 1, 0, 0},
    {0x050, 0, 0, 0, 0, 0, 0, 1, 0},
    {0x052, 1, 0, 0, 0, 0, 0, 0, 0},
    {0x047, 1, 0, 0, 0, 0, 0, 0, 0},
    {0x006, 2, 0, 0, 0, 0, 0, 0, 0},
    {0x263, 0, 1, 0, 0, 0, 0, 0, 0},
    {0x253, 0, 1, 0, 2, 0, 0, 0, 0},
    {0x251, 0, 0, 0, 1, 0, 0, 0, 0},
    {0x257, 0, 2, 0, 5, 0, 0, 0, 0},
    {0x271, 0, 2, 1, 0, 0, 0, 0, 0},
    {0x055, 0, 0, 0, 0, 0, 0, 1, 1},
    {0x266, 0, 2, 0, 0, 0, 0, 0, 0},
    {0x256, 0, 1, 0, 4, 0, 0, 0, 0},
    {0x268, 0, 0, 1, 0, 0, 0, 0, 0},
    {0x269, 0, 2, 1, 0, 0, 0, 0, 0},
    {0x008, 0, 0, 0, 0, 1, 0, 0, 0},
    {0x3E7, 0, 0, 0, 0, 1, 0, 0, 0},
}};

NeoCartConfig resolve_quirks(const NeoHeader& hdr) noexcept {
    NeoCartConfig cfg{};

    for (const NeoQuirk& q : kNeoQuirks) {
        if (q.id != hdr.ngh)
            continue;

        bool skip = false;
        switch (hdr.ngh) {
            case 0x0251:
                if (hdr.p_size != 9437184)
                    skip = true;
                break;
            case 0x0253:
                if (hdr.p_size != 9437184)
                    skip = true;
                if (hdr.name[27] == 'A') {
                    cfg.sma = 3;
                    cfg.cmc = 1;
                    cfg.vrom_mirror = false;
                    skip = true;
                }
                break;
            case 0x0256:
                if (hdr.p_size != 9437184)
                    skip = true;
                break;
            case 0x0263:
                if (hdr.s_size != 524288)
                    skip = true;
                break;
            case 0x0268:
                if (hdr.p_size == 5242880)
                    cfg.ms5p = 1;
                if (hdr.p_size != 8388608)
                    skip = true;
                break;
            case 0x0269:
                if (hdr.p_size != 8388608)
                    skip = true;
                if (hdr.name[15] == 'S') {
                    cfg.cmc = 0;
                    cfg.pvc = 1;
                    skip = true;
                }
                break;
            case 0x0271:
                if (hdr.p_size != 9437184)
                    skip = true;
                break;
            default:
                break;
        }
        if (skip)
            break;

        cfg.hw_type = q.hw;
        cfg.cmc = q.cmc;
        cfg.sma = q.sma;
        cfg.pvc = q.pvc;
        cfg.xram = q.xram;
        cfg.vrom_mirror = !q.mir;
        cfg.rom_wait = q.rwait;
        cfg.p_wait = q.pwait;
        break;
    }
    return cfg;
}

}
