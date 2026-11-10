// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/board_ops.h"

#include <unistd.h>

#include <chrono>

namespace mister::app {

using WholeMs = std::chrono::duration<unsigned, std::milli>;
using WholeUs = std::chrono::duration<::useconds_t, std::micro>;

void sleep_ms(infra::OptRef<ISleeper> sleeper, std::chrono::milliseconds ms) noexcept {
    if (sleeper) {
        sleeper->sleep_ms(std::chrono::duration_cast<WholeMs>(ms).count());
    } else {
        ::usleep(std::chrono::duration_cast<WholeUs>(ms).count());
    }
}

}
