// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "app/session_identity.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

class IdentityLatch {
    TASTY_SEAT_MEDIATOR(RT, Ui);

public:
    IdentityLatch() noexcept = default;
    IdentityLatch(const IdentityLatch&) = delete;
    IdentityLatch& operator=(const IdentityLatch&) = delete;

    void publish(const SessionIdentity& id) noexcept;

    [[nodiscard]] std::optional<SessionIdentity> copy() const noexcept;

    [[nodiscard]] std::uint32_t generation() const noexcept { return cell_.generation(); }

private:
    xthread::Telemetry<SessionIdentity, SeatTag::RT> cell_{};
};

}
