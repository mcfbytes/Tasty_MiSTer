// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::reactor {

enum class Cause : std::uint8_t {
    None,
    Tick,
    Frame,
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
    constexpr CauseSet() noexcept = default;
    constexpr explicit CauseSet(Cause c) noexcept : bits_(bit(c)) {}

    constexpr CauseSet& operator|=(CauseSet o) noexcept {
        bits_ = static_cast<std::uint8_t>(bits_ | o.bits_);
        return *this;
    }
    friend constexpr CauseSet operator|(CauseSet a, CauseSet b) noexcept {
        a |= b;
        return a;
    }
    friend constexpr bool operator==(CauseSet, CauseSet) = default;

    constexpr bool has(Cause c) const noexcept { return (bits_ & bit(c)) != 0u; }
    constexpr bool empty() const noexcept { return bits_ == 0u; }

private:
    static constexpr std::uint8_t bit(Cause c) noexcept {
        return c == Cause::None ? std::uint8_t{0}
                                : static_cast<std::uint8_t>(1u << static_cast<unsigned>(c));
    }
    std::uint8_t bits_ = 0;
};
static_assert(static_cast<unsigned>(Cause::kCount) <= 8,
              "CauseSet packs one bit per Cause into a uint8_t. Keyed to the "
              "enum's OWN count, never to the last enumerator's name: a ninth "
              "Cause appended after Test left `Cause::Test < 8` true while its "
              "own bit shifted out of the byte and read false forever — a "
              "notifier whose cause never matches is a STARVED service row, "
              "with no diagnostic anywhere (reviewer D1, pass MZ)");

}  // namespace mister::reactor
