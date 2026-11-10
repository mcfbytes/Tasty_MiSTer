// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <concepts>
#include <cstdint>

namespace mister::svc {

struct PllPayload {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::array<std::uint16_t, 2> halves() const noexcept {
        constexpr unsigned kHalfBits = 16;
        return {static_cast<std::uint16_t>(v), static_cast<std::uint16_t>(v >> kHalfBits)};
    }

    template <std::integral Word>
    constexpr void store_into(Word& slot) const noexcept {
        slot = static_cast<Word>(v);
    }

    friend constexpr bool operator==(PllPayload, PllPayload) = default;
};

inline constexpr PllPayload kPllNPayload{0x10000};
inline constexpr PllPayload kPllChargePumpValue{2};
inline constexpr PllPayload kPllBandwidthValue{7};

}
