// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "cores/cheat_lookup.h"
#include "cores/input_sink_id.h"
#include "cores/key_policy.h"
#include "cores/pause_on_osd.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/input/stick_reach.h"

namespace mister::svc::input {
class IAnalogReshape;
}

namespace mister::cores {

struct HostPolicy {
    TASTY_SEAT_EXEMPT(const_shared);

    PauseOnOsd pause = PauseOnOsd::Option;

    bool suppress_analog_followup = false;

    infra::OptRef<const svc::input::IAnalogReshape> analog_reshape{};

    svc::input::StickReach stick_reach = svc::input::StickReach::Symmetric;

    bool suppress_stick_dpad = false;

    bool front_end = false;

    std::string_view cue_browse_dir{};

    bool image_rows_no_zip = false;

    CheatLookup cheats{};

    bool forces_gui_f12 = false;

    InputSinkId input_sink = InputSinkId::None;

    KeyPolicy keys{};
};

inline constexpr HostPolicy kNoHostPolicy{};

[[nodiscard]] constexpr const HostPolicy& policy_of(
    infra::OptRef<const HostPolicy> borrow
) noexcept {
    return borrow ? *borrow : kNoHostPolicy;
}

}
