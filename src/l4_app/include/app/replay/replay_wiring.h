// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/conf_str_cell.h"
#include "app/output_lock.h"
#include "app/replay/replay_control.h"
#include "app/replay/replay_ring.h"
#include "app/replay_status.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "os/clock.h"
#include "proto/status_cell.h"

namespace mister::svc {
class Vfs;
}

namespace mister::infra::xthread {
class DiagLog;
}

namespace mister::app {

class IdentityLatch;
class CoreInputBinding;
class IOsdClose;
template <SeatTag Seat>
class LinkTxChannel;
class ScreenshotPump;
class UiRequestRing;

}

namespace mister::app::replay {

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
    const OutputLock& output_lock;
    CoreInputBinding& input;
    ScreenshotPump& shots;
    xthread::DiagLog& diag;

    IOsdClose& osd;

    LinkTxChannel<SeatTag::Ui>& settings_tx;
};

}
