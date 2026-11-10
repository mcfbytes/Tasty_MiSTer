// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mouse_divider.h"

#include <chrono>

#include "proto/mouse_delta.h"

namespace mister::app {

namespace {
constexpr std::chrono::milliseconds kMouseDivider{15};
}

std::optional<MouseDivider::Packet> MouseDivider::flush() noexcept {
    const MouseDelta md = acc_.take();
    carry_.dx += md.motion.dx;
    carry_.dy += md.motion.dy;
    carry_.wheel += md.motion.wheel;
    if (md.moved || md.button_edge)
        mouse_pending_ = true;
    if (!mouse_pending_)
        return std::nullopt;
    const os::RawMonotonicClock::time_point now = clock_.now();
    const bool due = (now - last_mouse_) > kMouseDivider;
    if (!due && !md.button_edge)
        return std::nullopt;
    const Packet p{carry_, md.buttons};
    carry_ = {};
    last_mouse_ = now;
    mouse_pending_ = false;
    return p;
}

}
