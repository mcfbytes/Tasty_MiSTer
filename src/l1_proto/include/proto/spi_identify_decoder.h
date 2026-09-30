// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"
#include "infra/seat.h"
#include "hal/core_signals.h"
#include "proto/core_session.h"
#include "proto/link_event.h"
#include "proto/link_port.h"
#include "proto/link_router.h"
#include "proto/types.h"

namespace mister::proto {

class SpiIdentifyDecoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    using Port = LinkPort<LinkEvent::IdentityMatched, LinkEvent::IdentityMismatch>;

    SpiIdentifyDecoder(hal::ICoreSignals& signals, CoreSession& session, ILinkRouter& router,
                       const BindGeneration& gen) noexcept
        : signals_(&signals), session_(&session), gen_(&gen), out_(router) {}

    void service() noexcept;

    [[nodiscard]] bool active() const noexcept { return !session_->negotiating(); }

private:
    void refuse_(const Error& e, bool at_accept) noexcept;

    hal::ICoreSignals* signals_;
    CoreSession* session_;
    const BindGeneration* gen_;
    Port out_;
};

}  // namespace mister::proto
