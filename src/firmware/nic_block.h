// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "hal/phys_region.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/xthread/park_fds.h"
#include "infra/xthread/wake_flag.h"
#include "open_verdict.h"
#include "svc/nic_bridge.h"
#include "svc/nic_control.h"
#include "svc/nic_host_port.h"
#include "svc/nic_main.h"
#include "svc/nic_mapped_window.h"
#include "svc/nic_status.h"
#include "thread_assembly.h"

namespace mister::fw {

class NicBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    NicBlock(std::span<const hal::PhysRegion> regions, OpenVerdict& verdict) noexcept;
    NicBlock(const NicBlock&) = delete;
    NicBlock& operator=(const NicBlock&) = delete;

    [[nodiscard]] infra::OptRef<svc::NicControlInbox> inbox() noexcept;

    void open_seat(
        ThreadAssembly::SeatMains& mains,
        infra::OptRef<xthread::WakeFlag> asker
    ) noexcept;

    xthread::WakeFlag wake_{};
    svc::NicControlInbox inbox_{wake_};
    svc::NicStatusCell status_{};
    svc::NicHostPort port_{};
    std::optional<svc::NicMappedWindow> window_;
    std::optional<xthread::ParkFds> fds_;
    std::optional<svc::NicBridge> bridge_;
    std::optional<svc::NicMain> main_;
};

}
