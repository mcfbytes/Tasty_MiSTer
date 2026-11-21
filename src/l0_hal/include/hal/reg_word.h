// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_field.h"
#include "hal/spi_halves.h"
#include "hal/types.h"
#include "infra/error.h"

namespace mister::hal {

struct RegWord {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return v; }

    [[nodiscard]] constexpr bool intersects(RegWord mask) const noexcept {
        return (v & mask.v) != 0u;
    }

    [[nodiscard]] constexpr bool any() const noexcept { return v != 0u; }

    [[nodiscard]] constexpr RegWord masked(RegWord mask) const noexcept {
        return RegWord{v & mask.v};
    }

    template <class Decode>
    [[nodiscard]] constexpr decltype(auto) decoded(Decode&& decode) const noexcept {
        return decode(v);
    }

    template <class Store>
    void emit(Store&& store) const noexcept {
        store(v);
    }

    [[nodiscard]] constexpr SpiHalves halves() const noexcept { return SpiHalves::of(v); }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(RegWord, RegWord) = default;
};

template <std::unsigned_integral Rep>
constexpr auto RegField<Rep>::get(RegWord word) const noexcept -> Value
    requires std::same_as<Rep, std::uint32_t>
{
    return Value{get(word.v)};
}

constexpr LwOffset LwOffset::of(RegWord w) noexcept {
    return LwOffset{w.v};
}

constexpr LwExtent LwExtent::of(RegWord w) noexcept {
    return LwExtent{w.v};
}

}
