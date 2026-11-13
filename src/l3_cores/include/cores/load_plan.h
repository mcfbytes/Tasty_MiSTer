// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "cores/transfer_row.h"
#include "infra/seat.h"

namespace mister::cores {

inline constexpr std::size_t kMaxTransferRows = 32;

struct PlanFrame {
    bool hold_reset = false;
    bool release_reset = false;
    bool refused = false;
    std::string_view save{};
};

struct LoadPlan {
    TASTY_SEAT_EXEMPT(component);

    struct RowIx {
        std::size_t v = 0;
        friend constexpr bool operator==(RowIx, RowIx) = default;
    };

    std::array<TransferRow, kMaxTransferRows> rows{};
    std::size_t count = 0;
    PlanFrame frame{};

    [[nodiscard]] constexpr TransferRow& operator[](RowIx ix) noexcept { return rows[ix.v]; }

    [[nodiscard]] constexpr const TransferRow& operator[](RowIx ix) const noexcept {
        return rows[ix.v];
    }
};

}
