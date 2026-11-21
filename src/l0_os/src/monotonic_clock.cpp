// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/monotonic_clock.h"

#include "os/raw_monotonic_clock.h"

namespace mister::os {

RawMonotonicClock::time_point MonotonicClock::now() const {
    return RawMonotonicClock::now();
}

}
