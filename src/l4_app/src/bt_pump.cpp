// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bt_pump.h"

#include "app/process_integration.h"

namespace mister::app {

void BtPump::tick() {
    if (state_ == State::Spent) return;

    if (state_ == State::Unarmed) {
        due_ = clock_->now() + kDelay;
        state_ = State::Armed;
        ++stats_.arms;
        return;
    }

    if (clock_->now() < due_) return;

    state_ = State::Spent;

    const auto up = probe_();

    const char* outcome = "fire";
    std::uint32_t detail = 0;
    if (!up) {
        ++stats_.probe_failures;
        outcome = "probe_failed";
        detail = up.error().detail;
    } else if (*up) {
        ++stats_.suppressed_by_probe;
        outcome = "adapter_up";
    } else {
        ++stats_.fires;
        if (auto r = proc::bluetoothd_hcireset(); !r) {
            ++stats_.spawn_failures;
            outcome = "spawn_failed";
            detail = r.error().detail;
        }
    }

    if (diag_) {
        diag_->appendf("{\"t\":\"bt_oneshot\",\"outcome\":\"%s\","
                       "\"delay_ms\":%u,\"detail\":%u}",
                       outcome, static_cast<unsigned>(kDelay.count()),
                       static_cast<unsigned>(detail));
    }
}

}  // namespace mister::app
