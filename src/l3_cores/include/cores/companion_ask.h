// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "os/types.h"

namespace mister::cores {

struct CompanionAsk {
    std::string_view path{};
    bool mailbox_live = false;
    os::PhysAddr aperture_base{};
};

}
