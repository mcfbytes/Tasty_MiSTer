// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/save_copy_level.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/phys_region.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/types.h"

namespace mister::svc {
class Vfs;
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
class SpiFioQueue;
}

namespace mister::cores {
class IDiskPort;
class IMailboxPort;
}

namespace mister::app {

class PcmRingFeeder;

struct CoreTerms {
    TASTY_SEAT_EXEMPT(const_shared);
    hal::ISpiTransport& link;
    const os::IClock& clock;
    proto::IImageSink& bulk;
    proto::SpiFioQueue& fio_queue;
    svc::DiscReadService& unbound_discs;
    infra::OptRef<const svc::Vfs> vfs{};
    infra::OptRef<svc::DiscReadService> discs{};
    infra::OptRef<svc::ChdPrefetch> prefetch{};
    infra::OptRef<cores::IMailboxPort> mailbox{};
    infra::OptRef<cores::IDiskPort> disk{};
    infra::OptRef<const cores::SaveCopyCell> save_copy{};
    infra::OptRef<PcmRingFeeder> feeder{};
    hal::FpgaAperture aperture{};

    hal::DoorbellPolicy doorbells{};
    hal::PhysRegion lw_window{};
    os::UioLineSpace doorbell_nodes{};
};

}
