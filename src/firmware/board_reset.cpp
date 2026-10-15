// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_reset.h"

#include <unistd.h>

#include "hal/bridge_sequencer.h"

namespace mister::fw {

void BoardReset::board_reset() {
    TASTY_SEAT_BODY(BoardReset);

    if (!bridges_.request_board_reset()) return;
    if (sleeper_) {
        sleeper_->sleep_ms(kResetSettleMs);
        return;
    }
    for (unsigned ms = 0; ms < kResetSettleMs; ms += 100) {
        ::usleep(100000);
    }
}

}  // namespace mister::fw
