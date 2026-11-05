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

infra::ParkTimeout RecWriteMain::park_ms() const noexcept {
    const infra::ParkTimeout a = avi_.park_ms();
    const infra::ParkTimeout b = writer_.park_ms();
    if (a == infra::kForever)
        return b;
    return b == infra::kForever ? a : std::min(a, b);
}

}
