// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/raw_monotonic_clock.h"

namespace mister::os {

class IClock {
public:
    virtual ~IClock() = default;

    virtual RawMonotonicClock::time_point now() const = 0;
};

}
