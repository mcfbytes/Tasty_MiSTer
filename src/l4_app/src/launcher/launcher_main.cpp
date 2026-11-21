// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher/launcher_main.h"

namespace mister::app::launcher {

void LauncherMain::start() noexcept {
    TASTY_SEAT_BODY(LauncherMain);
    loop_();
}

}
