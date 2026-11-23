// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/opt_ref.h"

namespace mister::cores {

struct LoadChoice {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    template <class Row>
    [[nodiscard]] constexpr infra::OptRef<const Row> among(
        std::span<const Row> rows
    ) const noexcept {
        if (none() || std::size_t{v} > rows.size())
            return {};
        return infra::OptRef<const Row>{rows[std::size_t{v} - 1u]};
    }

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr LoadChoice at(I place) noexcept {
        return LoadChoice{static_cast<std::uint8_t>(place + 1u)};
    }

    friend constexpr bool operator==(LoadChoice, LoadChoice) = default;
};

}
