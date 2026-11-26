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

[[nodiscard]] constexpr hal::LinkWord word(OsdOp op) noexcept {
    return hal::LinkWord{static_cast<std::uint16_t>(op)};
}

[[nodiscard]] constexpr hal::LinkWord osd_word(OsdOp op, hal::LinkWord payload) noexcept {
    return word(op).with(payload);
}

static_assert(word(OsdOp::Write) == hal::LinkWord{0x20});
static_assert(word(OsdOp::WriteLatch) == hal::LinkWord{0x28});
static_assert(word(OsdOp::Disable) == hal::LinkWord{0x40});
static_assert(word(OsdOp::Enable) == hal::LinkWord{0x41});
static_assert(word(OsdOp::EnableInfo) == hal::LinkWord{0x45});

}
