// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/frame_main.h"

namespace mister::reactor {

void FrameMain::start() noexcept {
    TASTY_SEAT_BODY(FrameMain);
    loop_();
}

void FrameMain::serve() noexcept {
    if (stopping()) return;
    clock_.wait_vsync();
}

}  // namespace mister::reactor
