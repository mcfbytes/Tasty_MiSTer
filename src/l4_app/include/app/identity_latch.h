// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/session_identity.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::svc {
class IAnalogReshape;
}

namespace mister::app {

class IdentityLatch {
    TASTY_SEAT_MEDIATOR(RT, Ui);

public:
    IdentityLatch() noexcept = default;
    IdentityLatch(const IdentityLatch&) = delete;
    IdentityLatch& operator=(const IdentityLatch&) = delete;

    void publish(std::string_view core, std::string_view rbf, const svc::JoyPlan& joy,
                 std::string_view j_names, bool front_end, bool suppress_analog_followup,
                 const svc::IAnalogReshape* analog_reshape, const char* cue_dir,
                 const cores::CheatLookup* cheats, std::span<const cores::FileSlot> slots,
                 const GameId& game_id = {}, bool setname_same_dir = false,
                 bool image_no_zip = false) noexcept;

    [[nodiscard]] bool copy(SessionIdentity& out) const noexcept;

    [[nodiscard]] std::uint32_t generation() const noexcept { return cell_.generation(); }

private:
    xthread::Telemetry<SessionIdentity> cell_{};

    SessionIdentity scratch_{};
};

}  // namespace mister::app
