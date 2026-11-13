// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/connect_main.h"

namespace mister::app {

void ConnectMain::start() noexcept {
    TASTY_SEAT_BODY(ConnectMain);
    loop_();
}

}
