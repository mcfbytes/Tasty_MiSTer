// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/launcher_demand.h"
#include "app/launcher_fb_ack.h"
#include "app/launcher_host.h"
#include "app/launcher_key_bridge.h"
#include "app/launcher_main.h"
#include "app/launcher_state.h"
#include "app/scanout_channel.h"
#include "app/scanout_relay.h"
#include "boot_parts.h"
#include "device_block.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "os/clock.h"
#include "os/linux_vt_console.h"
#include "os/uinput_keyboard.h"
#include "thread_assembly.h"

namespace mister::fw {

class LauncherBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    explicit LauncherBlock(const BootParts& plat) : scanout_relay_{plat.link, mint_answerer_()} {}

    LauncherBlock(const LauncherBlock&) = delete;
    LauncherBlock& operator=(const LauncherBlock&) = delete;

    [[nodiscard]] DeviceBlock::Front device_front(bool grants_uart) noexcept;

    void open_seat(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept;

    app::LauncherStateCell launcher_state_{};
    os::UinputKeyboard uinput_{};
    app::LauncherKeyBridge launcher_keys_{uinput_};
    app::LauncherDemandCell launcher_demand_{};
    xthread::WakeFlag launcher_wake_{};
    app::LauncherFbAckCell launcher_fb_ack_{};
    app::ScanoutChannel scanout_channel_{launcher_wake_};
    app::ScanoutRelay scanout_relay_;
    os::LinuxVtConsole vt_console_{};
    std::optional<app::LauncherHost> launcher_host_;
    std::optional<app::LauncherMain> launcher_main_;

private:
    [[nodiscard]] app::ScanoutChannel::Answerer mint_answerer_() noexcept;
};

}
