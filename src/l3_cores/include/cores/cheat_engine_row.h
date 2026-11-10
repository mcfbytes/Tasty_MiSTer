// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cheat_engine.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/role_row.h"
#include "infra/seat.h"

namespace mister::cores {

namespace detail {

struct CheatEngineRole {
    TASTY_SEAT_RESIDENT(RT);

    void service(Core& c) const noexcept {
        if (const auto r = c.cheat_engine())
            r->service_cheat_tick();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::RoleRow<kProfile, detail::CheatEngineRole> kCheatEngineTick{};

}
