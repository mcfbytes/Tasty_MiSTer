// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_window_grant.h"
#include "cores/save_copy_level.h"
#include "infra/opt_ref.h"

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

class IMailboxPort;

struct HostServices {
    const svc::Vfs& vfs;
    hal::ISpiTransport& link;

    const os::IClock& clock;
    svc::ChdPrefetch* prefetch = nullptr;

    proto::IImageSink* bulk = nullptr;

    CoreWindowGrant windows{};

    svc::DiscReadService* discs = nullptr;

    proto::SpiFioQueue* fio_queue = nullptr;

    IMailboxPort* mailbox = nullptr;

    infra::OptRef<const SaveCopyCell> save_copy{};
};

}  // namespace mister::cores
