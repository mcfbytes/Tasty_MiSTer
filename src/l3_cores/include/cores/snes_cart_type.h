// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::cores::snes {

enum class CartChip : std::uint8_t {
    None = 0,
    Dsp1,
    Dsp1b,
    Dsp2,
    Dsp3,
    Dsp4,
    St010,
    St011,
    Obc1,
    Spc7110,
    Spc7110Rtc,
    Srtc,
    Cx4,
    Sdd1,
    Sa1,
    Gsu,
    Cc92,
    Pf94,
};

struct CartChipRow {
    CartChip chip;
    std::uint8_t h1;
    std::uint8_t ram;
};

inline constexpr std::array<CartChipRow, 18> kCartChips = {{
    {CartChip::None, 0x00, 0},
    {CartChip::Dsp1, 0x84, 0},
    {CartChip::Dsp1b, 0x80, 0},
    {CartChip::Dsp2, 0x90, 0},
    {CartChip::Dsp3, 0xA0, 0},
    {CartChip::Dsp4, 0xB0, 0},
    {CartChip::St010, 0x88, 1},
    {CartChip::St011, 0xA8, 1},
    {CartChip::Obc1, 0xC0, 0},
    {CartChip::Spc7110, 0xD0, 0},
    {CartChip::Spc7110Rtc, 0xD8, 0},
    {CartChip::Srtc, 0x08, 0},
    {CartChip::Cx4, 0x40, 0},
    {CartChip::Sdd1, 0x50, 0},
    {CartChip::Sa1, 0x60, 0},
    {CartChip::Gsu, 0x70, 0},
    {CartChip::Cc92, 0xE4, 3},
    {CartChip::Pf94, 0xF4, 3},
}};

struct CartRomType {

    static constexpr std::uint8_t kLoRom = 0;
    static constexpr std::uint8_t kHiRom = 1;
    static constexpr std::uint8_t kExHiRom = 2;
    static constexpr std::uint8_t kSpecialLoRom = 3;

    std::uint8_t v = 0;
    std::uint8_t ram = 0;
    bool ram_forced = false;

    [[nodiscard]] static constexpr CartRomType from(std::uint8_t kind) noexcept {
        return CartRomType{kind, 0, false};
    }

    [[nodiscard]] constexpr CartRomType with(CartChip chip) const noexcept {
        for (const CartChipRow& row : kCartChips)
            if (row.chip == chip)
                return CartRomType{static_cast<std::uint8_t>(v | row.h1), row.ram, row.ram != 0};
        return *this;
    }

    [[nodiscard]] constexpr CartRomType with_ram(std::uint8_t nibble) const noexcept {
        return CartRomType{v, nibble, true};
    }

    [[nodiscard]] constexpr bool forces_ram() const noexcept { return ram_forced; }

    friend constexpr bool operator==(CartRomType, CartRomType) = default;
};

struct CartMapperRow {
    static constexpr std::uint8_t kAnyCompany = 0x00;

    std::uint8_t mapper;
    std::uint8_t rom_type;
    std::uint8_t company;
    CartChip chip;
};

inline constexpr std::array<CartMapperRow, 24> kCartMappers = {{
    {0x20, 0x03, CartMapperRow::kAnyCompany, CartChip::Dsp1},
    {0x21, 0x03, CartMapperRow::kAnyCompany, CartChip::Dsp1b},
    {0x30, 0x05, 0xB2, CartChip::Dsp3},
    {0x30, 0x05, CartMapperRow::kAnyCompany, CartChip::Dsp1b},
    {0x31, 0x03, CartMapperRow::kAnyCompany, CartChip::Dsp1b},
    {0x31, 0x05, CartMapperRow::kAnyCompany, CartChip::Dsp1b},
    {0x20, 0x05, CartMapperRow::kAnyCompany, CartChip::Dsp2},
    {0x30, 0x03, CartMapperRow::kAnyCompany, CartChip::Dsp4},
    {0x30, 0xF6, CartMapperRow::kAnyCompany, CartChip::St010},
    {0x30, 0x25, CartMapperRow::kAnyCompany, CartChip::Obc1},
    {0x3A, 0xF5, CartMapperRow::kAnyCompany, CartChip::Spc7110},
    {0x3A, 0xF9, CartMapperRow::kAnyCompany, CartChip::Spc7110Rtc},
    {0x35, 0x55, CartMapperRow::kAnyCompany, CartChip::Srtc},
    {0x20, 0xF3, CartMapperRow::kAnyCompany, CartChip::Cx4},
    {0x32, 0x43, CartMapperRow::kAnyCompany, CartChip::Sdd1},
    {0x32, 0x45, CartMapperRow::kAnyCompany, CartChip::Sdd1},
    {0x23, 0x32, CartMapperRow::kAnyCompany, CartChip::Sa1},
    {0x23, 0x33, CartMapperRow::kAnyCompany, CartChip::Sa1},
    {0x23, 0x34, CartMapperRow::kAnyCompany, CartChip::Sa1},
    {0x23, 0x35, CartMapperRow::kAnyCompany, CartChip::Sa1},
    {0x20, 0x13, CartMapperRow::kAnyCompany, CartChip::Gsu},
    {0x20, 0x14, CartMapperRow::kAnyCompany, CartChip::Gsu},
    {0x20, 0x15, CartMapperRow::kAnyCompany, CartChip::Gsu},
    {0x20, 0x1A, CartMapperRow::kAnyCompany, CartChip::Gsu},
}};

[[nodiscard]] constexpr CartChip chip_for(
    std::uint8_t mapper,
    std::uint8_t rom_type,
    std::uint8_t company
) noexcept {
    for (const CartMapperRow& row : kCartMappers)
        if (row.mapper == mapper
            && row.rom_type == rom_type
            && (row.company == CartMapperRow::kAnyCompany || row.company == company)) {
            return row.chip;
        }
    return CartChip::None;
}

inline constexpr std::uint8_t kSufamiRomType = 0x20;
inline constexpr std::uint8_t kSufamiTurboBit = 8;
inline constexpr std::uint8_t kSufamiBiosBit = 4;

[[nodiscard]] constexpr CartRomType sufami_type(bool turbo, bool bios) noexcept {
    unsigned code = kSufamiRomType;
    if (turbo)
        code |= kSufamiTurboBit;
    if (bios)
        code |= kSufamiBiosBit;
    return CartRomType::from(static_cast<std::uint8_t>(code));
}

}
