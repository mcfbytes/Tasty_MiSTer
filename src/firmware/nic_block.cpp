// SPDX-License-Identifier: GPL-3.0-or-later
#include "nic_block.h"

#include <utility>

#include "hal/board_regions.h"
#include "hal/region_id.h"
#include "infra/prelude.h"

namespace mister::fw {

NicBlock::NicBlock(std::span<const hal::PhysRegion> regions, OpenVerdict& verdict) noexcept {
    const auto page = hal::BoardRegions::hps_region(regions, hal::RegionId::NicMailbox);
    if (!page)
        return;
    if (auto fds = xthread::ParkFds::create(wake_); !fds) {
        verdict.refuse("T-NIC: ParkFds::create", fds.error());
    } else {
        window_.emplace(*page);
        fds_.emplace(std::move(*fds));
    }
}

infra::OptRef<svc::NicControlInbox> NicBlock::inbox() noexcept {
    return window_ ? infra::OptRef<svc::NicControlInbox>{inbox_}
                   : infra::OptRef<svc::NicControlInbox>{};
}

void NicBlock::open_seat(
    ThreadAssembly::SeatMains& mains,
    infra::OptRef<xthread::WakeFlag> asker
) noexcept {
    if (!window_ || !fds_)
        return;
    bridge_.emplace(
        svc::NicBridge::Wiring{
            .inbox = inbox_,
            .window = *window_,
            .port = port_,
            .status = status_
        }
    );
    main_.emplace(*bridge_, std::move(seated(fds_, "T-NIC bound past a refused open")), asker);
    mains.bind(&*main_);
}

}
