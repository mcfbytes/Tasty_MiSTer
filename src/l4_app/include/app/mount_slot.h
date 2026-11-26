// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/ocram_slot_arg.h"
#include "hal/ocram_types.h"
#include "proto/types.h"

namespace mister::app {

struct MountSlot {
    hal::ocram::SlotArg arg{};
    proto::SlotIndex slot{};
    hal::ocram::Words frame{};

    [[nodiscard]] constexpr bool granted() const noexcept { return frame != hal::ocram::Words{}; }

    [[nodiscard]] constexpr proto::BlockSize block_size() const noexcept {
        return proto::BlockSize::of(frame.bytes());
    }
};

}
