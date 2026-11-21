// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/enum_set.h"
#include "infra/json_out.h"
#include "infra/seat.h"

namespace mister::reactor {

enum class RoundLifecycle : std::uint8_t { Rung, Bind, Switch, kCount };

class RoundLifecycleSet {

    TASTY_SEAT_EXEMPT(component);

public:
    using Bits = infra::EnumSet<RoundLifecycle, std::uint8_t>;

    constexpr RoundLifecycleSet() noexcept = default;

    constexpr explicit RoundLifecycleSet(RoundLifecycle why) noexcept { bits_.insert(why); }

    [[nodiscard]] constexpr RoundLifecycleSet with(RoundLifecycle why) const noexcept {
        RoundLifecycleSet s = *this;
        s.bits_.insert(why);
        return s;
    }

    [[nodiscard]] constexpr bool contains(RoundLifecycle why) const noexcept {
        return bits_.contains(why);
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_.is_empty(); }

    [[nodiscard]] static constexpr RoundLifecycleSet all() noexcept {
        return RoundLifecycleSet{Bits::all()};
    }

    [[nodiscard]] constexpr std::uint8_t mask() const noexcept { return bits_.bits(); }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        RoundLifecycleSet s
    ) noexcept {
        o.field(key, s.mask());
    }

    friend constexpr void json_elem(infra::JsonOut& o, RoundLifecycleSet s) noexcept {
        o.elem(s.mask());
    }

    friend constexpr bool operator==(RoundLifecycleSet, RoundLifecycleSet) = default;

private:
    constexpr explicit RoundLifecycleSet(Bits bits) noexcept : bits_(bits) {}

    Bits bits_{};
};

static_assert(sizeof(RoundLifecycleSet) == 1);

inline constexpr RoundLifecycleSet kRoundLifecycleAll = RoundLifecycleSet::all();

}
