// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/pcm_wire_rows.h"
#include "reactor/core_state.h"
#include "infra/seat.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace pcm_detail {

template <const CoreProfile& kProfile>
class WireTickRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    void service(reactor::CoreState& st) const override {
        TASTY_SEAT_BODY(WireTickRow);
        Core* c = bound_core(st);
        if (c == nullptr || &c->profile() != &kProfile) return;
        if (IPcmWireRows* r = c->pcm_wire_rows()) r->service_pcm_tick();
    }
};

}  // namespace pcm_detail

template <const CoreProfile& kProfile>
inline constexpr pcm_detail::WireTickRow<kProfile> kPcmWireTick{};

}  // namespace mister::cores
