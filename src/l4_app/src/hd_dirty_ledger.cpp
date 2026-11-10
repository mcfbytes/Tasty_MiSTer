// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_dirty_ledger.h"

namespace mister::app {
namespace {

std::size_t region_index(HdDirtyLedger::Region region) noexcept {
    return static_cast<std::size_t>(region);
}

}

void HdDirtyLedger::reset(HdRect panel, HdRect band, HdRect game) noexcept {
    for (const FlipBuffer b : kFlipBuffers) {
        stale_[b][region_index(Region::Game)] = game;
        stale_[b][region_index(Region::Panel)] = panel;
        stale_[b][region_index(Region::Band)] = band;
    }
}

void HdDirtyLedger::mark(Region region, HdRect dirty) noexcept {
    if (dirty.is_empty())
        return;
    const std::size_t r = region_index(region);
    for (const FlipBuffer b : kFlipBuffers)
        stale_[b][r] = stale_[b][r].union_with(dirty);
}

HdRect HdDirtyLedger::peek(FlipBuffer buffer, Region region) const noexcept {
    return stale_[buffer][region_index(region)];
}

HdRect HdDirtyLedger::take(FlipBuffer buffer, Region region) noexcept {
    const std::size_t r = region_index(region);
    const HdRect out = stale_[buffer][r];
    stale_[buffer][r] = {};
    return out;
}

}
