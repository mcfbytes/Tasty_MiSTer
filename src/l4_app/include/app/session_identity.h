// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "app/game_id.h"
#include "app/remembered_path.h"
#include "cores/cheat_lookup.h"
#include "cores/file_slot.h"
#include "infra/fixed_str.h"
#include "svc/joy_plan.h"

namespace mister::svc {
class IAnalogReshape;
}

namespace mister::app {

struct SessionIdentity {
    static constexpr std::size_t kMax = 64;

    char core[kMax] = {};

    char rbf[kMax] = {};

    RememberedStem stem{};

    bool setname_same_dir = false;
    svc::JoyPlan joy{};

    static constexpr std::size_t kJNamesCap = 1024;
    FixedStr<kJNamesCap, StrFit::Clip> j_names{};

    bool front_end = false;

    bool suppress_analog_followup = false;

    const svc::IAnalogReshape* analog_reshape = nullptr;

    const char* cue_dir = nullptr;

    const cores::CheatLookup* cheats = nullptr;

    std::span<const cores::FileSlot> slots{};

    bool image_no_zip = false;

    GameId game_id{};
};

[[nodiscard]] inline std::string_view home_name(const SessionIdentity& id) noexcept {
    return id.setname_same_dir ? std::string_view{id.rbf} : std::string_view{id.core};
}

static_assert(RememberedStem::kMax == SessionIdentity::kMax, "the stem clips as `core` does");

}  // namespace mister::app
