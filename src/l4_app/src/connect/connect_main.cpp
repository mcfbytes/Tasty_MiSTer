// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/connect/connect_main.h"

namespace mister::app::connect {

void ConnectMain::start() noexcept {
    TASTY_SEAT_BODY(ConnectMain);
    loop_();
}

}
