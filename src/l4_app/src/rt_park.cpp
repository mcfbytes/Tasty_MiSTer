// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rt_park.h"

#include "reactor/executive.h"

namespace mister::app {

void RtPark::wait(int ms) noexcept {
    (void)ms;
    exec_.block();
}

}  // namespace mister::app
