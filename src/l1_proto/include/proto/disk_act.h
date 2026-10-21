// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::proto {

inline constexpr std::size_t kDiskStateBytes = 16;

inline constexpr std::size_t kDiskActBytes = 512;

inline constexpr std::uint8_t kNoDiskUnit = 0xFF;

struct DiskAct {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t { None, Out, In, Signal };
    Kind kind = Kind::None;
    bool last = false;
    bool has_word = false;
    std::uint8_t unit = kNoDiskUnit;
    std::uint16_t opcode = 0;
    std::uint16_t len = 0;
    std::uint16_t word = 0;
};

static_assert(std::is_trivially_copyable_v<DiskAct>);

}
