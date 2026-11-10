// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

namespace mister::os {

class RawMonotonicClock {
public:
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<RawMonotonicClock, duration>;

    static constexpr bool is_steady = true;

    [[nodiscard]] static time_point now() noexcept;
};

}
