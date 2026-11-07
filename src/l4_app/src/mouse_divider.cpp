// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mouse_divider.h"

namespace mister::app {

namespace {
constexpr std::int64_t kMouseDividerNs = 15'000'000;
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
    const std::int64_t now = clock_.now().count();
    const bool due = (now - last_mouse_ns_) > kMouseDividerNs;
    if (!due && !md.button_edge)
        return std::nullopt;
    const Packet p{carry_, md.buttons};
    carry_ = {};
    last_mouse_ns_ = now;
    mouse_pending_ = false;
    return p;
}

}
