// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/cheat_engine.h"
#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "reactor/core_state.h"
#include "reactor/link_decoder.h"
#include "infra/seat.h"

namespace mister::cores {

namespace detail {

template <const CoreProfile& kProfile>
class CheatEngineRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    void service(reactor::CoreState& st) const override {
        TASTY_SEAT_BODY(CheatEngineRow);
        Core* c = bound_core(st);
        if (c == nullptr || &c->profile() != &kProfile)
            return;
        if (ICheatEngine* e = c->cheat_engine())
            e->service_cheat_tick();
    }
};

}

template <const CoreProfile& kProfile>
inline constexpr detail::CheatEngineRow<kProfile> kCheatEngineTick{};

}
