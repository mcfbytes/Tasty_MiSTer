// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "hal/handoff_page.h"

namespace mister::hal {

class IBootHandoff {
public:
    virtual ~IBootHandoff() = default;

    [[nodiscard]] virtual Ex<HandoffPage> handoff() = 0;

    [[nodiscard]] virtual Ex<void> write_env(std::string_view core_name,
                                             std::span<const std::byte> cfg) = 0;
    [[nodiscard]] virtual Ex<void> write_sdram_cfg(std::uint16_t cfg) = 0;
    [[nodiscard]] virtual Ex<void> write_altcfg(std::uint8_t alt) = 0;
    [[nodiscard]] virtual Ex<void> write_reboot_flag(bool warm) = 0;

protected:
    IBootHandoff() = default;
    IBootHandoff(const IBootHandoff&) = default;
    IBootHandoff& operator=(const IBootHandoff&) = default;
};

}  // namespace mister::hal
