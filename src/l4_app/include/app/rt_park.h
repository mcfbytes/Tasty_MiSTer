// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/park_timeout.h"
#include "infra/seat.h"

namespace mister::reactor {
class Executive;
}

namespace mister::app {

class RtPark {
    TASTY_SEAT_RESIDENT(RT);

public:
    explicit RtPark(reactor::Executive& exec) noexcept : exec_(exec) {}

    RtPark(const RtPark&) = delete;
    RtPark& operator=(const RtPark&) = delete;
    RtPark(RtPark&&) = delete;
    RtPark& operator=(RtPark&&) = delete;

    void arm() noexcept {}

    void disarm() noexcept {}

    void wait(infra::ParkTimeout ms) noexcept;

private:
    reactor::Executive& exec_;
};

}
