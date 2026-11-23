// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/core_window_grant.h"
#include "cores/disk_ports.h"
#include "cores/save_copy_level.h"
#include "cores/slot_servant_grant.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"

namespace mister::svc {
class IStorageBackend;
}

namespace mister::svc::disc {
class ChdPrefetch;
class DiscReadService;
}

namespace mister::hal {
class ISpiTransport;
}

namespace mister::os {
class IClock;
}

namespace mister::proto {
class IImageSink;
}

namespace mister::proto::link {
class SpiFioQueue;
}

namespace mister::hal {
class DiskLamp;
}

namespace mister::cores {

class IMailboxPort;

struct HostServices {
    hal::ISpiTransport& link;

    const os::IClock& clock;
    infra::OptRef<svc::disc::ChdPrefetch> prefetch{};

    infra::OptRef<proto::IImageSink> bulk{};

    CoreWindowGrant windows{};

    infra::OptRef<svc::disc::DiscReadService> discs{};

    infra::OptRef<proto::link::SpiFioQueue> fio_queue{};

    infra::OptRef<IMailboxPort> mailbox{};

    infra::OptRef<const SaveCopyCell> save_copy{};

    DiskPorts disk{};

    infra::OptRef<hal::DiskLamp> lamp{};
    infra::OptRef<xthread::WakeFlag> disk_activity{};

    infra::OptRef<svc::IStorageBackend> save_store{};

    SlotServantGrant slot_servants{};
};

}
