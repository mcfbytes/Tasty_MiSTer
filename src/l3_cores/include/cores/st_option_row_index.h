// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::cores {

struct StRowCount {
    std::uint8_t v = 0;
    using affine_count = StRowCount;
    friend constexpr bool operator==(StRowCount, StRowCount) = default;
};

struct StRowPos {
    std::uint8_t v = 0;
    using affine_count = StRowCount;
    friend constexpr bool operator==(StRowPos, StRowPos) = default;
};

[[nodiscard]] constexpr StRowPos row_pos(proto::OptionRowIndex row) noexcept {
    return StRowPos{row.v};
}

}
