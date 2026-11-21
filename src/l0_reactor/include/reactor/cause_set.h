// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/enum_set.h"
#include "infra/seat.h"

namespace mister::reactor {

enum class Cause : std::uint8_t {
    None,
    Tick,
    Blk,
    Fifo,
    Ring,
    CoreTick,
    Test,
    kCount,
};

class CauseSet {

    TASTY_SEAT_EXEMPT(component);

public:
    using Bits = infra::EnumSet<Cause, std::uint16_t>;

    constexpr CauseSet() noexcept = default;

    constexpr explicit CauseSet(Cause c) noexcept {
        if (c != Cause::None)
            bits_.insert(c);
    }

    constexpr CauseSet& operator|=(CauseSet o) noexcept {
        bits_ = bits_.united(o.bits_);
        return *this;
    }

    friend constexpr CauseSet operator|(CauseSet a, CauseSet b) noexcept {
        a |= b;
        return a;
    }

    friend constexpr bool operator==(CauseSet, CauseSet) = default;

    [[nodiscard]] constexpr bool contains(Cause c) const noexcept { return bits_.contains(c); }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return bits_.is_empty(); }

private:
    Bits bits_{};
};

static_assert(sizeof(CauseSet) == 2);
static_assert(
    static_cast<unsigned>(Cause::kCount) <= 16,
    "CauseSet packs one bit per Cause into a uint16_t. Keyed to the "
    "enum's OWN count, never to the last enumerator's name: a ninth "
    "Cause appended after Test left `Cause::Test < 8` true while its "
    "own bit shifted out of the byte and read false forever — a "
    "notifier whose cause never matches is a STARVED service row, "
    "with no diagnostic anywhere (reviewer D1, pass MZ)"
);

}
