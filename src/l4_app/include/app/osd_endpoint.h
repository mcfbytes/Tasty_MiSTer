// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <span>

#include "app/grant_endpoint.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "infra/prelude.h"
#include "infra/seat_owner_ui.h"

namespace mister::app {

using OsdEndpoint = GrantEndpoint<OsdGrant, OsdRoute, seat_owner::Ui>;

using OsdEndpoints = std::span<const std::reference_wrapper<OsdEndpoint>>;

}
