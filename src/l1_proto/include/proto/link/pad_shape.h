// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/pad_block.h"
#include "proto/types.h"

namespace mister::proto::link {

struct PadShape {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr PadShape expected() noexcept {
        return PadShape{hal::PadBlock::kShape};
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(PadShape, PadShape) = default;
};

}
