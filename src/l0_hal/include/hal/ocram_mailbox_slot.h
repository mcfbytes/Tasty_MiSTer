// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace mister::hal::ocram {

enum class MailboxSlot : std::uint8_t { Sector = 0, Mount0 = 1, Mount1 = 2 };

inline constexpr std::array<MailboxSlot, 2> kMountSlots{MailboxSlot::Mount0, MailboxSlot::Mount1};

}
