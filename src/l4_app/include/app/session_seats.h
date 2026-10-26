// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/bay_releases.h"
#include "app/board_ops.h"
#include "app/built_core.h"
#include "app/file_stream_service.h"
#include "app/ini_parse.h"
#include "app/ladder_cell.h"
#include "app/uart_mode.h"
#include "cores/save_copy_level.h"
#include "infra/opt_ref.h"
#include "infra/wake_flag.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/pin_levels.h"
#include "os/types.h"
#include "proto/status_snapshot.h"

namespace mister::xthread {
struct RtStats;
class LogLane;
}

namespace mister::reactor {
class Executive;
struct CoreState;
}

namespace mister::os {
class IClock;
}

namespace mister::proto {
class IStorageChannel;
}

namespace mister::svc {
class Vfs;
class DiscReadService;
class ChdPrefetch;
class IStorageLifecycle;
class InputEmitter;
}

namespace mister::app {

class VideoWire;
class InputWire;
class OsdWire;
template <SeatTag Producer>
class LinkTxChannel;
template <SeatTag Consumer>
class LinkRxChannel;
class LinkRouter;
class ICheatApply;
class PcmRingFeeder;
class MailboxRelay;
class DiskRelay;
class DurableWriteService;
class WindowJobService;

struct SupervisorParts {

    struct Services {
        infra::OptRef<PcmRingFeeder> feeder{};
        infra::OptRef<svc::ChdPrefetch> prefetch{};
        infra::OptRef<MailboxRelay> mailbox{};
        infra::OptRef<DiskRelay> disks{};
        infra::OptRef<svc::DiscReadService> discs{};
        infra::OptRef<DurableWriteService> writes{};
        infra::OptRef<WindowJobService> window_jobs{};
        infra::OptRef<svc::IStorageLifecycle> storage_life{};
    };

    VideoWire& video;
    InputWire& input;
    OsdWire& osd;

    LinkTxChannel<SeatTag::Main>& link_inbox;
    LinkRxChannel<SeatTag::Main>& link_rx;
    const ConfigCell& config_cell;
    LinkTxChannel<SeatTag::Ui>& ui_inbox;

    LinkTxChannel<SeatTag::Input>& input_inbox;
    LinkRouter& router;

    const LadderStateCell& ladder_cell;

    FileStreamService& streams;

    svc::DiscReadService& unbound_discs;

    hal::PinLevelCell& pin_levels;
    xthread::WakeFlag& main_wake;
    reactor::Executive& exec;
    reactor::CoreState& core_state;
    const os::IClock& clock;

    svc::InputEmitter& input_emitter;
    xthread::LogLane& log_lane;

    ICheatApply& cheats;

    CoreBay& bay;
    BayReleasesCell& bay_releases;

    infra::OptRef<const svc::Vfs> vfs{};

    infra::OptRef<UartModeController::Handoffs> uart_handoffs{};
    bool stdout_routing = false;

    hal::DoorbellPolicy doorbells{};
    hal::FpgaAperture fpga_mem{};
    hal::PhysRegion lw_window{};
    std::span<std::uint32_t> lw_image{};
    os::UioLineSpace doorbell_nodes{};
    os::CpuMask doorbell_cpus{};

    infra::OptRef<const proto::StatusSnapshotCell> status_snapshots{};

    infra::OptRef<proto::IStorageChannel> storage_channel{};

    BoardOps board_ops{};

    infra::OptRef<const cores::SaveCopyCell> save_copy{};
    Services services{};
};

}
