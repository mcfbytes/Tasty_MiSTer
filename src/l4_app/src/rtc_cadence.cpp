// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rtc_cadence.h"

#include "proto/rtc_image.h"

namespace mister::app {

void RtcCadence::arm(std::chrono::seconds period) noexcept {
    TASTY_SEAT_BODY(RtcCadence);
    period_ = period;
    next_ = os::Deadline::immediate();
}

void RtcCadence::disarm() noexcept {
    TASTY_SEAT_BODY(RtcCadence);
    period_ = std::chrono::seconds{};
}

void RtcCadence::restart() noexcept {
    TASTY_SEAT_BODY(RtcCadence);
    next_ = os::Deadline::immediate();
}

std::optional<proto::LinkOp::SendRtc> RtcCadence::due(
    std::chrono::system_clock::time_point wall
) const noexcept {
    TASTY_SEAT_BODY(RtcCadence);
    if (period_ == std::chrono::seconds{} || !next_.expired(clock_))
        return std::nullopt;

    const auto image = proto::rtc_image_at(std::chrono::system_clock::to_time_t(wall));
    if (!image)
        return std::nullopt;
    return proto::LinkOp::SendRtc{.image = *image};
}

void RtcCadence::sent() noexcept {
    TASTY_SEAT_BODY(RtcCadence);
    next_ = os::Deadline::in(clock_, period_);
}

}
