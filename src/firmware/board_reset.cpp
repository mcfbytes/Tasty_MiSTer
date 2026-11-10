// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_reset.h"

#include <chrono>

#include "app/board_ops.h"
#include "hal/bridge_sequencer.h"

namespace mister::fw {

void BoardReset::board_reset() {
    TASTY_SEAT_BODY(BoardReset);

    if (!bridges_.request_board_reset())
        return;

    if (sleeper_) {
        app::sleep_ms(sleeper_, kResetSettle);
        return;
    }
    for (auto waited = std::chrono::milliseconds{}; waited < kResetSettle; waited += kPollSlice) {
        app::sleep_ms(infra::OptRef<app::ISleeper>{}, kPollSlice);
    }
}

}
