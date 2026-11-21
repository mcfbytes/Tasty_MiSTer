// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/saturating.h"

namespace mister::infra {

using Nanos32 = std::chrono::duration<std::uint32_t, std::nano>;
using Micros32 = std::chrono::duration<std::uint32_t, std::micro>;
using Millis32 = std::chrono::duration<std::uint32_t, std::milli>;

static_assert(sizeof(Nanos32) == 4 && sizeof(Micros32) == 4 && sizeof(Millis32) == 4);

template <class To>
[[nodiscard]] constexpr To saturated(std::chrono::nanoseconds span) noexcept {
    if (span <= std::chrono::nanoseconds::zero())
        return To::zero();
    using Wide = std::chrono::duration<std::uint64_t, typename To::period>;
    return To{sat_add(std::uint32_t{0}, std::chrono::duration_cast<Wide>(span).count())};
}

}
