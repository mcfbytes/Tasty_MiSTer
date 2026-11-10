// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

#include "cores/types.h"

namespace mister::cores::n64 {

struct SaveRowCount {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr SaveRowCount of(I rows) noexcept {
        return SaveRowCount{static_cast<std::uint8_t>(rows)};
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] static constexpr SaveRowCount through(SaveRow row) noexcept {
        return SaveRowCount::of(row.v + 1u);
    }

    [[nodiscard]] constexpr SaveRow end() const noexcept { return SaveRow{v}; }

    [[nodiscard]] constexpr SaveRow last() const noexcept {
        return SaveRow{static_cast<std::uint8_t>(v - 1u)};
    }

    [[nodiscard]] constexpr bool fits() const noexcept;
    friend constexpr bool operator==(SaveRowCount, SaveRowCount) = default;
};

inline constexpr std::size_t kMaxSaveRows = 5;

constexpr bool SaveRowCount::fits() const noexcept {
    return v <= kMaxSaveRows;
}

}
