// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "cores/transfer_row.h"
#include "proto/mailbox_poll.h"

namespace mister::cores {

struct CompanionPlan {
    std::string_view stem{};
    bool present = false;
    bool loads = false;
    std::uint8_t opcode = 0;
    std::array<std::uint16_t, 3> before{};
    std::array<std::uint16_t, 3> after{};
    proto::MailboxPoll poll = proto::MailboxPoll::Off;
    TransferRow row{};
};

}  // namespace mister::cores
