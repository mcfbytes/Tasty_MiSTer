// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input_decode.h"
#include "infra/seat.h"
#include "infra/seat_park.h"

namespace mister::app {

class InputPark {
    TASTY_SEAT_RESIDENT(Input);

public:
    InputPark(InputDecode& decode, xthread::WakeFlag& pause_wake) noexcept
        : decode_(decode), pause_wake_(pause_wake) {}
    void arm() noexcept { pause_wake_.arm(); }
    void wait(int ms) noexcept { decode_.wait_events(ms); }
    void disarm() noexcept { pause_wake_.disarm(); }

private:
    InputDecode& decode_;
    xthread::WakeFlag& pause_wake_;
};

static_assert(xthread::ParkLike<InputPark>);

}  // namespace mister::app
