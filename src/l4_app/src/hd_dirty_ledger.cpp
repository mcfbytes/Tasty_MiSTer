// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_dirty_ledger.h"

namespace mister::app {
namespace {

[[nodiscard]] bool empty(HdRect r) noexcept {
    return r.w <= 0 || r.h <= 0;
}

[[nodiscard]] HdRect unite(HdRect a, HdRect b) noexcept {
    if (empty(a))
        return b;
    if (empty(b))
        return a;
    const std::int32_t x0 = a.x < b.x ? a.x : b.x;
    const std::int32_t y0 = a.y < b.y ? a.y : b.y;
    const std::int32_t x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const std::int32_t y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return HdRect{x0, y0, x1 - x0, y1 - y0};
}

int region_index(HdDirtyLedger::Region region) noexcept {
    return static_cast<int>(region);
}

}

void HdDirtyLedger::reset(HdRect panel, HdRect band, HdRect game) noexcept {
    stale_[0][region_index(Region::Game)] = stale_[1][region_index(Region::Game)] = game;
    stale_[0][region_index(Region::Panel)] = stale_[1][region_index(Region::Panel)] = panel;
    stale_[0][region_index(Region::Band)] = stale_[1][region_index(Region::Band)] = band;
}

void HdDirtyLedger::invalidate(HdRect panel, HdRect band, HdRect game) noexcept {
    reset(panel, band, game);
}

void HdDirtyLedger::mark(Region region, HdRect dirty) noexcept {
    if (empty(dirty))
        return;
    const int r = region_index(region);
    stale_[0][r] = unite(stale_[0][r], dirty);
    stale_[1][r] = unite(stale_[1][r], dirty);
}

HdRect HdDirtyLedger::peek(std::uint8_t slot, Region region) const noexcept {
    return stale_[slot & 1u][region_index(region)];
}

HdRect HdDirtyLedger::take(std::uint8_t slot, Region region) noexcept {
    const int s = slot & 1u;
    const int r = region_index(region);
    const HdRect out = stale_[s][r];
    stale_[s][r] = {};
    return out;
}

}
