// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::os {

class HexDigit {
public:
    [[nodiscard]] static constexpr std::optional<HexDigit> of(char c) noexcept {
        if (c >= '0' && c <= '9')
            return HexDigit{c - '0'};
        if (c >= 'a' && c <= 'f')
            return HexDigit{c - 'a' + kAlphaOffset};
        if (c >= 'A' && c <= 'F')
            return HexDigit{c - 'A' + kAlphaOffset};
        return std::nullopt;
    }

    [[nodiscard]] static constexpr std::byte octet(HexDigit hi, HexDigit lo) noexcept {
        return (std::byte{hi.nibble_} << kNibbleBits) | std::byte{lo.nibble_};
    }

    [[nodiscard]] static constexpr char character(HexDigit hi, HexDigit lo) noexcept {
        return std::bit_cast<char>(octet(hi, lo));
    }

private:
    static constexpr int kAlphaOffset = 10;
    static constexpr int kNibbleBits = 4;

    explicit constexpr HexDigit(int nibble) noexcept : nibble_(static_cast<std::uint8_t>(nibble)) {}

    std::uint8_t nibble_;
};

}
