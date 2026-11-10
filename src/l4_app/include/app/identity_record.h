// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "app/mra_facts.h"
#include "app/session_identity.h"
#include "cores/button_override.h"
#include "cores/types.h"
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
    cores::BaudRate uart_default_baud{cores::kDefaultUartBaud};
    Stamp stamp{};
};

using IdentityRecordCell = xthread::Telemetry<IdentityRecord, SeatTag::Main>;

[[nodiscard]] IdentityRecord build_identity_record(
    const IdentityRecord::Inputs& in,
    const MraFacts& facts
) noexcept;

static_assert(sizeof(IdentityRecord) <= 2048, "the record is one bounded copy on T-RT");

}
