// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fabric_state.h"

namespace mister::app {

void FabricStateView::refresh(const FabricCell& cell, std::int64_t now_ns) noexcept {
    TASTY_SEAT_BODY(FabricStateView);

    FabricState got{};
    if (reader_.take_if_changed(cell, got)) {
        last_ = got;
        fresh_ns_ = now_ns;
        has_sample_ = true;
        stale_ = false;
        return;
    }

    if (has_sample_ && !stale_ && now_ns - fresh_ns_ > kFabricStaleNs) {
        stale_ = true;
        ++expiries_;
    }
}

}  // namespace mister::app
