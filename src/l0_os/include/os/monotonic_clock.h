// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "os/clock.h"

namespace mister::os {

class MonotonicClock final : public IClock {
public:
    RawMonotonicClock::time_point now() const override;
};

}
