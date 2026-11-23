// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "proto/mailbox_act.h"
#include "proto/mailbox_bus.h"
#include "proto/mailbox_result.h"
#include "proto/mailbox_select.h"
#include "proto/user_io/poll_frame.h"

namespace mister::cores {

class IMailboxServant {
public:
    using ActBytes = std::span<std::uint8_t, proto::kMailboxActBytes>;

    using BulkBytes = std::span<std::byte, proto::kMailboxBulkBytes>;

    virtual ~IMailboxServant() = default;

    [[nodiscard]] virtual proto::MailboxAct serve(
        proto::MailboxSelectIndex select,
        const proto::user_io::PollFrame& frame,
        ActBytes bytes,
        BulkBytes bulk
    ) noexcept = 0;

    [[nodiscard]] virtual proto::MailboxAct settle(
        const proto::MailboxResult& result,
        ActBytes bytes,
        BulkBytes bulk
    ) noexcept = 0;

    virtual void begin_core() noexcept = 0;

    virtual void rebind(std::string_view stem, bool present) noexcept = 0;

    virtual void refill_one() noexcept = 0;
    [[nodiscard]] virtual bool rest() const noexcept = 0;

    virtual void release() noexcept = 0;

protected:
    IMailboxServant() = default;
    IMailboxServant(const IMailboxServant&) = default;
    IMailboxServant& operator=(const IMailboxServant&) = default;
};

}
