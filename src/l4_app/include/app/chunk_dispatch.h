// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/chunk_slot.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept ChunkSink = requires(S& s, const ChunkSlot& m) {
    s.on(std::declval<const ChunkSlot::Open&>(), m.head);
    s.on(std::declval<const ChunkSlot::Data&>(), m.head);
    s.on(std::declval<const ChunkSlot::Close&>(), m.head);
    s.on(std::declval<const ChunkSlot::Refuse&>(), m.head);
    s.misrouted(m);
};

using ChunkRoutes = infra::AllRouted;

}
