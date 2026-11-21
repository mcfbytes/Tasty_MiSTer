// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/input_decode.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/seat_park.h"

namespace mister::app::input {

class InputPark {
    TASTY_SEAT_RESIDENT(Input);

public:
    InputPark(InputDecode& decode, xthread::WakeFlag& pause_wake) noexcept
        : decode_(decode),
          pause_wake_(pause_wake) {}

    void arm() noexcept { pause_wake_.arm(); }

    void wait(infra::ParkTimeout ms) noexcept { decode_.wait_events(ms); }

    void disarm() noexcept { pause_wake_.disarm(); }

private:
    InputDecode& decode_;
    xthread::WakeFlag& pause_wake_;
};

static_assert(xthread::ParkLike<InputPark>);

}
