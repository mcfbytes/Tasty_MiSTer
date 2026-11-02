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
    SlotIndex i,
    IBlockRoles& r
) {
    s.on(std::declval<const StorageCompletion::Read&>(), h, i, r);
    s.on(std::declval<const StorageCompletion::Write&>(), h, i, r);
    s.on(std::declval<const StorageCompletion::Create&>(), h, i, r);
    s.on(std::declval<const StorageCompletion::Flush&>(), h, i, r);
    s.on(std::declval<const StorageCompletion::Attach&>(), h, i, r);
    s.on(std::declval<const StorageCompletion::Detach&>(), h, i, r);
    s.misrouted(m, i, r);
};

using StorageCompletionRoutes = infra::AllRouted;

}
