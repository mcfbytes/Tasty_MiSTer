// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "reactor/deadline_miss.h"

namespace mister::reactor {

class IDeadlineMissSink {
public:
    virtual void on(const DeadlineMiss& miss, std::chrono::nanoseconds wake) noexcept = 0;

protected:
    IDeadlineMissSink() = default;
    IDeadlineMissSink(const IDeadlineMissSink&) = default;
    IDeadlineMissSink& operator=(const IDeadlineMissSink&) = default;
    ~IDeadlineMissSink() = default;
};

}
