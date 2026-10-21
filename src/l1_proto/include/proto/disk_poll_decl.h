// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"
#include "proto/disk_act.h"

namespace mister::proto {

struct DiskPollDecl {
    TASTY_SEAT_EXEMPT(component);
    std::uint16_t opcode = 0;
    std::uint8_t beats = 0;
    std::uint8_t head_byte = 0;
    std::uint8_t head_mask = 0;
    std::uint8_t pad_{};
};

[[nodiscard]] constexpr bool well_formed(const DiskPollDecl& d) noexcept {
    return d.opcode != 0
        && d.beats >= 1
        && d.beats <= kDiskStateBytes
        && d.head_byte < d.beats
        && d.head_mask != 0;
}

}
