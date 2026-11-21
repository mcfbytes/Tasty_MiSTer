// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>
#include <limits>

namespace mister::infra {

template <std::unsigned_integral R, std::unsigned_integral W>
    requires(sizeof(R) < sizeof(std::uintmax_t))
[[nodiscard]] constexpr R sat_add(R a, W b) noexcept {
    constexpr std::uintmax_t kTop = std::numeric_limits<R>::max();
    const std::uintmax_t room = kTop - a;
    const std::uintmax_t add = b;
    return add >= room ? static_cast<R>(kTop) : static_cast<R>(a + add);
}

template <std::unsigned_integral R>
[[nodiscard]] constexpr R sat_inc(R a) noexcept {
    return a == std::numeric_limits<R>::max() ? a : static_cast<R>(a + 1);
}

}
