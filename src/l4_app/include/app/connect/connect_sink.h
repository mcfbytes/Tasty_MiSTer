// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "svc/connect/bt_page_mode.h"
#include "svc/connect/connect_request.h"
#include "svc/types.h"

namespace mister::app::connect {

template <class Wifi, class Bt>
class BasicConnectSink {
    TASTY_SEAT_RESIDENT(Connect);

public:
    BasicConnectSink(Wifi& wifi, Bt& bt) noexcept : wifi_(wifi), bt_(bt) {}

    void on(const svc::connect::ConnectRequest::WifiPage& r) noexcept {
        wifi_page_open_ = r.open;
        wifi_.on(r);
    }

    void on(const svc::connect::ConnectRequest::WifiScan& r) noexcept { wifi_.on(r); }

    void on(const svc::connect::ConnectRequest::WifiJoin& r) noexcept { wifi_.on(r); }

    void on(const svc::connect::ConnectRequest::WifiForget& r) noexcept { wifi_.on(r); }

    void on(const svc::connect::ConnectRequest::WifiForgetAll& r) noexcept { wifi_.on(r); }

    void on(const svc::connect::ConnectRequest::WifiSetCountry& r) noexcept { wifi_.on(r); }

    void on(const svc::connect::ConnectRequest::BtPage& r) noexcept {
        bt_page_open_ = r.mode != svc::connect::BtPageMode::Closed;
        bt_.on(r);
    }

    void on(const svc::connect::ConnectRequest::BtStopAutoFirst& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtPair& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtCancelPair& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtConnect& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtDisconnect& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtRemove& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtRemoveAll& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtAnswer& r) noexcept { bt_.on(r); }

    void on(const svc::connect::ConnectRequest::BtResetAdapter& r) noexcept { bt_.on(r); }

    void misrouted(const svc::connect::ConnectRequest&) noexcept {
        TASTY_SEAT_BODY(BasicConnectSink);
        misrouted_.add();
    }

    [[nodiscard]] bool wifi_page_open() const noexcept { return wifi_page_open_; }

    [[nodiscard]] bool bt_page_open() const noexcept { return bt_page_open_; }

    [[nodiscard]] infra::Tally misrouted_count() const noexcept { return misrouted_; }

private:
    Wifi& wifi_;
    Bt& bt_;
    bool wifi_page_open_ = false;
    bool bt_page_open_ = false;
    infra::Tally misrouted_{};
};

}
