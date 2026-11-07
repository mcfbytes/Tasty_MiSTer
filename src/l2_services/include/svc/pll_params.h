// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

enum class PllReg : std::uint32_t {
    M = 4,
    N = 3,
    C = 5,
    ChargePump = 9,
    Bandwidth = 8,
    K = 7,
};

[[nodiscard]] constexpr std::uint32_t pll_reg_word(PllReg r) noexcept {
    return static_cast<std::uint32_t>(r);
}

inline constexpr std::uint32_t kPllNPayload = 0x10000;
inline constexpr std::uint32_t kPllChargePumpValue = 2;
inline constexpr std::uint32_t kPllBandwidthValue = 7;

inline constexpr std::uint32_t kPllAsymmetricBit = 0x20000;
inline constexpr unsigned kPllDivHalfShift = 8;

inline constexpr double kFvcoFloorMhz = 400.0;
inline constexpr double kFractionScale = 4294967296.0;

inline constexpr std::uint32_t kPllZeroFraction = 1;

struct PllParams {
    std::uint32_t c = 0;
    std::uint32_t m = 0;
    std::uint32_t k = 0;
    bool approximated = false;
};

}
