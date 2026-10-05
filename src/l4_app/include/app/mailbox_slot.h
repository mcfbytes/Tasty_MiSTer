// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_frame.h"

namespace mister::app {

inline constexpr std::size_t kMailboxDepth = 4;

struct MailboxSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint16_t gen = 0;
    std::uint16_t round = 0;
    proto::MailboxFrame frame{};
    proto::MailboxAct act{};
    std::array<std::uint8_t, proto::kMailboxActBytes> bytes{};
};
static_assert(std::is_trivially_copyable_v<MailboxSlot>);

}  // namespace mister::app
