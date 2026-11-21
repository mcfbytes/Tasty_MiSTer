// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd/hd_osd_main.h"

namespace mister::app::hd {

void HdOsdMain::start() noexcept {
    TASTY_SEAT_BODY(HdOsdMain);
    renderer_.begin();
    loop_();
}

void HdOsdMain::serve() noexcept {
    if (stopping())
        return;
    renderer_.serve();
}

}
