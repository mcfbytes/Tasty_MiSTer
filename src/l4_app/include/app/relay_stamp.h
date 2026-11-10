// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::app {

struct CoreGen {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr CoreGen next() const noexcept {
        const auto n = static_cast<std::uint16_t>(v + 1u);
        return CoreGen{n == 0 ? std::uint16_t{1} : n};
    }

    friend constexpr bool operator==(CoreGen, CoreGen) = default;
};

struct ActEpoch {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr ActEpoch next() const noexcept {
        const auto n = static_cast<std::uint16_t>(v + 1u);
        return ActEpoch{n == 0 ? std::uint16_t{1} : n};
    }

    friend constexpr bool operator==(ActEpoch, ActEpoch) = default;
};

struct RelayStamp {
    CoreGen gen{};
    ActEpoch epoch{};

    [[nodiscard]] constexpr RelayStamp next_epoch() const noexcept {
        return RelayStamp{.gen = gen, .epoch = epoch.next()};
    }

    [[nodiscard]] constexpr RelayStamp next_core() const noexcept {
        return RelayStamp{.gen = gen.next(), .epoch = epoch};
    }

    [[nodiscard]] constexpr bool same_core(RelayStamp our) const noexcept { return gen == our.gen; }

    [[nodiscard]] constexpr bool matches(RelayStamp our) const noexcept {
        return gen == our.gen && epoch == our.epoch;
    }

    friend constexpr bool operator==(RelayStamp, RelayStamp) = default;
};

static_assert(sizeof(RelayStamp) == 4);

}
