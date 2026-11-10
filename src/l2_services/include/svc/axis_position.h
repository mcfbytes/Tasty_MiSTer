// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>

#include "svc/types.h"

namespace mister::svc {

struct AxisPosition {
    std::size_t v = 0;

    template <std::size_t Count>
    [[nodiscard]] static constexpr std::optional<AxisPosition> of(EvdevCode code) noexcept {
        const std::optional<std::size_t> row = code.axis_position<Count>();
        if (!row)
            return std::nullopt;
        return AxisPosition{*row};
    }

    friend constexpr bool operator==(AxisPosition, AxisPosition) = default;
};

}
