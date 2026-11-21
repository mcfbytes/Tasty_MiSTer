// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "svc/input/device_match.h"
#include "svc/input/quirk_id.h"
#include "svc/types.h"

namespace mister::svc::input {

struct MouseFixupRule {
    DeviceMatch match;
    QuirkId quirk;
    const char* why;
};

std::optional<MouseFixupRule> find_mouse_fixup(VidPidKey device);

}
