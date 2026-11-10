// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/reboot_flag.h"
#include "hal/sdram_cfg.h"

namespace mister::hal {

struct HandoffPage {
    bool env = false;
    std::optional<SdramCfg> sdram;
    std::optional<std::uint8_t> altcfg;
    RebootFlag reboot{};
};

}
