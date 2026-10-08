// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/board_ops.h"
#include "app/file_stream_service.h"
#include "app/ini_parse.h"
#include "app/ladder_cell.h"
#include "app/uart_mode.h"
#include "infra/wake_flag.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "hal/pin_levels.h"
#include "os/types.h"

namespace mister::xthread {
struct RtStats;
class LogLane;
}  // namespace mister::xthread

namespace mister::reactor {
class Executive;
struct CoreState;
}  // namespace mister::reactor
namespace mister::os {
class IClock;
}
namespace mister::hal {
class IBootHandoff;
}
namespace mister::proto {
class IStorageChannel;
}

namespace mister::svc {
class Vfs;
class DiscReadService;
class InputEmitter;
}  // namespace mister::svc

namespace mister::app {

class VideoWire;
class InputWire;
class OsdWire;
class LinkTxChannel;
class LinkRxChannel;
class LinkRouter;
class ICheatApply;

struct SupervisorParts {
    VideoWire& video;
    InputWire& input;
    OsdWire& osd;

    LinkTxChannel& link_inbox;
    LinkRxChannel& link_rx;
    const ConfigCell& config_cell;
    LinkTxChannel& ui_inbox;

    LinkTxChannel& input_inbox;
    LinkRouter& router;

    LadderStateCell& ladder_cell;

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

    const svc::Vfs* vfs = nullptr;

    UartModeController::Handoffs* uart_handoffs = nullptr;
    bool stdout_routing = false;

    hal::DoorbellPolicy doorbells{};
    hal::FpgaAperture fpga_mem{};
    hal::PhysRegion lw_window{};
    os::UioLineSpace doorbell_nodes{};

    hal::IBootHandoff* boot_handoff = nullptr;

    proto::IStorageChannel* storage_channel = nullptr;

    BoardOps board_ops{};
};

}  // namespace mister::app
