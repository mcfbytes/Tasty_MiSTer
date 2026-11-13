// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/connect_channel.h"
#include "app/connect_main.h"
#include "connect_paths.h"
#include "infra/error.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "open_verdict.h"
#include "os/clock.h"
#include "os/epoll_set.h"
#include "thread_assembly.h"
#include "ui/connect_port.h"

namespace mister::fw {

class ConnectBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    ConnectBlock(const ConnectPaths& paths, OpenVerdict& verdict) noexcept;
    ConnectBlock(const ConnectBlock&) = delete;
    ConnectBlock& operator=(const ConnectBlock&) = delete;

    [[nodiscard]] ui::ConnectPort& port() noexcept { return port_; }

    void open_seat(const os::IClock& clock, ThreadAssembly::SeatMains& mains) noexcept;

    xthread::WakeFlag connect_wake_{};
    app::ConnectChannel channel_{connect_wake_};
    std::optional<os::EpollSet> epoll_;
    std::optional<xthread::ParkFds> fds_;
    ui::ConnectPort port_;
    std::optional<app::ConnectHost> host_;
    std::optional<app::ConnectMain> main_;

private:
    ConnectPaths paths_;
};

}
