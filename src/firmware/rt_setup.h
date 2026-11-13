// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "os/types.h"

namespace mister::fw {

enum class RtMode : std::uint8_t { Required, BestEffort };

struct RtSetup {
    bool applied = false;
    int err = 0;
};

[[nodiscard]] RtSetup pin_current_cpu(os::CpuId cpu) noexcept;

[[nodiscard]] RtSetup confirm_current_cpu(os::CpuId cpu) noexcept;

}
