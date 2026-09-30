// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/idle_blank.h"

namespace mister::app {

void IdleBlank::set_timeout_minutes(std::uint16_t m) noexcept {
    if (m == minutes_) return;
    minutes_ = m;
    armed_ = false;
}

std::optional<bool> IdleBlank::tick(std::int64_t now_ns, std::uint32_t activity,
                                    bool grabbed) noexcept {
    if (minutes_ == 0) {

        if (!blanked_) return std::nullopt;
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
        deadline_ns_ = now_ns + static_cast<std::int64_t>(minutes_) * 60'000'000'000LL;
        if (blanked_) {
            blanked_ = false;
            ++wakes_;
            return true;
        }
        return std::nullopt;
    }

    if (now_ns >= deadline_ns_ && !blanked_) {
        blanked_ = true;
        ++blanks_;
        return false;
    }
    return std::nullopt;
}

}  // namespace mister::app
