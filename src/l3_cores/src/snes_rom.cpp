// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_rom.h"

namespace mister::cores::snes {
namespace {

enum HeaderField : std::uint32_t {
    Mapper = 0x15,
    RomType = 0x16,
    RomSize = 0x17,
    RamSize = 0x18,
    CartRegion = 0x19,
    Company = 0x1a,
    Complement = 0x1c,
    Checksum = 0x1e,
    ResetVector = 0x3c,
};

}

std::uint16_t window_reset_vector(std::span<const std::uint8_t> hdr) noexcept {
    if (hdr.size() < kHeaderWindowBytes) return 0;
    return static_cast<std::uint16_t>(hdr[ResetVector] | (hdr[ResetVector + 1] << 8));
}

std::uint32_t score_window(std::span<const std::uint8_t> hdr, std::uint8_t resetop,
                           std::uint32_t addr) noexcept {
    if (hdr.size() < kHeaderWindowBytes) return 0;
    int score = 0;

    const auto resetvector =
        static_cast<std::uint16_t>(hdr[ResetVector] | (hdr[ResetVector + 1] << 8));
    const auto checksum = static_cast<std::uint16_t>(hdr[Checksum] | (hdr[Checksum + 1] << 8));
    const auto complement =
        static_cast<std::uint16_t>(hdr[Complement] | (hdr[Complement + 1] << 8));

    const auto mapper = static_cast<std::uint8_t>(hdr[Mapper] & ~0x10);

    if (resetvector < 0x8000) return 0;

    if (resetop == 0x78 || resetop == 0x18 || resetop == 0x38 || resetop == 0x9c ||
        resetop == 0x4c || resetop == 0x5c) {
        score += 8;
    }
    if (resetop == 0xc2 || resetop == 0xe2 || resetop == 0xad || resetop == 0xae ||
        resetop == 0xac || resetop == 0xaf || resetop == 0xa9 || resetop == 0xa2 ||
        resetop == 0xa0 || resetop == 0x20 || resetop == 0x22) {
        score += 4;
    }
    if (resetop == 0x40 || resetop == 0x60 || resetop == 0x6b || resetop == 0xcd ||
        resetop == 0xec || resetop == 0xcc) {
        score -= 4;
    }
    if (resetop == 0x00 || resetop == 0x02 || resetop == 0xdb || resetop == 0x42 ||
        resetop == 0xff) {
        score -= 8;
    }

    if ((checksum + complement) == 0xffff && (checksum != 0) && (complement != 0)) score += 4;

    if (addr == kLoRomHeader && mapper == 0x20) score += 2;
    if (addr == kHiRomHeader && mapper == 0x21) score += 2;
    if (addr == kLoRomHeader && mapper == 0x22) score += 2;
    if (addr == kExHiRomHeader && mapper == 0x25) score += 2;

    if (hdr[Company] == 0x33) score += 2;
    if (hdr[RomType] < 0x08) score++;
    if (hdr[RomSize] < 0x10) score++;
    if (hdr[RamSize] < 0x08) score++;
    if (hdr[CartRegion] < 14) score++;

    if (score < 0) score = 0;
    return static_cast<std::uint32_t>(score);
}

std::uint32_t score_header(std::span<const std::uint8_t> image, std::uint32_t addr) noexcept {
    if (image.size() < static_cast<std::uint64_t>(addr) + kHeaderWindowBytes) return 0;
    const auto win = image.subspan(addr, kHeaderWindowBytes);
    const std::uint16_t resetvector = window_reset_vector(win);
    if (resetvector < 0x8000) return 0;

    const std::uint32_t resetop_at = resetop_addr(addr, resetvector);
    if (resetop_at >= image.size()) return 0;
    return score_window(win, image[resetop_at], addr);
}

std::uint32_t pick_header(std::uint32_t score_lo, std::uint32_t score_hi,
                          std::uint32_t score_ex) noexcept {
    if (score_ex) score_ex += 4;

    if (score_lo >= score_hi && score_lo >= score_ex) {
        return score_lo ? kLoRomHeader : kNoHeader;
    }
    if (score_hi >= score_ex) {
        return score_hi ? kHiRomHeader : kNoHeader;
    }
    return score_ex ? kExHiRomHeader : kNoHeader;
}

std::uint32_t find_header(std::span<const std::uint8_t> image) noexcept {
    return pick_header(score_header(image, kLoRomHeader), score_header(image, kHiRomHeader),
                       score_header(image, kExHiRomHeader));
}

std::uint32_t next_pow2(std::uint32_t v) noexcept {
    if (!v) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

std::uint32_t snes_mirror(std::uint32_t addr, std::uint32_t size) noexcept {
    if (!size) return 0;
    std::uint32_t base = 0;
    std::uint32_t mask = 1u;
    while (mask < size)
        mask <<= 1;
    while (addr >= size) {
        while (mask && !(addr & mask))
            mask >>= 1;
        if (!mask) return addr % size;
        addr -= mask;
        if (size > mask) {
            size -= mask;
            base += mask;
        }
        mask >>= 1;
    }
    return base + addr;
}

std::uint32_t mirrored_size(std::uint32_t payload) noexcept {
    if (payload == 0) return 0;
    return next_pow2(payload);
}

MirrorRun mirror_run(std::uint32_t pos, std::uint32_t payload, std::uint32_t padded) noexcept {
    const std::uint32_t src = snes_mirror(pos, payload);
    std::uint32_t len = payload - src;
    if (len > padded - pos) len = padded - pos;
    return {src, len};
}

const std::array<std::uint8_t, 32> kCc92Signature{
    0x00, 0x08, 0x22, 0x02, 0x1C, 0x00, 0x10, 0x00, 0x08, 0x65, 0x80, 0x84, 0x20, 0x00, 0x22, 0x25,
    0x00, 0x83, 0x0C, 0x80, 0x10, 0x00, 0x00, 0xA0, 0x80, 0x01, 0x80, 0x80, 0x00, 0x01, 0x02, 0x2D};

const std::array<std::uint8_t, 32> kPf94TenKSignature{
    0xC9, 0x80, 0x80, 0x44, 0x15, 0x00, 0x62, 0x09, 0x29, 0xA0, 0x52, 0x70, 0x50, 0x12, 0x05, 0x35,
    0x31, 0x63, 0xC0, 0x22, 0x01, 0x80, 0xC2, 0x3A, 0x6C, 0xB0, 0xE8, 0x4A, 0x11, 0x20, 0xC0, 0xF8};

const std::array<std::uint8_t, 64> kPf94OneMSignature{
    0x50, 0x52, 0x45, 0x48, 0x49, 0x53, 0x54, 0x4F, 0x52, 0x49, 0x4B, 0x20, 0x4D, 0x41, 0x4E, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x00, 0x0A, 0x00, 0x01, 0x33, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x2B, 0x80, 0x2B, 0x80, 0x2B, 0x80, 0xFE, 0x91, 0x2B, 0x80, 0xA4, 0xF7,
    0xFF, 0xFF, 0xFF, 0xFF, 0x2B, 0x80, 0x2B, 0x80, 0x2B, 0x80, 0x75, 0xF7, 0x00, 0x80, 0xA4, 0xF7};

TypingBytes type_cart(std::span<const std::uint8_t> window, std::uint32_t window_base,
                      std::uint32_t addr, std::uint32_t payload, const CartSniff& sniff) noexcept {

    const auto at = [&](std::int64_t rel) -> std::uint8_t {
        const std::int64_t abs = static_cast<std::int64_t>(addr) + rel;
        if (abs < static_cast<std::int64_t>(window_base)) return 0;
        const auto idx = static_cast<std::uint64_t>(abs) - window_base;
        if (idx >= window.size()) return 0;
        return window[static_cast<std::size_t>(idx)];
    };

    std::uint8_t ramsz = at(RamSize);
    if (ramsz >= 0x09) ramsz = 0;

    std::uint8_t romsz = 15;
    std::uint32_t sz = payload - 1;
    if (!(sz & 0xFF000000u)) {
        while (sz != 0 && !(sz & 0x1000000u)) {
            romsz = static_cast<std::uint8_t>(romsz - 1);
            sz <<= 1;
        }
    }

    bool has_bsx_slot = false;
    const std::uint8_t g13 = at(-13);
    if (at(-14) == 'Z' && at(-11) == 'J' &&
        ((g13 >= 'A' && g13 <= 'Z') || (g13 >= '0' && g13 <= '9')) &&
        (at(Company) == 0x33 || (at(-10) == 0x00 && at(-4) == 0x00))) {
        has_bsx_slot = true;
    }

    unsigned h1 = (addr == kHiRomHeader)     ? 1u
                  : (addr == kExHiRomHeader) ? 2u
                  : has_bsx_slot             ? 3u
                                             : 0u;

    if (sniff.bsx_bios) {
        h1 = 0x30;
    } else if (sniff.sufami_base) {
        h1 = 0x20u | (sniff.sufami_turbo ? 8u : 0u) | (sniff.sufami_bios ? 4u : 0u);
        constexpr std::uint8_t rom_sz_tbl[9] = {0, 7, 8, 9, 9, 10, 10, 10, 10};
        constexpr std::uint8_t ram_sz_tbl[5] = {0, 1, 2, 3, 3};
        const std::uint8_t b36 = at(0x36);
        const std::uint8_t b37 = at(0x37);
        romsz = b36 >= 8 ? rom_sz_tbl[8] : rom_sz_tbl[b36 & 0x0F];
        ramsz = b37 >= 4 ? ram_sz_tbl[4] : ram_sz_tbl[b37 & 0x07];
    } else if (sniff.cc92) {
        h1 = 0xE4;
        ramsz = 3;
    } else if (sniff.pf94) {
        h1 = 0xF4;
        ramsz = 3;
    } else {
        const std::uint8_t mapper = at(Mapper);
        const std::uint8_t rom_type = at(RomType);

        if (mapper == 0x20 && rom_type == 0x03) {
            h1 |= 0x84;
        } else if (mapper == 0x21 && rom_type == 0x03) {
            h1 |= 0x80;
        } else if (mapper == 0x30 && rom_type == 0x05 && at(Company) != 0xb2) {
            h1 |= 0x80;
        } else if (mapper == 0x31 && (rom_type == 0x03 || rom_type == 0x05)) {
            h1 |= 0x80;
        } else if (mapper == 0x20 && rom_type == 0x05) {
            h1 |= 0x90;
        } else if (mapper == 0x30 && rom_type == 0x05 && at(Company) == 0xb2) {
            h1 |= 0xA0;
        } else if (mapper == 0x30 && rom_type == 0x03) {
            h1 |= 0xB0;
        } else if (mapper == 0x30 && rom_type == 0xf6) {
            h1 |= 0x88;
            ramsz = 1;
            if (at(RomSize) < 10) h1 |= 0x20;
        } else if (mapper == 0x30 && rom_type == 0x25) {
            h1 |= 0xC0;
        }

        if (mapper == 0x3a && (rom_type == 0xf5 || rom_type == 0xf9)) {
            h1 |= 0xD0;
            if (rom_type == 0xf9) h1 |= 0x08;
        }
        if (mapper == 0x35 && rom_type == 0x55) h1 |= 0x08;
        if (mapper == 0x20 && rom_type == 0xf3) h1 |= 0x40;
        if (mapper == 0x32 && (rom_type == 0x43 || rom_type == 0x45)) {
            if (romsz < 14) h1 |= 0x50;
        }
        if (mapper == 0x23 &&
            (rom_type == 0x32 || rom_type == 0x33 || rom_type == 0x34 || rom_type == 0x35)) {
            h1 |= 0x60;
        }
        if (mapper == 0x20 &&
            (rom_type == 0x13 || rom_type == 0x14 || rom_type == 0x15 || rom_type == 0x1a)) {
            ramsz = at(-3);
            if (ramsz == 0xFF) ramsz = 5;
            if (ramsz > 6) ramsz = 6;
            h1 |= 0x70;
        }
    }

    unsigned h3 = 0;
    const std::uint8_t region = at(CartRegion);
    if (((region >= 0x02 && region <= 0x0C) || region == 0x11) && !sniff.sufami_base &&
        !sniff.cc92 && !sniff.pf94) {
        h3 |= 1;
    }

    TypingBytes out;
    out.size_field = static_cast<std::uint8_t>((static_cast<unsigned>(ramsz) << 4) | romsz);
    out.rom_type = static_cast<std::uint8_t>(h1);
    out.reserved = 0;
    out.region = static_cast<std::uint8_t>(h3);
    return out;
}

std::array<std::uint8_t, kHeaderBlockBytes> header_block(std::uint32_t addr, std::uint32_t payload,
                                                         const TypingBytes* typing) noexcept {
    std::array<std::uint8_t, kHeaderBlockBytes> hdr{};
    if (typing != nullptr) {
        hdr[0] = typing->size_field;
        hdr[1] = typing->rom_type;
        hdr[2] = typing->reserved;
        hdr[3] = typing->region;
    }
    hdr[4] = static_cast<std::uint8_t>(addr & 0xFF);
    hdr[5] = static_cast<std::uint8_t>((addr >> 8) & 0xFF);
    hdr[6] = static_cast<std::uint8_t>((addr >> 16) & 0xFF);
    hdr[7] = static_cast<std::uint8_t>((addr >> 24) & 0xFF);
    hdr[8] = static_cast<std::uint8_t>(payload & 0xFF);
    hdr[9] = static_cast<std::uint8_t>((payload >> 8) & 0xFF);
    hdr[10] = static_cast<std::uint8_t>((payload >> 16) & 0xFF);
    hdr[11] = static_cast<std::uint8_t>((payload >> 24) & 0xFF);
    return hdr;
}

}  // namespace mister::cores::snes
