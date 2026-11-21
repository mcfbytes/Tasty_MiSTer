// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "app/connect/connect_host.h"
#include "infra/error.h"
#include "infra/xthread/park_fds.h"
#include "infra/park_timeout.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/seat_main.h"
#include "infra/xthread/seat_park.h"
#include "infra/xthread/wake_flag.h"
#include "os/connect/wpa_ctrl.h"
#include "svc/connect/bt_service.h"
#include "svc/connect/ifup_starter.h"
#include "svc/connect/wifi_service.h"
#include "svc/connect/wpa_conf_store.h"

namespace mister::app::connect {

using WifiService = svc::connect::BasicWifiService<
    os::connect::WpaCtrlOpener,
    svc::connect::WpaConfStore,
    svc::connect::IfupStarter>;
using BtService = svc::connect::BasicBtService<os::connect::DbusConnection>;

using ConnectHost = BasicConnectHost<WifiService, BtService>;

class ConnectMain final : public xthread::SeatMain<ConnectMain> {
    TASTY_SEAT_RESIDENT(Connect);

public:
    ConnectMain(ConnectHost& host, xthread::ParkFds fds) noexcept
        : host_(host),
          wake_(fds.wake()),
          stop_(std::move(fds).take_stop()),
          park_(wake_, stop_, host_.epoll().fd()) {}

    ConnectMain(const ConnectMain&) = delete;
    ConnectMain& operator=(const ConnectMain&) = delete;
    ConnectMain(ConnectMain&&) = delete;
    ConnectMain& operator=(ConnectMain&&) = delete;

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    void serve() noexcept { host_.serve(); }

    [[nodiscard]] bool idle() const noexcept { return stopping() || host_.idle(); }

    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }

    [[nodiscard]] infra::ParkTimeout park_ms() const noexcept { return host_.park_ms(); }

    [[nodiscard]] xthread::SeatPark& park() noexcept { return park_; }

    [[nodiscard]] bool paused() noexcept { return false; }

    [[nodiscard]] bool pause_pending() const noexcept { return false; }

    void settle() noexcept { host_.stop(); }

private:
    ConnectHost& host_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_;
    xthread::SeatPark park_;
};

static_assert(xthread::SeatBody<ConnectMain>);

}
