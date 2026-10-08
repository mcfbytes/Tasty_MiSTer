// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc_read_service.h"

namespace mister::svc {

struct UnboundDisc {
    DiscReadService::GeomCell geometry{};
    DiscReadService::CountCell counters{};
    DiscReadService service{{.geometry = geometry, .counters = counters}};
};

}  // namespace mister::svc
