// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/mgl.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S>
concept MglItemSink = requires(S& s, const MglItem& m) {
    s.on(std::declval<const MglItem::Load&>());
    s.on(std::declval<const MglItem::Reset&>());
    s.misrouted(m);
};

using MglItemRoutes = infra::AllRouted;

}  // namespace mister::app
