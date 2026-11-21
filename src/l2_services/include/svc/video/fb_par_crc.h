// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::svc::video {

struct FbParCrc {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr FbParCrc of(hal::SpiWord reply) noexcept {
        return FbParCrc{reply.low_byte()};
    }

    friend constexpr bool operator==(FbParCrc, FbParCrc) = default;
};

}
