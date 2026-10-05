// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "proto/mailbox_act.h"
#include "proto/mailbox_frame.h"

namespace mister::cores {

class IMailboxServant {
public:
    virtual ~IMailboxServant() = default;

    [[nodiscard]] virtual proto::MailboxAct serve(
        const proto::MailboxFrame& frame,
        std::span<std::uint8_t, proto::kMailboxActBytes> bytes) noexcept = 0;

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

}  // namespace mister::cores
