// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/indexed_array.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto::link {

inline constexpr std::uint8_t kPadPorts = 6;
inline constexpr std::array<UioOp, kPadPorts> kPadDigitalOpcode{
    UioOp::Joystick0,
    UioOp::Joystick1,
    UioOp::Joystick2,
    UioOp::Joystick3,
    UioOp::Joystick4,
    UioOp::Joystick5
};

[[nodiscard]] constexpr UioOp digital_opcode(PlayerIndex port) noexcept {
    return infra::at_index(kPadDigitalOpcode, port);
}

}
