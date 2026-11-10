// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/idle_blank.h"

#include <chrono>

namespace mister::app {

void IdleBlank::set_timeout_minutes(std::chrono::minutes m) noexcept {
    if (m == minutes_)
        return;
    minutes_ = m;
    armed_ = false;
}

std::optional<bool> IdleBlank::tick(
    os::RawMonotonicClock::time_point now,
    std::uint32_t activity,
    bool grabbed
) noexcept {
    if (minutes_ == std::chrono::minutes{}) {

        if (!blanked_)
            return std::nullopt;
        blanked_ = false;
        armed_ = false;
        ++wakes_;
        return true;
    }

    const bool moved = !seen_ || activity != last_activity_;
    last_activity_ = activity;
    seen_ = true;

    if (!armed_ || moved || !grabbed) {
        armed_ = true;
        deadline_ = now + minutes_;
        if (blanked_) {
            blanked_ = false;
            ++wakes_;
            return true;
        }
        return std::nullopt;
    }

    if (now >= deadline_ && !blanked_) {
        blanked_ = true;
        ++blanks_;
        return false;
    }
    return std::nullopt;
}

}
