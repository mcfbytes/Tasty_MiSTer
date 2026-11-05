// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_reset.h"

#include <unistd.h>

#include "hal/bridge_sequencer.h"

namespace mister::fw {

void BoardReset::board_reset() {
    TASTY_SEAT_BODY(BoardReset);

    if (!bridges_.request_board_reset())
        return;
    if (sleeper_) {
        sleeper_->sleep_ms(kResetSettleMs);
        return;
    }
    constexpr unsigned kPollSliceMs = 100;
    constexpr useconds_t kPollSliceUs = kPollSliceMs * 1000;
    for (unsigned ms = 0; ms < kResetSettleMs; ms += kPollSliceMs) {
        ::usleep(kPollSliceUs);
    }
}

}
