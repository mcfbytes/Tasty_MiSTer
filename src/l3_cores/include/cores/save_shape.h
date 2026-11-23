// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/save_frame.h"
#include "proto/types.h"

namespace mister::cores {

struct SaveShape {
    SaveFrame frame{};

    std::span<const std::uint8_t> lead{};

    [[nodiscard]] constexpr bool frames(proto::FileSize wire) const noexcept {
        return frame.frames(wire, lead);
    }
};

}
