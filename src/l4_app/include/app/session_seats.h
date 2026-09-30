// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/ini_parse.h"
#include "infra/wake_flag.h"
#include "hal/doorbell_policy.h"
#include "hal/fpga_aperture.h"
#include "os/types.h"

namespace mister::xthread {
struct RtStats;
}

namespace mister::reactor {
class Executive;
struct CoreState;
}  // namespace mister::reactor
namespace mister::os {
class IClock;
}

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class VideoWire;
class InputWire;
class OsdWire;
class LinkTxChannel;
class LinkRxChannel;
class LinkRouter;

struct SupervisorParts {
    VideoWire& video;
    InputWire& input;
    OsdWire& osd;

    LinkTxChannel& link_inbox;
    LinkRxChannel& link_rx;
    ConfigCell& config_cell;
    LinkTxChannel& ui_inbox;

    LinkTxChannel& input_inbox;
    LinkRouter& router;

    const svc::Vfs* vfs = nullptr;

    xthread::WakeFlag* io_wake = nullptr;

    xthread::WakeFlag* main_wake = nullptr;
    bool stdout_routing = false;

    hal::DoorbellPolicy doorbells{};
    hal::FpgaAperture fpga_mem{};
    hal::PhysRegion lw_window{};
    os::UioLineSpace doorbell_nodes{};
    reactor::Executive* exec = nullptr;
    reactor::CoreState* core_state = nullptr;
    const os::IClock* clock = nullptr;
};

}  // namespace mister::app
