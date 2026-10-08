// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/hd_rect.h"
#include "infra/seat.h"

namespace mister::app {

class HdDirtyLedger {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Region : std::uint8_t { Game, Panel };

    void reset(HdRect panel, HdRect game) noexcept;
    void invalidate(HdRect panel, HdRect game) noexcept;
    void begin_tick() noexcept;
    void mark(Region region, HdRect dirty) noexcept;
    [[nodiscard]] HdRect consume(std::uint8_t slot, Region region) noexcept;

private:
    static constexpr int kRegions = 2;
    HdRect stale_[2][kRegions]{};
    HdRect tick_[kRegions]{};
};

}  // namespace mister::app
