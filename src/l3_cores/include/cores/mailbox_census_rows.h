// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_profile.h"
#include "cores/core_support.h"
#include "cores/mailbox_rows.h"
#include "infra/seat.h"
#include "reactor/core_state.h"
#include "reactor/link_decoder.h"

namespace mister::cores {

namespace mailbox_detail {

template <const CoreProfile& kProfile>
class TickRow final : public reactor::ILinkDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    void service(reactor::CoreState& st) const override {
        TASTY_SEAT_BODY(TickRow);
        Core* c = bound_core(st);
        if (c == nullptr || &c->profile() != &kProfile) return;
        if (IMailboxRows* r = c->mailbox_rows()) r->service_mailbox_tick();
    }
};

}  // namespace mailbox_detail

template <const CoreProfile& kProfile>
inline constexpr mailbox_detail::TickRow<kProfile> kMailboxTick{};

}  // namespace mister::cores
