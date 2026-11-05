// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::os {
class IClock;
}

namespace mister::ui {

class Page;

class PageDeadlines final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::chrono::nanoseconds kStatusPeriod = std::chrono::seconds{1};

    void arm(infra::OptRef<const Page> top, infra::OptRef<const os::IClock> clock) noexcept;

    void disarm() noexcept { auto_pop_at_ = std::chrono::nanoseconds::zero(); }

    [[nodiscard]] bool fired(std::chrono::nanoseconds now) const noexcept;

    void consume_pop() noexcept {
        auto_pop_at_ = std::chrono::nanoseconds::zero();
        ++auto_pops_;
    }

    void rearm_help(std::chrono::nanoseconds now) noexcept { help_rearm_at_ = now; }

    [[nodiscard]] std::chrono::nanoseconds help_rearm_at() const noexcept { return help_rearm_at_; }

    [[nodiscard]] bool status_due(std::chrono::nanoseconds now) const noexcept {
        return now >= status_due_;
    }

    [[nodiscard]] bool take_status_tick(std::chrono::nanoseconds now) noexcept;

    [[nodiscard]] std::uint32_t auto_pops() const noexcept { return auto_pops_; }

    [[nodiscard]] std::uint32_t status_paints() const noexcept { return status_paints_; }

private:
    std::chrono::nanoseconds status_due_{};
    std::chrono::nanoseconds help_rearm_at_{};

    std::chrono::nanoseconds auto_pop_at_{};
    std::uint32_t auto_pops_ = 0;
    std::uint32_t status_paints_ = 0;
};

}
