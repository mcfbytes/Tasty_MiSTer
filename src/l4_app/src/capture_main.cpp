// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/capture_main.h"

namespace mister::app {

void CaptureMain::start() noexcept {
    TASTY_SEAT_BODY(CaptureMain);
    loop_();
}

void CaptureMain::serve() noexcept {
    if (stopping()) return;
    copier_.serve();
}

}  // namespace mister::app
