// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "cores/core_window_decl.h"

namespace mister::cores::n64 {

inline constexpr std::uint32_t kRomLoadAddr = 0x3200'0000;
inline constexpr std::uint32_t kDdDiskLoadAddr = 0x3600'0000;
inline constexpr std::uint32_t kDdIplLoadAddr = 0x30C0'0000;

inline constexpr auto kN64Windows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0080'0000u, "n64-rdram"}, .feeds_pcm = false},
});

}
