// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "proto/paddle_pos.h"

namespace mister::svc::input {

struct AxisCal {
    std::int32_t min = 0;
    std::int32_t max = 0;
    std::int32_t flat = 0;
    std::int32_t fuzz = 0;
    std::int32_t seen_max = 0;

    [[nodiscard]] constexpr std::optional<proto::PaddlePos::Range> paddle_range() const noexcept {
        return proto::PaddlePos::Range::from(min).to(max);
    }
};

}
