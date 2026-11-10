// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "os/delay.h"

namespace mister::os {

class NanosleepDelay final : public IDelay {
public:
    void sleep_for(std::chrono::microseconds d) override;
    void sleep_until(RawMonotonicClock::time_point at) override;
};

}
