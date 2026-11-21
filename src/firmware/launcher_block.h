// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/launcher/launcher_demand.h"
#include "app/launcher/launcher_fb_ack.h"
#include "app/launcher/launcher_host.h"
#include "app/input/launcher_key_bridge.h"
#include "app/launcher/launcher_main.h"
#include "app/launcher_state.h"
#include "app/launcher/scanout_channel.h"
#include "app/launcher/scanout_relay.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/xthread/park_fds.h"
#include "infra/prelude.h"
#include "os/epoll_set.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "os/clock.h"
#include "os/linux_vt_console.h"
#include "os/uinput_keyboard.h"
#include "thread_assembly.h"

namespace mister::fw {

class LauncherBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    explicit LauncherBlock(const BootParts& plat) : scanout_relay_{plat.link, mint_worker_()} {}

    LauncherBlock(const LauncherBlock&) = delete;
    LauncherBlock& operator=(const LauncherBlock&) = delete;

    [[nodiscard]] DeviceBlock::Parts::Frontend device_front() noexcept;

    void open_fds(OpenVerdict& verdict) noexcept;

    void open_seat(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept;

    app::LauncherStateCell launcher_state_{};
    os::UinputKeyboard uinput_{};
    app::input::LauncherKeyBridge launcher_keys_{uinput_};
    app::launcher::LauncherDemandCell launcher_demand_{};
    xthread::WakeFlag launcher_wake_{};
    app::launcher::LauncherFbAckCell launcher_fb_ack_{};
    app::launcher::ScanoutChannel scanout_channel_{launcher_wake_};
    app::launcher::ScanoutRelay scanout_relay_;
    os::LinuxVtConsole vt_console_{};
    std::optional<os::EpollSet> host_epoll_;
    std::optional<xthread::ParkFds> launcher_fds_;
    std::optional<app::launcher::LauncherHost> launcher_host_;
    std::optional<app::launcher::LauncherMain> launcher_main_;

private:
    [[nodiscard]] app::launcher::ScanoutChannel::Worker mint_worker_() noexcept;
};

}
