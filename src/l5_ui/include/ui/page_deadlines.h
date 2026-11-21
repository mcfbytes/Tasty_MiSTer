// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/deadline.h"

namespace mister::ui {

class Page;

class PageDeadlines final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::chrono::nanoseconds kStatusPeriod = std::chrono::seconds{1};

    void arm(infra::OptRef<const Page> top, infra::OptRef<const os::IClock> clock) noexcept;

    void disarm() noexcept { auto_pop_at_.reset(); }

    [[nodiscard]] bool fired(os::RawMonotonicClock::time_point now) const noexcept;

    void consume_pop() noexcept {
        auto_pop_at_.reset();
        ++auto_pops_;
    }

    void rearm_help(os::RawMonotonicClock::time_point now) noexcept { help_rearm_at_ = now; }

    [[nodiscard]] os::RawMonotonicClock::time_point help_rearm_at() const noexcept {
        return help_rearm_at_;
    }

    [[nodiscard]] bool status_due(os::RawMonotonicClock::time_point now) const noexcept {
        return status_due_.expired_at(now);
    }

    [[nodiscard]] bool take_status_tick(os::RawMonotonicClock::time_point now) noexcept;

    [[nodiscard]] std::uint32_t auto_pops() const noexcept { return auto_pops_; }

    [[nodiscard]] std::uint32_t status_paints() const noexcept { return status_paints_; }

private:
    os::Deadline status_due_{os::Deadline::immediate()};
    os::RawMonotonicClock::time_point help_rearm_at_{};

    std::optional<os::Deadline> auto_pop_at_{};
    std::uint32_t auto_pops_ = 0;
    std::uint32_t status_paints_ = 0;
};

}
