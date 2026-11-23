// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "proto/mailbox_gate.h"
#include "proto/mailbox_select_count.h"
#include "proto/mailbox_select_index.h"
#include "proto/types.h"
#include "proto/user_io/poll_frame_shape.h"

namespace mister::proto {

inline constexpr MailboxSelectCount kMailboxSelects{4};
inline constexpr MailboxSelectCount kMailboxGuards{2};

struct MailboxSelect {
    user_io::PollFrameShape shape;

    std::optional<MailboxSelectIndex> follows{};

    bool relent_at_bind = false;

    std::optional<MailboxGate> gate{};

    bool pops = false;

    ReplyCount repeats{};

    bool relent_after_act = false;
};

}
