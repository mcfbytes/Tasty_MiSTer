// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/session_identity.h"
#include "infra/cell_gen.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"

namespace mister::app {

class IdentityLatch {
    TASTY_SEAT_MEDIATOR(RT, Ui);

public:
    IdentityLatch() noexcept = default;
    IdentityLatch(const IdentityLatch&) = delete;
    IdentityLatch& operator=(const IdentityLatch&) = delete;

    void publish(const SessionIdentity& id) noexcept;

    [[nodiscard]] std::optional<SessionIdentity> copy() const noexcept;

    [[nodiscard]] infra::CellGen generation() const noexcept { return cell_.generation(); }

private:
    xthread::Telemetry<SessionIdentity, SeatTag::RT> cell_{};
};

}
