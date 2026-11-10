// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "os/raw_monotonic_clock.h"

namespace mister::os {

class IDelay {
public:
    virtual ~IDelay() = default;

    virtual void sleep_for(std::chrono::microseconds d) = 0;

    virtual void sleep_until(RawMonotonicClock::time_point at) = 0;

protected:
    IDelay() = default;
    IDelay(const IDelay&) = default;
    IDelay& operator=(const IDelay&) = default;
};

}
