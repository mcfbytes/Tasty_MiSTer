// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_service_rows.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/role_row.h"
#include "cores/sector_kick_row.h"
#include "cores/service_tick_row.h"
#include "infra/seat.h"
#include "reactor/link_decoder_decl.h"

namespace mister::cores {

namespace detail {

struct CdCommandEdgeRole {
    TASTY_SEAT_RESIDENT(RT);

    void service(Core& c) const noexcept {
        if (const auto r = c.cd_service_rows())
            r->service_command_edge();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::RoleRow<kProfile, detail::CdCommandEdgeRole> kCdCommandEdge{};

}
