// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_main.h"

namespace mister::app {

void LauncherMain::start() noexcept {
    TASTY_SEAT_BODY(LauncherMain);
    loop_();
}

}  // namespace mister::app
