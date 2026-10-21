// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/mouse_divider.h"

namespace mister::app {

namespace {
constexpr std::int64_t kMouseDividerNs = 15'000'000;
}

std::optional<MouseDivider::Packet> MouseDivider::flush() noexcept {
    const MouseDelta md = acc_.take();
    carry_x_ += md.dx;
    carry_y_ += md.dy;
    carry_z_ += md.dz;
    if (md.moved || md.button_edge)
        mouse_pending_ = true;
    if (!mouse_pending_)
        return std::nullopt;
    const std::int64_t now = clock_.now().count();
    const bool due = (now - last_mouse_ns_) > kMouseDividerNs;
    if (!due && !md.button_edge)
        return std::nullopt;
    const Packet p{carry_x_, carry_y_, carry_z_, md.buttons};
    carry_x_ = 0;
    carry_y_ = 0;
    carry_z_ = 0;
    last_mouse_ns_ = now;
    mouse_pending_ = false;
    return p;
}

}
