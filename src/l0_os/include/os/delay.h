// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

namespace mister::os {

class IDelay {
public:
    virtual ~IDelay() = default;

    virtual void sleep_for(std::chrono::microseconds d) = 0;

    virtual void sleep_until(std::chrono::nanoseconds at) = 0;

protected:
    IDelay() = default;
    IDelay(const IDelay&) = default;
    IDelay& operator=(const IDelay&) = default;
};

}  // namespace mister::os
