// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/role_row.h"
#include "cores/sector_kick.h"
#include "infra/seat.h"

namespace mister::cores {

namespace detail {

struct SectorKickRole {
    TASTY_SEAT_RESIDENT(RT);

    void service(Core& c) const noexcept {
        if (const auto r = c.sector_kick())
            r->service_kick();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::RoleRow<kProfile, detail::SectorKickRole> kSectorKick{};

}
