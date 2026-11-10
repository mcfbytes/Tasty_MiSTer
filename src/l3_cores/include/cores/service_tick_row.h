// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cd_service_rows.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/role_row.h"
#include "infra/seat.h"

namespace mister::cores {

namespace detail {

struct CdServiceRole {
    TASTY_SEAT_RESIDENT(RT);

    void service(Core& c) const noexcept {
        if (const auto r = c.cd_service_rows())
            r->service_tick();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::RoleRow<kProfile, detail::CdServiceRole> kCdServiceTick{};

}
