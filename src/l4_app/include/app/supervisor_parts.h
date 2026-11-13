// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/bay_releases.h"
#include "app/board_ops.h"
#include "app/built_core.h"
#include "app/identity_record.h"
#include "app/ini_parse.h"
#include "app/rumble_players_cell.h"
#include "app/ladder_cell.h"
#include "app/uart_mode.h"
#include "cores/save_copy_level.h"
#include "infra/log_lane.h"
#include "infra/opt_ref.h"
#include "infra/wake_flag.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/pin_levels.h"
#include "os/types.h"
#include "os/uio_line_space.h"
#include "proto/status_snapshot.h"

namespace mister::xthread {
struct RtStats;
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
class IStorageBackend;
class InputEmitter;
}

namespace mister::hal {
class DiskLamp;
}

namespace mister::app {

class VideoWire;
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
class SaveUploadRelay;
class WindowJobService;

struct SupervisorParts {

    struct Services {
        PcmRingFeeder& feeder;
        svc::ChdPrefetch& prefetch;
        MailboxRelay& mailbox;
        DiskRelay& disks;
        SaveUploadRelay& saves;
        svc::DiscReadService& discs;
        WindowJobService& window_jobs;

        infra::OptRef<svc::IStorageLifecycle> storage_life{};

        infra::OptRef<svc::IStorageBackend> disc_backend{};
    };

    VideoWire& video;
    const RumblePlayersCell& rumble_players;
    OsdWire& osd;

    LinkTxChannel<SeatTag::Main>& link_inbox;
    LinkRxChannel<SeatTag::Main>& link_rx;
    const ConfigCell& config_cell;

    IdentityRecordCell& identity_record_cell;
    LinkTxChannel<SeatTag::Ui>& ui_inbox;

    LinkTxChannel<SeatTag::Input>& input_inbox;
    LinkRouter& router;

    const LadderStateCell& ladder_cell;

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

    infra::OptRef<hal::DiskLamp> lamp{};
    infra::OptRef<xthread::WakeFlag> disk_activity{};

    BoardOps board_ops{};

    infra::OptRef<const cores::SaveCopyCell> save_copy{};
    Services services;
};

}
