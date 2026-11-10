// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::n64 {

enum class DetectFlag : std::uint8_t {
    Region = 1u << 0,
    Db = 1u << 1,
};

struct DetectFlags {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool has(DetectFlag f) const noexcept { return (v & bit(f)) != 0; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr DetectFlags with(DetectFlag f) const noexcept {
        return DetectFlags{static_cast<std::uint8_t>(v | bit(f))};
    }

    friend constexpr bool operator==(DetectFlags, DetectFlags) = default;

private:
    [[nodiscard]] static constexpr std::uint8_t bit(DetectFlag f) noexcept {
        return static_cast<std::uint8_t>(f);
    }
};

}
