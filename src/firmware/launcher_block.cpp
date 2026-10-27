// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher_block.h"

#include <utility>

#include "infra/opt_ref.h"
#include "open_verdict.h"

namespace mister::fw {

DeviceBlock::Front LauncherBlock::device_front(bool grants_uart) noexcept {
    return DeviceBlock::Front{
        .launcher_demand = infra::OptRef{launcher_demand_},
        .launcher_wake = infra::OptRef<xthread::WakeFlag>{launcher_wake_},
        .launcher_state = infra::OptRef<const app::LauncherStateCell>{launcher_state_},
        .launcher_keys = infra::OptRef<app::LauncherKeyBridge>{launcher_keys_},
        .grants_uart = grants_uart,
    };
}

app::ScanoutChannel::Answerer LauncherBlock::mint_answerer_() noexcept {
    app::ScanoutChannel::Answerer a = scanout_channel_.answerer();
    if (!a)
        fatal(Error{Errc::busy, ERR_SITE(), 0}, "scanout answerer minted twice");
    return a;
}

void LauncherBlock::open_fds(OpenVerdict& verdict) noexcept {
    if (auto host = app::LauncherHost::Fds::create(); !host) {
        verdict.refuse("T-LAUNCHER: LauncherHost::Fds::create", host.error());
    } else if (auto fds = xthread::ParkFds::create(launcher_wake_); !fds) {
        verdict.refuse("T-LAUNCHER: ParkFds::create", fds.error());
    } else {
        host_fds_.emplace(std::move(*host));
        launcher_fds_.emplace(std::move(*fds));
    }
}

void LauncherBlock::open_seat(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept {
    app::ScanoutChannel::Asker asker = scanout_channel_.asker();
    if (!asker)
        fatal(Error{Errc::busy, ERR_SITE(), 0}, "scanout asker minted twice");
    launcher_host_.emplace(
        std::move(seated(host_fds_, "T-LAUNCHER bound past a refused open")),
        app::LauncherHost::Wiring{
            .demand = launcher_demand_,
            .state = launcher_state_,
            .fb_ack = launcher_fb_ack_,
            .scanout = std::move(asker),
            .console = vt_console_,
            .clock = device.clock_
        }
    );
    launcher_main_.emplace(
        *launcher_host_,
        std::move(seated(launcher_fds_, "T-LAUNCHER bound past a refused open")),
        infra::OptRef{device.main_wake_}
    );
    mains.mains.bind(&*launcher_main_);
    device.grant_pause(SeatTag::Launcher, launcher_main_->pause_latch());
}

}
