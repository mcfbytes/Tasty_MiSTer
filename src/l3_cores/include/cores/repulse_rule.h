// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include "cores/read_count.h"
#include "proto/types.h"

namespace mister::cores {

struct RepulseRule {
    proto::SlotIndex slot{};

    std::chrono::milliseconds after{};

    ReadCount unread_reads{};

    [[nodiscard]] constexpr bool declared() const noexcept {
        return after != std::chrono::milliseconds::zero();
    }
};

}
