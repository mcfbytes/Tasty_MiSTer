// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/frame_route.h"

namespace mister::proto::link {

void FrameRoute::bind(IFrameLane& lane) noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    lane_ = infra::OptRef<IFrameLane>{lane};
    if (suspended_) {
        lane.suspend();
    } else {
        lane.resume();
    }
}

void FrameRoute::unbind() noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    if (lane_)
        lane_->suspend();
    lane_.reset();
}

ILinkFrame& FrameRoute::frame() noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    spi_.abandon();
    if (off_link())
        return *lane_;
    return spi_;
}

void FrameRoute::settle(bool park_asked) noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    if (!off_link())
        return;
    lane_->settle();
    if (park_asked)
        lane_->abandon();
}

bool FrameRoute::busy() const noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    return off_link() && lane_->busy();
}

void FrameRoute::suspend() noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    if (suspended_)
        return;
    suspended_ = true;
    if (lane_)
        lane_->suspend();
}

void FrameRoute::resume() noexcept {
    TASTY_SEAT_BODY(FrameRoute);
    if (!suspended_)
        return;
    suspended_ = false;
    if (lane_)
        lane_->resume();
}

}
