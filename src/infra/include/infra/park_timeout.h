// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

namespace mister::infra {

using ParkTimeout = std::chrono::duration<std::int32_t, std::milli>;

inline constexpr ParkTimeout kForever{-1};
inline constexpr ParkTimeout kNoWait{0};

static_assert(sizeof(ParkTimeout) == 4);

}
