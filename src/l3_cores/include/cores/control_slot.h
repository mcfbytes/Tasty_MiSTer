// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::cores {

struct ControlSlot {

    enum class Root : std::uint8_t { CoreHome, SharedFolder };

    enum class Announce : std::uint8_t { Always, WhenDirectory };

    proto::SlotIndex slot{};
    Root root = Root::CoreHome;
    std::string_view subdir{};
    Announce announce = Announce::Always;
};

}
