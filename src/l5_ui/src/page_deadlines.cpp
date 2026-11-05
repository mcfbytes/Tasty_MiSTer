// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/page_deadlines.h"

#include "os/clock.h"
#include "ui/page.h"

namespace mister::ui {

void PageDeadlines::arm(
    infra::OptRef<const Page> top,
    infra::OptRef<const os::IClock> clock
) noexcept {
    TASTY_SEAT_BODY(PageDeadlines);
    auto_pop_at_ = std::chrono::nanoseconds::zero();
    if (!top || !clock)
        return;
    const std::chrono::milliseconds pop = top->facts().auto_pop;
    if (pop.count() == 0)
        return;
    auto_pop_at_ = clock->now() + std::chrono::nanoseconds{pop};
}

bool PageDeadlines::fired(std::chrono::nanoseconds now) const noexcept {
    TASTY_SEAT_BODY(PageDeadlines);
    return auto_pop_at_ != std::chrono::nanoseconds::zero() && now >= auto_pop_at_;
}

bool PageDeadlines::take_status_tick(std::chrono::nanoseconds now) noexcept {
    TASTY_SEAT_BODY(PageDeadlines);
    if (now < status_due_)
        return false;
    status_due_ = now + kStatusPeriod;
    ++status_paints_;
    return true;
}

}
