// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "os/clock.h"

namespace mister::os {

class MonotonicClock final : public IClock {
public:
    std::chrono::nanoseconds now() const override;
};

}  // namespace mister::os
