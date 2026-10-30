// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/event.h"
#include "app/file_tx_level.h"
#include "app/link_tx_channel.h"
#include "app/load_window_map.h"
#include "app/window_tally.h"
#include "hal/phys_region.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "os/clock.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

struct RungEdges {
    TASTY_SEAT_RESIDENT(Main);

    LinkTxChannel<SeatTag::Main>& inbox;
    const FileTxLevelCell& level;
    EventQueue<SeatTag::Main>::Producer events;
    ILoadWindowMap<seat_owner::Main>& window_map;
    hal::PhysRegion aperture;
    const svc::Vfs& vfs;
    WindowTally& windows;
    const os::IClock& clock;
};

}
