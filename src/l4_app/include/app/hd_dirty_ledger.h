// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/flip_buffer.h"
#include "app/hd_rect.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"

namespace mister::app {

class HdDirtyLedger {
    TASTY_SEAT_EXEMPT(component);

public:
    enum class Region : std::uint8_t { Game, Panel, Band };

    void reset(HdRect panel, HdRect band, HdRect game) noexcept;
    void mark(Region region, HdRect dirty) noexcept;
    [[nodiscard]] HdRect peek(FlipBuffer buffer, Region region) const noexcept;
    [[nodiscard]] HdRect take(FlipBuffer buffer, Region region) noexcept;

private:
    static constexpr std::size_t kRegions = 3;
    infra::IndexedArray<std::array<HdRect, kRegions>, 2, FlipBuffer> stale_{};
};

}
