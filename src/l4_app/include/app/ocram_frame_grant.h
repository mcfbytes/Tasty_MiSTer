// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "hal/ocram.h"

namespace mister::app {

struct OcramFrameGrant {
    hal::ocram::RingLend lend{};
    std::optional<hal::ocram::LaneShape> shape{};

    [[nodiscard]] bool granted() const noexcept { return lend.lent() && shape.has_value(); }
};

}
