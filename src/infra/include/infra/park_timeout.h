// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

namespace mister::infra {

using ParkTimeout = std::chrono::duration<std::int32_t, std::milli>;

inline constexpr ParkTimeout kForever{-1};
inline constexpr ParkTimeout kNoWait{0};

static_assert(sizeof(ParkTimeout) == 4);

[[nodiscard]] constexpr ParkTimeout park_timeout_for(
    std::chrono::nanoseconds left,
    ParkTimeout ceiling
) noexcept {
    if (left <= std::chrono::nanoseconds::zero())
        return kNoWait;
    const std::chrono::milliseconds ms = std::chrono::ceil<std::chrono::milliseconds>(left);
    if (ms >= ceiling)
        return ceiling;
    return std::chrono::duration_cast<ParkTimeout>(ms);
}

}
