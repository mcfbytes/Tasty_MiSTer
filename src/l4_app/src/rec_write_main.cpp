// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rec_write_main.h"

#include <algorithm>

namespace mister::app {

void RecWriteMain::start() noexcept {
    TASTY_SEAT_BODY(RecWriteMain);
    loop_();
}

void RecWriteMain::serve() noexcept {
    avi_.serve();
    writer_.serve();
}

int RecWriteMain::park_ms() const noexcept {
    const int a = avi_.park_ms();
    const int b = writer_.park_ms();
    if (a < 0) return b;
    return b < 0 ? a : std::min(a, b);
}

}  // namespace mister::app
