// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "svc/types.h"

namespace mister::svc {

enum class RumbleApply : std::uint8_t { NoDevice, Applied, Failed };

class IRumbleOut {
public:
    [[nodiscard]] virtual RumblePadSet rumble_pads() const noexcept = 0;
    [[nodiscard]] virtual RumbleApply rumble(PlayerIndex player, std::uint16_t strong,
                                             std::uint16_t weak) noexcept = 0;

protected:
    IRumbleOut() = default;
    IRumbleOut(const IRumbleOut&) = default;
    IRumbleOut& operator=(const IRumbleOut&) = default;
    ~IRumbleOut() = default;
};

}  // namespace mister::svc
