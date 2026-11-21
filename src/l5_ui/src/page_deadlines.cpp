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
    auto_pop_at_.reset();
    if (!top || !clock)
        return;
    const std::chrono::milliseconds pop = top->facts().auto_pop;
    if (pop.count() == 0)
        return;
    auto_pop_at_ = os::Deadline::at_point(clock->now() + std::chrono::nanoseconds{pop});
}

bool PageDeadlines::fired(os::RawMonotonicClock::time_point now) const noexcept {
    TASTY_SEAT_BODY(PageDeadlines);
    return auto_pop_at_.has_value() && auto_pop_at_->expired_at(now);
}

bool PageDeadlines::take_status_tick(os::RawMonotonicClock::time_point now) noexcept {
    TASTY_SEAT_BODY(PageDeadlines);
    if (!status_due_.expired_at(now))
        return false;
    status_due_ = os::Deadline::at_point(now + kStatusPeriod);
    ++status_paints_;
    return true;
}

}
