// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "cores/n64_db.h"
#include "infra/seat.h"

namespace mister::cores::n64 {

enum class DetectFlag : std::uint8_t {
    Region = kDetectRegionBit,
    Db = kDetectDbBit,
};

[[nodiscard]] constexpr std::uint8_t detect_bit(DetectFlag f) noexcept {
    switch (f) {
        case DetectFlag::Region:
            return kDetectRegionBit;
        case DetectFlag::Db:
            return kDetectDbBit;
    }
    return 0;
}

struct DetectFlags {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr DetectFlags of(std::uint8_t bits) noexcept {
        return DetectFlags{bits};
    }

    [[nodiscard]] constexpr bool has(DetectFlag f) const noexcept {
        return (v & detect_bit(f)) != 0;
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr DetectFlags with(DetectFlag f) const noexcept {
        return DetectFlags{static_cast<std::uint8_t>(v | detect_bit(f))};
    }

    friend constexpr bool operator==(DetectFlags, DetectFlags) = default;
};

}
