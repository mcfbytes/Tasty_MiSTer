// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neo_header.h"
#include "cores/neo_quirk.h"

#include "infra/byte_view.h"
#include "infra/endian.h"

namespace mister::cores::neogeo {

namespace {

std::uint32_t get_u32(std::span<const std::uint8_t> b, std::size_t off) noexcept {
    return infra::load_le<std::uint32_t>(b.subspan(off).first<sizeof(std::uint32_t)>());
}

}

std::optional<NeoHeader> parse_neo_header(std::span<const std::uint8_t> buf) noexcept {

    constexpr std::size_t kVersionAt = 3;
    constexpr std::size_t kPSizeAt = 4;
    constexpr std::size_t kSSizeAt = 8;
    constexpr std::size_t kMSizeAt = 12;
    constexpr std::size_t kV1SizeAt = 16;
    constexpr std::size_t kV2SizeAt = 20;
    constexpr std::size_t kCSizeAt = 24;
    constexpr std::size_t kYearAt = 28;
    constexpr std::size_t kGenreAt = 32;
    constexpr std::size_t kScreenshotAt = 36;
    constexpr std::size_t kNghAt = 40;
    constexpr std::size_t kNameAt = 44;
    constexpr std::size_t kManuAt = 77;
    if (buf.size() < kNeoHeaderBytes)
        return std::nullopt;

    NeoHeader h{};
    h.magic = {buf[0], buf[1], buf[2]};
    h.version = buf[kVersionAt];
    h.p_size = get_u32(buf, kPSizeAt);
    h.s_size = get_u32(buf, kSSizeAt);
    h.m_size = get_u32(buf, kMSizeAt);
    h.v1_size = get_u32(buf, kV1SizeAt);
    h.v2_size = get_u32(buf, kV2SizeAt);
    h.c_size = get_u32(buf, kCSizeAt);
    h.year = get_u32(buf, kYearAt);
    h.genre = get_u32(buf, kGenreAt);
    h.screenshot = get_u32(buf, kScreenshotAt);
    h.ngh = get_u32(buf, kNghAt);
    for (std::size_t i = 0; i < h.name.size(); ++i)
        h.name[i] = buf[kNameAt + i];
    for (std::size_t i = 0; i < h.manu.size(); ++i)
        h.manu[i] = buf[kManuAt + i];
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

CartQuirks resolve_quirks(const NeoHeader& hdr) noexcept {

    constexpr std::uint32_t kNghKof99 = 0x0251;
    constexpr std::uint32_t kNghGarou = 0x0253;
    constexpr std::uint32_t kNghMslug3 = 0x0256;
    constexpr std::uint32_t kNghMslug4 = 0x0263;
    constexpr std::uint32_t kNghMslug5 = 0x0268;
    constexpr std::uint32_t kNghSvc = 0x0269;
    constexpr std::uint32_t kNghKof2003 = 0x0271;
    constexpr std::uint32_t kFinalReleasePromBytes = 9437184;
    constexpr std::uint32_t kSize8M = 8388608;
    constexpr std::uint32_t kSize5M = 5242880;
    constexpr std::uint32_t kSize512K = 524288;
    constexpr std::size_t kGarouhNameAt = 27;
    constexpr std::size_t kSvcsplusNameAt = 15;
    constexpr std::uint8_t kSmaGarouh = 3;
    CartQuirks cfg{};

    for (const NeoQuirk& q : kNeoQuirks) {
        if (q.id != hdr.ngh)
            continue;

        bool skip = false;
        switch (hdr.ngh) {
            case kNghKof99:
                if (hdr.p_size != kFinalReleasePromBytes)
                    skip = true;
                break;
            case kNghGarou:
                if (hdr.p_size != kFinalReleasePromBytes)
                    skip = true;
                if (hdr.name[kGarouhNameAt] == 'A') {
                    cfg.config.sma = kSmaGarouh;
                    cfg.config.cmc = 1;
                    cfg.vrom_mirror = false;
                    skip = true;
                }
                break;
            case kNghMslug3:
                if (hdr.p_size != kFinalReleasePromBytes)
                    skip = true;
                break;
            case kNghMslug4:
                if (hdr.s_size != kSize512K)
                    skip = true;
                break;
            case kNghMslug5:
                if (hdr.p_size == kSize5M)
                    cfg.config.ms5p = 1;
                if (hdr.p_size != kSize8M)
                    skip = true;
                break;
            case kNghSvc:
                if (hdr.p_size != kSize8M)
                    skip = true;
                if (hdr.name[kSvcsplusNameAt] == 'S') {
                    cfg.config.cmc = 0;
                    cfg.config.pvc = 1;
                    skip = true;
                }
                break;
            case kNghKof2003:
                if (hdr.p_size != kFinalReleasePromBytes)
                    skip = true;
                break;
            default:
                break;
        }
        if (skip)
            break;

        cfg.config.hw_type = q.hw;
        cfg.config.cmc = q.cmc;
        cfg.config.sma = q.sma;
        cfg.config.pvc = q.pvc;
        cfg.config.xram = q.xram;
        cfg.vrom_mirror = !q.mir;
        cfg.config.rom_wait = q.rwait;
        cfg.config.p_wait = q.pwait;
        break;
    }
    return cfg;
}

}
