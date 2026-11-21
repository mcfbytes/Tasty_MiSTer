// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/decimal_text.h"
#include "infra/json_out.h"
#include "infra/seat.h"

namespace mister::infra {

struct Tally {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t v = 0;

    constexpr void add(Tally by = Tally{1}) noexcept { v += by.v; }

    constexpr void set(Tally n) noexcept { v = n.v; }

    [[nodiscard]] constexpr Tally sum(Tally by) const noexcept { return Tally{v + by.v}; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr void json_field(infra::JsonOut& o, infra::JsonName key, Tally t) noexcept {
        o.field(key, t.v);
    }

    friend constexpr bool operator==(Tally, Tally) = default;
};

}
