// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/json_out.h"
#include "infra/seat.h"

namespace mister::reactor {

enum class RoundLifecycle : std::uint8_t { Rung = 1, Bind = 2, Switch = 4 };

class RoundLifecycleSet {

    TASTY_SEAT_EXEMPT(component);

public:
    constexpr RoundLifecycleSet() noexcept = default;

    constexpr explicit RoundLifecycleSet(RoundLifecycle why) noexcept : bits_(bit(why)) {}

    [[nodiscard]] constexpr RoundLifecycleSet with(RoundLifecycle why) const noexcept {
        RoundLifecycleSet s = *this;
        s.bits_ = static_cast<std::uint8_t>(bits_ | bit(why));
        return s;
    }

    [[nodiscard]] constexpr bool has(RoundLifecycle why) const noexcept {
        return (bits_ & bit(why)) != 0u;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0u; }

    [[nodiscard]] static constexpr RoundLifecycleSet all() noexcept {
        return RoundLifecycleSet{}
            .with(RoundLifecycle::Rung)
            .with(RoundLifecycle::Bind)
            .with(RoundLifecycle::Switch);
    }

    [[nodiscard]] constexpr std::uint8_t mask() const noexcept { return bits_; }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        RoundLifecycleSet s
    ) noexcept {
        o.field(key, s.bits_);
    }

    friend constexpr void json_elem(infra::JsonOut& o, RoundLifecycleSet s) noexcept {
        o.elem(s.bits_);
    }

    friend constexpr bool operator==(RoundLifecycleSet, RoundLifecycleSet) = default;

private:
    static constexpr std::uint8_t bit(RoundLifecycle why) noexcept {
        return static_cast<std::uint8_t>(why);
    }

    std::uint8_t bits_ = 0;
};

inline constexpr RoundLifecycleSet kRoundLifecycleAll = RoundLifecycleSet::all();

}
