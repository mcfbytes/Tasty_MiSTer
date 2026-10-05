// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/mailbox_frame_decl.h"

namespace mister::hal {
class ISpiTransport;
}
namespace mister::proto {
class SpiFioQueue;
}

namespace mister::cores {

class IMailboxPort {
public:
    virtual ~IMailboxPort() = default;

    virtual void service_rt(hal::ISpiTransport& link, proto::SpiFioQueue& queue,
                            const proto::MailboxFrameDecl& frame) noexcept = 0;

protected:
    IMailboxPort() = default;
    IMailboxPort(const IMailboxPort&) = default;
    IMailboxPort& operator=(const IMailboxPort&) = default;
};

}  // namespace mister::cores
