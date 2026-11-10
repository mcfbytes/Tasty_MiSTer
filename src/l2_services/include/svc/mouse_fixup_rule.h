// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "svc/device_match.h"
#include "svc/quirk_id.h"
#include "svc/types.h"

namespace mister::svc {

struct MouseFixupRule {
    DeviceMatch match;
    QuirkId quirk;
    const char* why;
};

std::optional<MouseFixupRule> find_mouse_fixup(Vid vid, Pid pid);

}
