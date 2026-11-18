// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "hal/types.h"

namespace mister::app {

struct UioBurst {
    static constexpr std::uint8_t kMaxWords = 12;
    std::uint16_t tag = 0;
    hal::SpiWord opcode{};
    std::uint8_t count = 0;
    bool ok = true;
    std::array<hal::SpiWord, kMaxWords> words{};
};

static_assert(std::is_trivially_copyable_v<UioBurst>);
static_assert(sizeof(UioBurst) <= 32, "half a cache line: the ask and its answer are one shape");

}
