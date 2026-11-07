// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/snes_rom.h"

#include <algorithm>

#include "cores/snes_cart_type.h"
#include "infra/byte_view.h"

namespace mister::cores::snes {
namespace {

struct ResetOpWeight {
    std::uint8_t opcode;
    HeaderScore weight;
};

inline constexpr ResetOpWeight kResetOpWeight[] = {
    {0x78, 8},  {0x18, 8},  {0x38, 8},  {0x9c, 8},  {0x4c, 8},  {0x5c, 8},  {0xc2, 4},
    {0xe2, 4},  {0xad, 4},  {0xae, 4},  {0xac, 4},  {0xaf, 4},  {0xa9, 4},  {0xa2, 4},
    {0xa0, 4},  {0x20, 4},  {0x22, 4},  {0x40, -4}, {0x60, -4}, {0x6b, -4}, {0xcd, -4},
    {0xec, -4}, {0xcc, -4}, {0x00, -8}, {0x02, -8}, {0xdb, -8}, {0x42, -8}, {0xff, -8},
};

struct MapperAffinity {
    HeaderAddr addr;
    std::uint8_t mapper;
    HeaderScore bonus;
};

inline constexpr MapperAffinity kMapperAffinity[] = {
    {kLoRomHeader, 0x20, {2}},
    {kHiRomHeader, 0x21, {2}},
    {kLoRomHeader, 0x22, {2}},
    {kExHiRomHeader, 0x25, {2}},
};

inline constexpr std::uint8_t kExtendedCompany = 0x33;
inline constexpr std::uint8_t kScoreRomTypeCeiling = 0x08;
inline constexpr std::uint8_t kScoreRomSizeCeiling = 0x10;
inline constexpr std::uint8_t kScoreRamSizeCeiling = 0x08;
inline constexpr std::uint8_t kScoreRegionCeiling = 14;

inline constexpr HeaderScore kChecksumBonus{4};
inline constexpr HeaderScore kCompanyBonus{2};
inline constexpr HeaderScore kFieldBonus{1};

inline constexpr HeaderScore kExHiRomFavor{4};

[[nodiscard]] constexpr HeaderScore weight_of(std::uint8_t opcode) noexcept {
    for (const ResetOpWeight& row : kResetOpWeight)
        if (row.opcode == opcode)
            return row.weight;
    return HeaderScore{};
}

[[nodiscard]] constexpr HeaderScore affinity_of(HeaderAddr addr, std::uint8_t mapper) noexcept {
    for (const MapperAffinity& row : kMapperAffinity)
        if (row.addr == addr && row.mapper == mapper)
            return row.bonus;
    return HeaderScore{};
}

[[nodiscard]] bool contains_at(
    std::span<const std::uint8_t> image,
    HeaderAddr at,
    std::string_view want
) noexcept {
    const auto magic = infra::text_bytes<const std::uint8_t>(want);
    return std::ranges::equal(at.slice(image, kImageStart, HeaderLen::of(magic)), magic);
}

struct HeaderScores {
    HeaderScore lo;
    HeaderScore hi;
    HeaderScore ex;
};

[[nodiscard]] HeaderAddr pick_header(HeaderScores scores) noexcept {
    const HeaderScore lo = scores.lo;
    const HeaderScore hi = scores.hi;
    HeaderScore ex = scores.ex;
    if (ex.scored())
        ex += kExHiRomFavor;

    const auto won = [](HeaderScore score, HeaderAddr at) {
        if (!score.scored())
            return kNoHeader;
        return at;
    };
    if (lo >= hi && lo >= ex)
        return won(lo, kLoRomHeader);
    if (hi >= ex)
        return won(hi, kHiRomHeader);
    return won(ex, kExHiRomHeader);
}

}

std::optional<HeaderFields> HeaderFields::of(std::span<const std::uint8_t> window) noexcept {
    if (HeaderLen::of(window) < kHeaderWindow)
        return std::nullopt;
    return HeaderFields{window};
}

ResetVector HeaderFields::reset_vector() const noexcept {
    return ResetVector{infra::load_le<std::uint16_t>(kResetVector.past(window_).first<2>())};
}

bool HeaderFields::resets_into_rom() const noexcept {
    return reset_vector() >= kResetVectorFloor;
}

bool HeaderFields::checksum_valid() const noexcept {
    const auto checksum = infra::load_le<std::uint16_t>(kChecksum.past(window_).first<2>());
    const auto complement = infra::load_le<std::uint16_t>(kComplement.past(window_).first<2>());
    return static_cast<std::uint16_t>(checksum + complement) == kChecksumTotal
        && checksum != 0
        && complement != 0;
}

std::uint8_t HeaderFields::mapper_class() const noexcept {
    std::uint8_t out = kMapper.past(window_).front();
    out &= kMapperClassMask;
    return out;
}

bool HeaderFields::extended_company() const noexcept {
    return kCompany.past(window_).front() == kExtendedCompany;
}

bool HeaderFields::rom_type_plausible() const noexcept {
    return kRomType.past(window_).front() < kScoreRomTypeCeiling;
}

bool HeaderFields::rom_size_plausible() const noexcept {
    return kRomSize.past(window_).front() < kScoreRomSizeCeiling;
}

bool HeaderFields::ram_size_plausible() const noexcept {
    return kRamSize.past(window_).front() < kScoreRamSizeCeiling;
}

bool HeaderFields::region_plausible() const noexcept {
    return kRegion.past(window_).front() < kScoreRegionCeiling;
}

namespace {

[[nodiscard]] HeaderScore score_fields(
    const HeaderFields& fields,
    std::uint8_t resetop,
    HeaderAddr addr
) noexcept {
    HeaderScore score = weight_of(resetop);

    if (fields.checksum_valid())
        score += kChecksumBonus;

    score += affinity_of(addr, fields.mapper_class());

    if (fields.extended_company())
        score += kCompanyBonus;
    if (fields.rom_type_plausible())
        score += kFieldBonus;
    if (fields.rom_size_plausible())
        score += kFieldBonus;
    if (fields.ram_size_plausible())
        score += kFieldBonus;
    if (fields.region_plausible())
        score += kFieldBonus;

    return score.clamped();
}

}

HeaderScore score_header(std::span<const std::uint8_t> image, HeaderAddr addr) noexcept {
    const std::optional<HeaderFields> fields = HeaderFields::of(
        addr.slice(image, kImageStart, kHeaderWindow)
    );

    if (!fields || !fields->resets_into_rom())
        return kRefused;

    const auto resetop = addr.reset_op(fields->reset_vector()).slice(image, kImageStart, kOneByte);
    if (resetop.empty())
        return kRefused;
    return score_fields(*fields, resetop.front(), addr);
}

SufamiScan sufami_walk(std::span<const std::uint8_t> payload) noexcept {
    SufamiScan out{};
    if (!contains_at(payload, kImageStart, kSufamiMagic))
        return out;
    const HeaderLen size = HeaderLen::of(payload);
    for (HeaderAddr at = kImageStart; at - kImageStart < size; at += kSufamiStride) {
        if (contains_at(payload, at, kSufamiMagic)) {
            if (contains_at(payload, at + kSufamiBackupOffset, kSufamiBackupMagic)) {
                out.bios = true;
            } else {
                if (!out.base)
                    out.addr = at;
                out.turbo = out.base;
                out.base = true;
            }
        }

        if (size - (at - kImageStart) <= kSufamiStride)
            break;
    }
    return out;
}

HeaderAddr find_header(std::span<const std::uint8_t> image) noexcept {
    return pick_header(
        {score_header(image, kLoRomHeader),
         score_header(image, kHiRomHeader),
         score_header(image, kExHiRomHeader)}
    );
}

std::uint32_t next_pow2(std::uint32_t v) noexcept {
    if (!v)
        return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

std::uint32_t snes_mirror(std::uint32_t addr, std::uint32_t size) noexcept {
    if (!size)
        return 0;
    std::uint32_t base = 0;
    std::uint32_t mask = 1u;
    while (mask < size)
        mask <<= 1;
    while (addr >= size) {
        while (mask && !(addr & mask))
            mask >>= 1;
        if (!mask)
            return addr % size;
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
    if (payload == 0)
        return 0;
    return next_pow2(payload);
}

MirrorRun mirror_run(std::uint32_t pos, std::uint32_t payload, std::uint32_t padded) noexcept {
    const std::uint32_t src = snes_mirror(pos, payload);
    std::uint32_t len = payload - src;
    if (len > padded - pos)
        len = padded - pos;
    return {src, len};
}

const std::array<std::uint8_t, 32> kCc92Signature{0x00, 0x08, 0x22, 0x02, 0x1C, 0x00, 0x10, 0x00,
                                                  0x08, 0x65, 0x80, 0x84, 0x20, 0x00, 0x22, 0x25,
                                                  0x00, 0x83, 0x0C, 0x80, 0x10, 0x00, 0x00, 0xA0,
                                                  0x80, 0x01, 0x80, 0x80, 0x00, 0x01, 0x02, 0x2D};

const std::array<std::uint8_t, 32> kPf94TenKSignature{
    0xC9, 0x80, 0x80, 0x44, 0x15, 0x00, 0x62, 0x09, 0x29, 0xA0, 0x52, 0x70, 0x50, 0x12, 0x05, 0x35,
    0x31, 0x63, 0xC0, 0x22, 0x01, 0x80, 0xC2, 0x3A, 0x6C, 0xB0, 0xE8, 0x4A, 0x11, 0x20, 0xC0, 0xF8
};

const std::array<std::uint8_t, 64> kPf94OneMSignature{
    0x50, 0x52, 0x45, 0x48, 0x49, 0x53, 0x54, 0x4F, 0x52, 0x49, 0x4B, 0x20, 0x4D, 0x41, 0x4E, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x00, 0x0A, 0x00, 0x01, 0x33, 0x00, 0xFF, 0xFF, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0x2B, 0x80, 0x2B, 0x80, 0x2B, 0x80, 0xFE, 0x91, 0x2B, 0x80, 0xA4, 0xF7,
    0xFF, 0xFF, 0xFF, 0xFF, 0x2B, 0x80, 0x2B, 0x80, 0x2B, 0x80, 0x75, 0xF7, 0x00, 0x80, 0xA4, 0xF7
};

namespace {

inline constexpr std::uint8_t kRamSizeCodeMax = 0x09;
inline constexpr std::uint8_t kRomSizeCodeTop = 15;
inline constexpr std::uint8_t kBsxBiosRomType = 0x30;
inline constexpr std::uint8_t kGsuRamSentinel = 0xFF;
inline constexpr std::uint8_t kGsuRamDefault = 5;
inline constexpr std::uint8_t kGsuRamMax = 6;
inline constexpr std::uint8_t kSt011RomSizeCeiling = 10;
inline constexpr std::uint8_t kSdd1RomSizeCeiling = 14;

inline constexpr HeaderLen kGameCodeZ{14};
inline constexpr HeaderLen kGameCodeSeries{13};
inline constexpr HeaderLen kGameCodeJ{11};
inline constexpr HeaderLen kReservedFirst{10};
inline constexpr HeaderLen kReservedLast{4};
inline constexpr HeaderLen kExpansionRamSize{3};
inline constexpr HeaderLen kSufamiRomSize{0x36};
inline constexpr HeaderLen kSufamiRamSize{0x37};

}

std::uint8_t ImageWindow::after(HeaderAddr addr, HeaderLen ahead) const noexcept {
    const auto byte = (addr + ahead).slice(bytes, base, kOneByte);
    return byte.empty() ? std::uint8_t{0} : byte.front();
}

std::uint8_t ImageWindow::before(HeaderAddr addr, HeaderLen back) const noexcept {
    return addr - kImageStart >= back ? after(addr - back, HeaderLen{}) : std::uint8_t{0};
}

ImageWindow typing_window(std::span<const std::uint8_t> image, HeaderAddr addr) noexcept {
    const HeaderAddr base = addr - kImageStart >= kTypingWindowBack ? addr - kTypingWindowBack
                                                                    : kImageStart;
    return ImageWindow{base.slice(image, kImageStart, kTypingWindow), base};
}

TypingBytes type_cart(
    const ImageWindow& window,
    HeaderAddr addr,
    std::uint32_t payload,
    const CartSniff& sniff
) noexcept {
    const auto field = [&](HeaderLen ahead) {
        return window.after(addr, ahead);
    };
    const auto before = [&](HeaderLen back) {
        return window.before(addr, back);
    };

    std::uint8_t ramsz = field(HeaderFields::kRamSize);
    if (ramsz >= kRamSizeCodeMax)
        ramsz = 0;

    std::uint8_t romsz = kRomSizeCodeTop;
    std::uint32_t sz = payload - 1;
    if (!(sz & 0xFF000000u)) {
        while (sz != 0 && !(sz & 0x1000000u)) {
            romsz = static_cast<std::uint8_t>(romsz - 1);
            sz <<= 1;
        }
    }

    bool has_bsx_slot = false;
    const std::uint8_t series = before(kGameCodeSeries);
    if (before(kGameCodeZ) == 'Z'
        && before(kGameCodeJ) == 'J'
        && ((series >= 'A' && series <= 'Z') || (series >= '0' && series <= '9'))
        && (field(HeaderFields::kCompany) == kExtendedCompany
            || (before(kReservedFirst) == 0x00 && before(kReservedLast) == 0x00))) {
        has_bsx_slot = true;
    }

    CartRomType rom_type = CartRomType::from(CartRomType::kLoRom);
    if (addr == kHiRomHeader) {
        rom_type = CartRomType::from(CartRomType::kHiRom);
    } else if (addr == kExHiRomHeader) {
        rom_type = CartRomType::from(CartRomType::kExHiRom);
    } else if (has_bsx_slot) {
        rom_type = CartRomType::from(CartRomType::kSpecialLoRom);
    }

    if (sniff.bsx_bios) {
        rom_type = CartRomType::from(kBsxBiosRomType);
    } else if (sniff.sufami_base) {
        rom_type = sufami_type(sniff.sufami_turbo, sniff.sufami_bios);
        constexpr std::uint8_t rom_sz_tbl[9] = {0, 7, 8, 9, 9, 10, 10, 10, 10};
        constexpr std::uint8_t ram_sz_tbl[5] = {0, 1, 2, 3, 3};
        const std::uint8_t b36 = field(kSufamiRomSize);
        const std::uint8_t b37 = field(kSufamiRamSize);
        romsz = b36 >= 8 ? rom_sz_tbl[8] : rom_sz_tbl[b36 & 0x0F];
        ramsz = b37 >= 4 ? ram_sz_tbl[4] : ram_sz_tbl[b37 & 0x07];
    } else if (sniff.cc92) {

        rom_type = CartRomType::from(CartRomType::kLoRom).with(CartChip::Cc92);
    } else if (sniff.pf94) {

        rom_type = CartRomType::from(CartRomType::kLoRom).with(CartChip::Pf94);
    } else {

        CartChip chip = chip_for(
            field(HeaderFields::kMapper),
            field(HeaderFields::kRomType),
            field(HeaderFields::kCompany)
        );

        if (chip == CartChip::St010 && field(HeaderFields::kRomSize) < kSt011RomSizeCeiling) {
            chip = CartChip::St011;
        }

        if (chip == CartChip::Sdd1 && romsz >= kSdd1RomSizeCeiling)
            chip = CartChip::None;
        rom_type = rom_type.with(chip);
        if (chip == CartChip::Gsu) {
            std::uint8_t gsu_ram = before(kExpansionRamSize);
            if (gsu_ram == kGsuRamSentinel)
                gsu_ram = kGsuRamDefault;
            if (gsu_ram > kGsuRamMax)
                gsu_ram = kGsuRamMax;
            rom_type = rom_type.with_ram(gsu_ram);
        }
    }

    if (rom_type.forces_ram())
        ramsz = rom_type.ram;

    std::uint8_t h3 = 0;
    const std::uint8_t region = field(HeaderFields::kRegion);
    if (((region >= 0x02 && region <= 0x0C) || region == 0x11)
        && !sniff.sufami_base
        && !sniff.cc92
        && !sniff.pf94) {
        h3 = 1;
    }

    TypingBytes out;
    out.size_field = static_cast<std::uint8_t>((static_cast<unsigned>(ramsz) << 4) | romsz);
    out.rom_type = rom_type.v;
    out.reserved = 0;
    out.region = h3;
    return out;
}

std::array<std::uint8_t, kHeaderBlockBytes> header_block(
    HeaderAddr addr,
    std::uint32_t payload,
    const TypingBytes* typing
) noexcept {
    std::array<std::uint8_t, kHeaderBlockBytes> hdr{};
    if (typing != nullptr) {
        hdr[0] = typing->size_field;
        hdr[1] = typing->rom_type;
        hdr[2] = typing->reserved;
        hdr[3] = typing->region;
    }
    addr.store_le32(std::span(hdr).subspan<4, sizeof(std::uint32_t)>());
    infra::store_le<std::uint32_t>(std::span(hdr).subspan<8, sizeof(std::uint32_t)>(), payload);
    return hdr;
}

}
