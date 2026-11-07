// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::xthread {

struct PauseGen {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PauseGen none() noexcept { return PauseGen{0}; }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    friend constexpr bool operator==(PauseGen, PauseGen) = default;
};

static_assert(
    sizeof(PauseGen) == 4 && std::is_trivially_copyable_v<PauseGen>,
    "the latch payload stays one lock-free word"
);

}
