// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/mra_facts.h"
#include "app/session_identity.h"
#include "cores/button_override.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/link_op.h"

namespace mister::app {

struct IdentityRecord {
    TASTY_SEAT_EXEMPT(component);
    using Stamp = proto::IdentityStamp;

    struct Inputs {

        bool identity_bound = false;

        std::array<std::string_view, 4> fields{};
        cores::ButtonOverride button_override{};
        bool gamepad_defaults = false;
        bool log_file_entry = false;
        Stamp stamp{};
    };

    SessionIdentity identity{};

    bool mgl_playable = true;
    std::uint32_t uart_default_baud = 115200;
    Stamp stamp{};
};

using IdentityRecordCell = xthread::Telemetry<IdentityRecord, SeatTag::Main>;

[[nodiscard]] constexpr IdentityRecord::Stamp next_identity_stamp(
    IdentityRecord::Stamp s
) noexcept {
    const auto v = static_cast<std::uint16_t>(s.v + 1u);
    return IdentityRecord::Stamp{v == 0 ? std::uint16_t{1} : v};
}

[[nodiscard]] IdentityRecord build_identity_record(
    const IdentityRecord::Inputs& in,
    const MraFacts& facts
) noexcept;

static_assert(sizeof(IdentityRecord) <= 2048, "the record is one bounded copy on T-RT");

}
