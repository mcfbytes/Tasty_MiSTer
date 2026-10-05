// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_window_grant.h"

namespace mister::svc {
class Vfs;
class ChdPrefetch;
class DiscReadService;
}  // namespace mister::svc
namespace mister::hal {
class ISpiTransport;
}
namespace mister::os {
class IClock;
}
namespace mister::proto {
class IImageSink;
class SpiFioQueue;
}  // namespace mister::proto

namespace mister::cores {

class CoreInitHost;
class IMailboxPort;

struct HostServices {
    const svc::Vfs& vfs;
    hal::ISpiTransport& link;

    const os::IClock& clock;
    svc::ChdPrefetch* prefetch = nullptr;

    proto::IImageSink* bulk = nullptr;

    CoreWindowGrant windows{};

    svc::DiscReadService* discs = nullptr;

    const CoreInitHost* init_host = nullptr;

    proto::SpiFioQueue* fio_queue = nullptr;

    IMailboxPort* mailbox = nullptr;
};

}  // namespace mister::cores
