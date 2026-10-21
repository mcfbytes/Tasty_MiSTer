// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/conf_str_cell.h"
#include "app/replay_control.h"
#include "app/replay_ring.h"
#include "app/replay_status.h"
#include "infra/opt_ref.h"
#include "os/clock.h"
#include "proto/status_cell.h"

namespace mister::svc {
class Vfs;
}

namespace mister::xthread {
class DiagLog;
}

namespace mister::app {

class IdentityLatch;
class InputWire;
class IOsdClose;
template <SeatTag Seat>
class LinkTxChannel;
class ScreenshotPump;
class UiRequestRing;
class VideoPump;

struct ReplayWiring {
    TASTY_SEAT_RESIDENT(Ui);

    infra::OptRef<const svc::Vfs> vfs{};
    const os::IClock& clock;
    ReplayRing::Sender ring;
    ReplayControlCell& control;
    const ReplayStatusCell& status;
    const IdentityLatch& identity;
    const proto::StatusCell& core_status;
    const ConfStrCell& conf;
    UiRequestRing& asks;
    const VideoPump& video;
    InputWire& input;
    ScreenshotPump& shots;
    xthread::DiagLog& diag;

    IOsdClose& osd;

    LinkTxChannel<SeatTag::Ui>& settings_tx;
};

}
