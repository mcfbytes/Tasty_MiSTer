// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/osd_block.h"
#include "proto/types.h"

namespace mister::proto::link {

struct OsdFifoDepth {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr OsdFifoDepth declared() noexcept {
        return OsdFifoDepth{hal::OsdBlock::kFifoEntries};
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(OsdFifoDepth, OsdFifoDepth) = default;
};

}
