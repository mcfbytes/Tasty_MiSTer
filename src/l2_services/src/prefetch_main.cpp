// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/prefetch_main.h"

namespace mister::svc {

void PrefetchMain::start() noexcept {
    TASTY_SEAT_BODY(PrefetchMain);
    loop_();
}

void PrefetchMain::serve() noexcept {
    if (stopping()) return;
    prefetch_.serve();
}

}  // namespace mister::svc
