// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/disc/disc_read_service.h"

namespace mister::svc::disc {

struct UnboundDisc {
    DiscReadService::GeomCell geometry{};
    DiscReadService::CountCell counters{};
    DiscReadService service{{.geometry = geometry, .counters = counters}};
};

}
