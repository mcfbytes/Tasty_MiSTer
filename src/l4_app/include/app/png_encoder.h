// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "app/picture_size.h"
#include "infra/seat.h"

namespace mister::app {

class PngEncoder {
    TASTY_SEAT_RESIDENT(Diag);

public:
    [[nodiscard]] std::vector<std::uint8_t> encode(
        PictureSize size,
        std::span<const std::uint8_t> rgb
    ) const;
};

}
