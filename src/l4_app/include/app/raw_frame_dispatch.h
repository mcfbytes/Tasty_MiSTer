// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/raw_frame_slot.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept RawFrameSink = requires(S& s, const RawFrameSlot& m) {
    s.on(std::declval<const RawFrameSlot::Open&>(), m.head);
    s.on(std::declval<const RawFrameSlot::Frame&>(), m.head);
    s.on(std::declval<const RawFrameSlot::Close&>(), m.head);
    s.misrouted(m);
};

using RawFrameRoutes = infra::AllRouted;

}
