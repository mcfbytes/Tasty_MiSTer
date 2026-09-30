// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/message_sum.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::proto {

template <class S>
concept StorageCompletionSink =
    requires(S& s, const StorageCompletion& m, const StorageCompletion::Head& h, SlotIndex i) {
        s.on(std::declval<const StorageCompletion::Read&>(), h, i);
        s.on(std::declval<const StorageCompletion::Write&>(), h, i);
        s.on(std::declval<const StorageCompletion::Create&>(), h, i);
        s.on(std::declval<const StorageCompletion::Flush&>(), h, i);
        s.on(std::declval<const StorageCompletion::Attach&>(), h, i);
        s.on(std::declval<const StorageCompletion::Detach&>(), h, i);
        s.misrouted(m, i);
    };

using StorageCompletionRoutes = infra::AllRouted;

}  // namespace mister::proto
