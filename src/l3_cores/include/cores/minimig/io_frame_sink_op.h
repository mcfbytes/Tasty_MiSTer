// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "hal/types.h"

namespace mister::cores::minimig {

enum class FrameOp : std::uint8_t {
    Status = 0x1E,
    Write = 0xF0,
    Reset = 0xF1,
    Audio = 0xF2,
    Chipset = 0xF3,
    Cpu = 0xF4,
    Memory = 0xF5,
    Video = 0xF6,
    Floppy = 0xF7,
    Hdd = 0xF8,
    Joy = 0xF9,
};

[[nodiscard]] constexpr hal::SpiWord word(FrameOp op) noexcept {
    return hal::SpiWord{std::to_underlying(op)};
}

inline constexpr std::array kFrameOps{
    FrameOp::Status,
    FrameOp::Write,
    FrameOp::Reset,
    FrameOp::Audio,
    FrameOp::Chipset,
    FrameOp::Cpu,
    FrameOp::Memory,
    FrameOp::Video,
    FrameOp::Floppy,
    FrameOp::Hdd,
    FrameOp::Joy
};

[[nodiscard]] constexpr bool opens_frame(hal::SpiWord head) noexcept {
    return std::ranges::any_of(kFrameOps, [head](FrameOp op) { return head == word(op); });
}

static_assert(
    opens_frame(hal::SpiWord{0xF0})
    && opens_frame(hal::SpiWord{0xF9})
    && opens_frame(hal::SpiWord{0x1E})
);
static_assert(
    !opens_frame(hal::SpiWord{0xFA})
    && !opens_frame(hal::SpiWord{0xEF})
    && !opens_frame(hal::SpiWord{0x54})
    && !opens_frame(hal::SpiWord{0x01F0})
);

}
