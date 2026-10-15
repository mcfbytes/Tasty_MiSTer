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
    infra::OptRef<svc::ChdPrefetch> prefetch{};

    infra::OptRef<proto::IImageSink> bulk{};

    CoreWindowGrant windows{};

    infra::OptRef<svc::DiscReadService> discs{};

    infra::OptRef<proto::SpiFioQueue> fio_queue{};

    infra::OptRef<IMailboxPort> mailbox{};

    infra::OptRef<const SaveCopyCell> save_copy{};
};

}  // namespace mister::cores
