// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/nic_main.h"

namespace mister::svc {

void NicMain::start() noexcept {
    TASTY_SEAT_BODY(NicMain);
    loop_();
}

}
