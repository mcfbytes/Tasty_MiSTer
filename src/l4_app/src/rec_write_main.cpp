// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rec_write_main.h"

namespace mister::app {

void RecWriteMain::start() noexcept {
    TASTY_SEAT_BODY(RecWriteMain);
    loop_();
}

void RecWriteMain::serve() noexcept {
    avi_.serve();
    for (unsigned n = 0; n < kSidecarMsgsPerPass; ++n) {
        const auto m = sidecar_.pop();
        if (!m)
            break;
        writer_.on(*m);
    }
    writer_.publish();
}

}
