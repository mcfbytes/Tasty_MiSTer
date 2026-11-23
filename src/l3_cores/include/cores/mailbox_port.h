// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/mailbox_programme.h"

namespace mister::os {
class IClock;
}

namespace mister::proto::link {
class Link;
class SpiFioQueue;
}

namespace mister::cores {

class IMailboxPort {
public:
    virtual ~IMailboxPort() = default;

    virtual void service_rt(
        proto::link::Link& link,
        proto::link::SpiFioQueue& queue,
        const proto::MailboxProgramme& programme,
        const os::IClock& clock
    ) noexcept = 0;

protected:
    IMailboxPort() = default;
    IMailboxPort(const IMailboxPort&) = default;
    IMailboxPort& operator=(const IMailboxPort&) = default;
};

}
