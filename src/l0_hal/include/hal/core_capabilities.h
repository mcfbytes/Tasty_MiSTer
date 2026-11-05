// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/types.h"

namespace mister::hal {

struct CoreCapabilities {
    Width width = Width::Byte;
    IoVersion io_version{};
    bool board_digital = false;
    bool button_osd = false;
    bool button_user = false;
    std::uint8_t free_caps = 0;
    bool lw_directory = false;
};

}
