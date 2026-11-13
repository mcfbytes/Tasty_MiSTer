// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "svc/bt_page_mode.h"
#include "svc/connect_request.h"
#include "svc/types.h"

namespace mister::app {

template <class Wifi, class Bt>
class BasicConnectSink {
    TASTY_SEAT_RESIDENT(Connect);

public:
    BasicConnectSink(Wifi& wifi, Bt& bt) noexcept : wifi_(wifi), bt_(bt) {}

    void on(const svc::ConnectRequest::WifiPage& r) noexcept {
        wifi_page_open_ = r.open;
        wifi_.on(r);
    }

    void on(const svc::ConnectRequest::WifiScan& r) noexcept { wifi_.on(r); }

    void on(const svc::ConnectRequest::WifiJoin& r) noexcept { wifi_.on(r); }

    void on(const svc::ConnectRequest::WifiForget& r) noexcept { wifi_.on(r); }

    void on(const svc::ConnectRequest::WifiForgetAll& r) noexcept { wifi_.on(r); }

    void on(const svc::ConnectRequest::WifiSetCountry& r) noexcept { wifi_.on(r); }

    void on(const svc::ConnectRequest::BtPage& r) noexcept {
        bt_page_open_ = r.mode != svc::BtPageMode::Closed;
        bt_.on(r);
    }

    void on(const svc::ConnectRequest::BtStopAutoFirst& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtPair& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtCancelPair& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtConnect& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtDisconnect& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtRemove& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtRemoveAll& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtAnswer& r) noexcept { bt_.on(r); }

    void on(const svc::ConnectRequest::BtResetAdapter& r) noexcept { bt_.on(r); }

    void misrouted(const svc::ConnectRequest&) noexcept {
        TASTY_SEAT_BODY(BasicConnectSink);
        misrouted_.add();
    }

    [[nodiscard]] bool wifi_page_open() const noexcept { return wifi_page_open_; }

    [[nodiscard]] bool bt_page_open() const noexcept { return bt_page_open_; }

    [[nodiscard]] svc::Tally misrouted_count() const noexcept { return misrouted_; }

private:
    Wifi& wifi_;
    Bt& bt_;
    bool wifi_page_open_ = false;
    bool bt_page_open_ = false;
    svc::Tally misrouted_{};
};

}
