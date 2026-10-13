// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct FileTxLevel {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint32_t act = 0;
    bool open = false;
    bool payload = false;
    bool stream_served = false;
    std::uint8_t pad_{};
    std::uint32_t stream_act = 0;
    std::uint32_t held_act = 0;
    std::uint32_t held_bytes = 0;

    [[nodiscard]] constexpr bool answers(std::uint32_t key) const noexcept {
        if (held_act == key) return true;
        return stream_act != 0 && static_cast<std::int32_t>(stream_act - key) >= 0;
    }
};

using FileTxLevelCell = xthread::Telemetry<FileTxLevel, SeatTag::RT>;

}  // namespace mister::app
