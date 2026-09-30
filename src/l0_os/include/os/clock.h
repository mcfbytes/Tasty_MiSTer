// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

namespace mister::os {

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::nanoseconds now() const = 0;
};

}  // namespace mister::os
