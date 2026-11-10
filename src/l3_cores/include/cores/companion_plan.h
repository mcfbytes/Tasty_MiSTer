// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "cores/transfer_row.h"
#include "proto/mailbox_act.h"
#include "proto/mailbox_poll.h"
#include "proto/uio_opcodes.h"

namespace mister::cores {

struct CompanionPlan {

    struct Load {
        proto::MailboxWords before{};
        TransferRow row{};
    };

    std::string_view stem{};
    bool present = false;
    std::optional<Load> load{};
    proto::MailboxWords after{};
    proto::MailboxPoll poll = proto::MailboxPoll::Off;
    proto::UioOp opcode{};
};

}
