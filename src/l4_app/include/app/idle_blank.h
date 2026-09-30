// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MISTER_APP_IDLE_BLANK_H
#define MISTER_APP_IDLE_BLANK_H

#include <cstdint>
#include <optional>
#include "infra/seat.h"

namespace mister::app {

class IdleBlank {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void set_timeout_minutes(std::uint16_t m) noexcept;

    std::optional<bool> tick(std::int64_t now_ns, std::uint32_t activity, bool grabbed) noexcept;

    bool blanked() const noexcept { return blanked_; }
    bool enabled() const noexcept { return minutes_ != 0; }
    std::uint32_t blanks() const noexcept { return blanks_; }
    std::uint32_t wakes() const noexcept { return wakes_; }

private:
    std::int64_t deadline_ns_ = 0;
    std::uint32_t last_activity_ = 0;
    std::uint32_t blanks_ = 0;
    std::uint32_t wakes_ = 0;
    std::uint16_t minutes_ = 0;
    bool armed_ = false;
    bool blanked_ = false;
    bool seen_ = false;
};

}  // namespace mister::app

#endif
