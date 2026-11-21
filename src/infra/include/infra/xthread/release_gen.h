// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <climits>
#include <cstdint>
#include <type_traits>

#include "infra/serial.h"

namespace mister::infra::xthread {

struct ReleaseGen {
    std::uint32_t v = 0;

    using serial_rule = infra::serial::SkipZero<std::uint32_t, (~0u >> CHAR_BIT)>;
    using serial_order = infra::serial::Order;
    static constexpr std::uint32_t kHalf = serial_rule::kMax / 2 + 1;

    [[nodiscard]] static constexpr ReleaseGen none() noexcept { return ReleaseGen{}; }

    [[nodiscard]] static constexpr ReleaseGen max() noexcept {
        return ReleaseGen{serial_rule::kMax};
    }

    [[nodiscard]] constexpr ReleaseGen next() const noexcept { return infra::serial::next(*this); }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool covers(ReleaseGen asked) const noexcept {
        return infra::serial::at_or_past(*this, asked.v);
    }

    friend constexpr bool operator==(ReleaseGen, ReleaseGen) = default;
};

static_assert(
    sizeof(ReleaseGen) == 4 && std::is_trivially_copyable_v<ReleaseGen>,
    "the latch payload stays one lock-free word"
);

}
