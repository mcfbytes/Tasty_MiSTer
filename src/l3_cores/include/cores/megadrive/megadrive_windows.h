// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "cores/core_window_decl.h"

namespace mister::cores::megadrive {

inline constexpr auto kMdPlusWindows = std::to_array<CoreWindowDecl>({
    {.region = {0x1000'0000u, 0x0001'0000u, "mdplus-ring"}, .feeds_pcm = true},
});

}
