// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/encode_main.h"

namespace mister::app {

void EncodeMain::start() noexcept {
    TASTY_SEAT_BODY(EncodeMain);
    loop_();
}

void EncodeMain::serve() noexcept {
    hasher_.serve();

    if (stopping() && hasher_.holding())
        delay_.sleep_for(std::chrono::milliseconds(FrameHasher::kHeldPollMs));
}

}  // namespace mister::app
