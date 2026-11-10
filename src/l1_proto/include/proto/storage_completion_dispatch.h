// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/message_sum.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::proto {

template <class S, class... Ctx>
concept StorageCompletionSink = requires(
    S& s,
    const StorageCompletion& m,
    const StorageCompletion::Head& h,
    Ctx&... c
) {
    s.on(std::declval<const StorageCompletion::Read&>(), h, c...);
    s.on(std::declval<const StorageCompletion::Write&>(), h, c...);
    s.on(std::declval<const StorageCompletion::Create&>(), h, c...);
    s.on(std::declval<const StorageCompletion::Flush&>(), h, c...);
    s.on(std::declval<const StorageCompletion::Attach&>(), h, c...);
    s.on(std::declval<const StorageCompletion::Detach&>(), h, c...);
    s.misrouted(m, c...);
};

using StorageCompletionRoutes = infra::AllRouted;

}
