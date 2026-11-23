// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/disk_port.h"
#include "cores/save_copy_level.h"
#include "cores/mount_reads.h"
#include "cores/remount_asked.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/irq_pin.h"
#include "hal/phys_region.h"
#include "hal/reg_port.h"
#include "hal/sys_directory.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/xthread/wake_flag.h"
#include "infra/seat.h"
#include "os/types.h"
#include "os/uio_line_space.h"

namespace mister::svc {
class Vfs;
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

namespace mister::cores {
class IMailboxPort;
}

namespace mister::hal {
class DiskLamp;
}

namespace mister::app {

namespace pcm {
class PcmRingFeeder;
}

struct CoreTerms {
    TASTY_SEAT_EXEMPT(const_shared);
    hal::ISpiTransport& link;
    const os::IClock& clock;
    proto::IImageSink& bulk;
    proto::link::SpiFioQueue& fio_queue;
    svc::disc::DiscReadService& unbound_discs;
    infra::OptRef<const svc::Vfs> vfs{};
    infra::OptRef<svc::disc::DiscReadService> discs{};
    infra::OptRef<svc::disc::ChdPrefetch> prefetch{};
    infra::OptRef<cores::IMailboxPort> mailbox{};
    cores::DiskPorts disk{};

    infra::OptRef<hal::DiskLamp> lamp{};
    infra::OptRef<xthread::WakeFlag> disk_activity{};

    infra::OptRef<cores::RemountCell> remount{};
    infra::OptRef<cores::MountReadsCell> mount_reads{};
    infra::OptRef<const cores::SaveCopyCell> save_copy{};
    infra::OptRef<pcm::PcmRingFeeder> feeder{};
    hal::FpgaAperture aperture{};

    hal::DoorbellPolicy doorbells{};
    hal::PhysRegion lw_window{};
    std::span<std::uint32_t> lw_image{};

    infra::OptRef<hal::IRegPort<hal::SysDirectory>> lw_header{};
    os::UioLineSpace doorbell_nodes{};
    os::CpuMask doorbell_cpus{};
    const char* proc_root = hal::kProcRoot;
};

}
