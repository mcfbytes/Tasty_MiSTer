// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/reg_field.h"

namespace mister::hal::ocram {

struct RowArg {
    static constexpr std::uint32_t kMaxRows = 32;

    constexpr RowArg() noexcept = default;

    [[nodiscard]] static constexpr std::optional<RowArg> of(std::uint32_t row) noexcept {
        if (row >= kMaxRows)
            return std::nullopt;
        return RowArg{static_cast<std::uint8_t>(row)};
    }

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    friend constexpr bool operator==(RowArg, RowArg) = default;

private:
    constexpr explicit RowArg(std::uint8_t row) noexcept : v(row) {}

    std::uint8_t v = 0;
};

}
