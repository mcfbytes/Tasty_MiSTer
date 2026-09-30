// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/file_stream_service.h"
#include "infra/message_sum.h"

namespace mister::app {

template <class S, class... C>
concept StreamAskSink =
    requires(S& s, const FileStreamSlot::Ask& m, const FileStreamSlot::Ask::Head& h, C&... c) {
        s.on(std::declval<const FileStreamSlot::Ask::Open&>(), h, c...);
        s.on(std::declval<const FileStreamSlot::Ask::Read&>(), h, c...);
        s.on(std::declval<const FileStreamSlot::Ask::Write&>(), h, c...);
        s.on(std::declval<const FileStreamSlot::Ask::Sync&>(), h, c...);
        s.on(std::declval<const FileStreamSlot::Ask::Close&>(), h, c...);
        s.misrouted(m, c...);
    };

using StreamAskRoutes = infra::AllRouted;

}  // namespace mister::app
