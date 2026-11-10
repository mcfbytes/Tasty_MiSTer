// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fabric_state.h"

namespace mister::app {

void FabricStateView::refresh(
    const FabricCell& cell,
    os::RawMonotonicClock::time_point now
) noexcept {
    TASTY_SEAT_BODY(FabricStateView);

    FabricState got{};
    if (reader_.take_if_changed(cell, got)) {
        last_ = got;
        fresh_ = now;
        has_sample_ = true;
        stale_ = false;
        return;
    }

    if (has_sample_ && !stale_ && now - fresh_ > kFabricStale) {
        stale_ = true;
        ++expiries_;
    }
}

}
