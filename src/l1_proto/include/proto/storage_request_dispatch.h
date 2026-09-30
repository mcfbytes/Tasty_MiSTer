// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/message_sum.h"
#include "proto/storage_request.h"

namespace mister::proto {

template <class S, class... Ctx>
concept StorageRequestSink =
    requires(S& s, const StorageRequest& m, const StorageRequest::Head& h, Ctx&... c) {
        s.on(std::declval<const StorageRequest::Read&>(), h, c...);
        s.on(std::declval<const StorageRequest::Write&>(), h, c...);
        s.on(std::declval<const StorageRequest::Create&>(), h, c...);
        s.on(std::declval<const StorageRequest::Flush&>(), h, c...);
        s.on(std::declval<const StorageRequest::Attach&>(), h, c...);
        s.on(std::declval<const StorageRequest::Detach&>(), h, c...);
        s.misrouted(m, c...);
    };

using StorageRequestRoutes = infra::AllRouted;

}  // namespace mister::proto
