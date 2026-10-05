// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::proto {

inline constexpr std::size_t kMailboxFrameWords = 4;

struct MailboxFrame {
    TASTY_SEAT_EXEMPT(component);
    std::array<std::uint16_t, kMailboxFrameWords> w{};
};
static_assert(std::is_trivially_copyable_v<MailboxFrame> && sizeof(MailboxFrame) == 8);

}  // namespace mister::proto
