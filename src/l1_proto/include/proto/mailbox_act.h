// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxActBytes = 1024;

inline constexpr std::size_t kMailboxCommandWords = 3;

struct MailboxAct {
    TASTY_SEAT_EXEMPT(component);

    enum class Kind : std::uint8_t { None, Command, Download };
    Kind kind = Kind::None;
    std::uint8_t opcode = 0;
    WideIoIndex index{};
    std::array<std::uint16_t, kMailboxCommandWords> words{};
    std::uint16_t len = 0;
};
static_assert(std::is_trivially_copyable_v<MailboxAct>);

}  // namespace mister::proto
