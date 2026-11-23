// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "infra/seat.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_bus.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_select.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame.h"

namespace mister::app::pcm {

inline constexpr std::size_t kMailboxDepth = 4;

struct MailboxSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    enum class Origin : std::uint8_t { Poll, Result };

    proto::CompanionBindGen gen{};
    std::uint16_t round = 0;
    Origin origin = Origin::Poll;
    proto::MailboxSelectIndex select{};
    proto::user_io::PollFrame frame{};
    proto::MailboxResult result{};
    proto::MailboxAct act{};
    std::array<std::uint8_t, proto::kMailboxActBytes> bytes{};

    std::array<std::byte, proto::kMailboxBulkBytes> bulk{};
};

static_assert(std::is_trivially_copyable_v<MailboxSlot>);

}
