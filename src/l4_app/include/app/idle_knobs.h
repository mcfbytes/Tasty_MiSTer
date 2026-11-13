// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

struct IdleKnobs {
    std::chrono::seconds osd_timeout{};
    std::chrono::seconds video_off{};
    bool video_off_logo = false;
    bool fb_terminal = false;

    friend constexpr bool operator==(IdleKnobs, IdleKnobs) = default;
};

using IdleKnobsCell = xthread::Telemetry<IdleKnobs, SeatTag::Main>;

}
