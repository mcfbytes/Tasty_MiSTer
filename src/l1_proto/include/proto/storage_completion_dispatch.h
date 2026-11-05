// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/message_sum.h"
#include "proto/block_roles.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::proto {

template <class S>
concept StorageCompletionSink = requires(
    S& s,
    const StorageCompletion& m,
    const StorageCompletion::Head& h,
    IBlockRoles& r
) {
    s.on(std::declval<const StorageCompletion::Read&>(), h, r);
    s.on(std::declval<const StorageCompletion::Write&>(), h, r);
    s.on(std::declval<const StorageCompletion::Create&>(), h, r);
    s.on(std::declval<const StorageCompletion::Flush&>(), h, r);
    s.on(std::declval<const StorageCompletion::Attach&>(), h, r);
    s.on(std::declval<const StorageCompletion::Detach&>(), h, r);
    s.misrouted(m, r);
};

using StorageCompletionRoutes = infra::AllRouted;

}
