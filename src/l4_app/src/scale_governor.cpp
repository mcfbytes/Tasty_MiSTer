// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scale_governor.h"

namespace mister::app {

void ScaleGovernor::restart(RecScale requested) noexcept {
    TASTY_SEAT_BODY(ScaleGovernor);
    auto_ = requested == RecScale::Auto;
    scale_ = requested == RecScale::Half ? PictureScale::Half : PictureScale::Native;
    cause_ = Cause::None;
    fell_behind_ = false;
    calm_ = std::chrono::nanoseconds{};
    returned_.reset();
    win_start_.reset();
    win_cpu_ = std::chrono::nanoseconds{};
    win_over_ = Count{};
    steps_ = Count{};
    recovered_ = Count{};
}

void ScaleGovernor::step_(bool behind, std::chrono::nanoseconds now) noexcept {
    scale_ = PictureScale::Half;
    if (!steps_.at_max())
        steps_ = steps_.next();
    fell_behind_ = behind;
    cause_ = behind && !too_soon_(now) ? Cause::Queue : Cause::Held;
    calm_ = now;
}

bool ScaleGovernor::too_soon_(std::chrono::nanoseconds now) const noexcept {
    return returned_ && now - *returned_ < lim_.budget_window * kSustainWindows;
}

ScaleGovernor::Change ScaleGovernor::on_encoded(
    std::chrono::nanoseconds cpu,
    std::chrono::nanoseconds now
) noexcept {
    TASTY_SEAT_BODY(ScaleGovernor);
    constexpr std::int64_t kPercent = 100;
    Change out{};
    if (!win_start_) {
        win_start_ = now;
        win_cpu_ = std::chrono::nanoseconds{};
    }
    win_cpu_ += cpu;
    const std::chrono::nanoseconds span = now - *win_start_;
    if (span < lim_.budget_window || span <= std::chrono::nanoseconds{})
        return out;
    out.budget_pct = Percent::measured(
        static_cast<std::uint32_t>(win_cpu_.count() * kPercent / span.count())
    );
    out.measured = true;
    if (out.budget_pct > lim_.budget_pct) {
        if (!win_over_.reached(Count{kSustainWindows}))
            win_over_ = win_over_.next();
    } else {
        win_over_ = Count{};
    }
    if (auto_ && scale_ == PictureScale::Native && win_over_.reached(Count{kSustainWindows})) {
        step_(false, now);
        out.stepped = true;
        out.changed = true;
    }
    win_start_ = now;
    win_cpu_ = std::chrono::nanoseconds{};
    return out;
}

ScaleGovernor::Change ScaleGovernor::on_queue(
    QueueCount queued,
    QueueCount depth,
    bool held,
    std::chrono::nanoseconds now
) noexcept {
    TASTY_SEAT_BODY(ScaleGovernor);
    Change out{};
    if (auto_ && !held && scale_ == PictureScale::Native && queued.fills(depth)) {
        step_(true, now);
        out.stepped = true;
        out.changed = true;
    }
    if (scale_ != PictureScale::Half || cause_ != Cause::Queue)
        return out;
    if (!queued.caught_up_to(depth)) {
        calm_ = now;
        return out;
    }
    const Count cap{kRecoverDoublings};
    const Count doublings = recovered_ < cap ? recovered_ : cap;
    if (now - calm_ < doublings.doublings_of(lim_.recover))
        return out;
    scale_ = PictureScale::Native;
    cause_ = Cause::None;
    returned_ = now;
    if (!recovered_.at_max())
        recovered_ = recovered_.next();
    out.recovered = true;
    out.changed = true;

    win_start_.reset();
    win_over_ = Count{};
    return out;
}

}
