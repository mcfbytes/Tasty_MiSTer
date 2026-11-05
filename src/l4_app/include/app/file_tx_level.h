// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "proto/types.h"

namespace mister::app {

struct FileTxLevel {
    TASTY_SEAT_EXEMPT(const_shared);
    proto::ActKey act{};
    bool open = false;
    bool payload = false;
    bool stream_served = false;
    std::uint8_t pad_{};
    proto::ActKey stream_act{};
    proto::ActKey held_act{};
    std::uint32_t held_bytes = 0;

    [[nodiscard]] constexpr bool answers(proto::ActKey key) const noexcept {
        if (held_act == key)
            return true;
        return static_cast<bool>(stream_act) && stream_act.at_or_past(key);
    }
};

using FileTxLevelCell = xthread::Telemetry<FileTxLevel, SeatTag::RT>;

}
