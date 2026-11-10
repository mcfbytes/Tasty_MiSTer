// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/pcm_wire_rows.h"
#include "cores/role_row.h"
#include "infra/seat.h"

namespace mister::cores {

namespace detail {

struct PcmWireRole {
    TASTY_SEAT_RESIDENT(RT);

    void service(Core& c) const noexcept {
        if (const auto r = c.pcm_wire_rows())
            r->service_pcm_tick();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::RoleRow<kProfile, detail::PcmWireRole> kPcmWireTick{};

}
