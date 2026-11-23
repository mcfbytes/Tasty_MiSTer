// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

#include "cores/lba_window_row.h"
#include "infra/seat.h"
#include "proto/window_geometry.h"

namespace mister::cores {

class LbaWindowGeometry final : public proto::IWindowGeometry {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit constexpr LbaWindowGeometry(std::span<const LbaWindowRow> rows) noexcept
        : rows_(rows) {}

    [[nodiscard]] proto::BlockGeometry geometry_for(
        proto::SlotIndex slot,
        proto::Lba lba,
        proto::BlockGeometry wire
    ) const noexcept override {
        TASTY_SEAT_BODY(LbaWindowGeometry);
        for (const LbaWindowRow& row : rows_) {
            if (row.holds(slot, lba))
                return row.geometry(wire);
        }
        return wire;
    }

private:
    std::span<const LbaWindowRow> rows_;
};

}
