// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"

namespace mister::cores {

struct SaturnPending {
    TASTY_SEAT_RESIDENT(RT);

    bool stop_pend = false;
    bool seek_pend = false;
    bool read_pend = false;
    bool final_read = false;
    bool seek_ring = false;
    bool seek_ring2 = false;
    bool pause_pend = false;
    bool read_toc = false;

    void reset() noexcept {
        stop_pend = seek_pend = read_pend = seek_ring = seek_ring2 = pause_pend = read_toc = false;
    }
};

}
