// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>

namespace mister::cores {

enum class AssetSweep : std::uint8_t { None, HexPage };

struct HexPagePlace {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr HexPagePlace end() noexcept {
        constexpr std::uint16_t kPlaces = 258;
        return HexPagePlace{kPlaces};
    }

    [[nodiscard]] constexpr HexPagePlace next() const noexcept {
        return HexPagePlace{static_cast<std::uint16_t>(v + 1)};
    }

    [[nodiscard]] constexpr std::array<char, 2> suffix() const noexcept {
        constexpr std::array<std::array<char, 2>, 2> kNamed{{{'Z', 'Z'}, {'Z', '0'}}};
        constexpr char kDigits[] = "0123456789ABCDEF";
        constexpr unsigned kRadix = 16;
        constexpr unsigned kPages = 256;
        if (v < kNamed.size())
            return kNamed[v];
        const unsigned page = (static_cast<unsigned>(v) - kNamed.size()) % kPages;
        return {kDigits[page / kRadix], kDigits[page % kRadix]};
    }

    friend constexpr bool operator==(HexPagePlace, HexPagePlace) = default;
    friend constexpr auto operator<=>(HexPagePlace, HexPagePlace) = default;
};

static_assert(
    HexPagePlace{}.suffix() == std::array<char, 2>{'Z', 'Z'}
        && HexPagePlace{}.next().suffix() == std::array<char, 2>{'Z', '0'}
        && HexPagePlace{2}.suffix() == std::array<char, 2>{'0', '0'}
        && HexPagePlace{12}.suffix() == std::array<char, 2>{'0', 'A'}
        && HexPagePlace{257}.suffix() == std::array<char, 2>{'F', 'F'}
        && HexPagePlace{257}.next() == HexPagePlace::end(),
    "user_io.cpp:1650-1657: eZZ, eZ0, then e%02X for 0..255"
);

}
