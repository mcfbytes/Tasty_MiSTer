// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "reactor/core_state.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace detail {

template <const CoreProfile& kProfile, class Traits>
class RoleRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    void service(reactor::CoreState& st) const override {
        TASTY_SEAT_BODY(RoleRow);
        const infra::OptRef<Core> c = bound_core<kProfile>(st);
        if (!c)
            return;
        traits_.service(*c);
    }

private:
    Traits traits_{};
};

}

}
