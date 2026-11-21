// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "app/core_name.h"
#include "app/game_id.h"
#include "app/remembered_path.h"
#include "cores/file_slot.h"
#include "cores/host_policy.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "proto/rotation_dir.h"
#include "svc/input/joy_plan.h"

namespace mister::app {

struct SessionIdentity {

    CoreName core{};

    CoreName conf_name{};

    RememberedStem stem{};

    proto::RotationDir rotation{};

    bool setname_same_dir = false;
    svc::input::JoyPlan joy{};

    static constexpr std::size_t kJNamesCap = 1024;
    FixedStr<kJNamesCap, StrFit::Clip> j_names{};

    infra::OptRef<const cores::HostPolicy> host{};

    std::span<const cores::FileSlot> slots{};

    GameId game_id{};

    [[nodiscard]] constexpr const cores::HostPolicy& policy() const noexcept {
        return cores::policy_of(host);
    }
};

[[nodiscard]] inline std::string_view home_name(const SessionIdentity& id) noexcept {
    return id.setname_same_dir ? id.conf_name.view() : id.core.view();
}

}
