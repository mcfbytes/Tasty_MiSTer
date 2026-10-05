// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

class IMailboxRows {
public:
    virtual ~IMailboxRows() = default;

    virtual void service_mailbox_tick() noexcept = 0;

protected:
    IMailboxRows() = default;
    IMailboxRows(const IMailboxRows&) = default;
    IMailboxRows& operator=(const IMailboxRows&) = default;
};

}  // namespace mister::cores
