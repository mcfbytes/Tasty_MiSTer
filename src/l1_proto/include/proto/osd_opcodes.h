// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "hal/types.h"

namespace mister::proto {

enum class OsdOp : std::uint16_t {
    Write = 0x20,
    WriteLatch = 0x28,
    Disable = 0x40,
    Enable = 0x41,
    EnableInfo = 0x45,
};

[[nodiscard]] constexpr hal::SpiWord word(OsdOp op) noexcept {
    return hal::SpiWord{static_cast<std::uint16_t>(op)};
}

[[nodiscard]] constexpr hal::SpiWord osd_word(OsdOp op, hal::SpiWord payload) noexcept {
    return word(op).with(payload);
}

static_assert(word(OsdOp::Write) == hal::SpiWord{0x20});
static_assert(word(OsdOp::WriteLatch) == hal::SpiWord{0x28});
static_assert(word(OsdOp::Disable) == hal::SpiWord{0x40});
static_assert(word(OsdOp::Enable) == hal::SpiWord{0x41});
static_assert(word(OsdOp::EnableInfo) == hal::SpiWord{0x45});

}
