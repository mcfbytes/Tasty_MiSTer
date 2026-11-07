// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mister::cores {

struct Fnv1a64 {
    std::uint64_t v = kBasis;

    static constexpr std::uint64_t kBasis = 0xcbf29ce484222325ull;
    static constexpr std::uint64_t kPrime = 0x100000001b3ull;
    static constexpr unsigned kNibbleBits = 4;
    static constexpr std::size_t kHexDigits = 16;

    [[nodiscard]] static constexpr Fnv1a64 digest_of(std::string_view bytes) noexcept {
        Fnv1a64 out;
        for (char c : bytes) {
            out.v ^= static_cast<std::uint8_t>(c);
            out.v *= kPrime;
        }
        return out;
    }

    [[nodiscard]] constexpr std::array<char, kHexDigits> hex() const noexcept {
        constexpr char kDigits[] = "0123456789abcdef";
        constexpr std::uint64_t kNibbleMask = 0xF;
        std::array<char, kHexDigits> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const std::size_t shift = (out.size() - 1u - i) * kNibbleBits;
            out[i] = kDigits[(v >> shift) & kNibbleMask];
        }
        return out;
    }

    friend constexpr bool operator==(Fnv1a64, Fnv1a64) = default;
};

}
