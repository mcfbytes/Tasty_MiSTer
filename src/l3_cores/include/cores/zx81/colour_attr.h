// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "cores/stock_scan.h"

namespace mister::cores::zx81 {

struct ColourAttr {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr ColourAttr with_ink(std::string_view decimal) const noexcept {
        constexpr unsigned kKeep = 0xF8;
        return ColourAttr{static_cast<std::uint8_t>((v & kKeep) | colour_bits(decimal))};
    }

    [[nodiscard]] constexpr ColourAttr with_ink_bright(std::string_view decimal) const noexcept {
        constexpr unsigned kKeep = 0xF7;
        constexpr unsigned kShift = 3;
        return ColourAttr{static_cast<std::uint8_t>((v & kKeep) | (bright_bit(decimal) << kShift))};
    }

    [[nodiscard]] constexpr ColourAttr with_paper(std::string_view decimal) const noexcept {
        constexpr unsigned kKeep = 0x8F;
        constexpr unsigned kShift = 4;
        return ColourAttr{
            static_cast<std::uint8_t>((v & kKeep) | (colour_bits(decimal) << kShift))
        };
    }

    [[nodiscard]] constexpr ColourAttr with_paper_bright(std::string_view decimal) const noexcept {
        constexpr unsigned kKeep = 0x7F;
        constexpr unsigned kShift = 7;
        return ColourAttr{static_cast<std::uint8_t>((v & kKeep) | (bright_bit(decimal) << kShift))};
    }

    friend constexpr bool operator==(ColourAttr, ColourAttr) = default;

private:
    [[nodiscard]] static constexpr unsigned colour_bits(std::string_view decimal) noexcept {
        constexpr unsigned kColour = 7;
        return static_cast<unsigned>(stock_scan::scan_atoi(decimal)) & kColour;
    }

    [[nodiscard]] static constexpr unsigned bright_bit(std::string_view decimal) noexcept {
        return static_cast<unsigned>(stock_scan::scan_atoi(decimal)) & 1u;
    }
};

static_assert(
    ColourAttr{}.with_ink("5").with_ink_bright("1") == ColourAttr{0x0D}
        && ColourAttr{}.with_paper("3").with_paper_bright("1") == ColourAttr{0xB0}
        && ColourAttr{0xFF}.with_ink("0") == ColourAttr{0xF8}
        && ColourAttr{}.with_ink("-1") == ColourAttr{0x07},
    "user_io.cpp's col_parse: each text sets its own field and keeps the rest"
);

}
