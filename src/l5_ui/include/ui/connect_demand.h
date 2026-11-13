// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/seat.h"
#include "svc/bt_page_mode.h"
#include "ui/pair_start.h"

namespace mister::ui {

class ConnectDemand {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr ConnectDemand() noexcept = default;

    [[nodiscard]] static constexpr ConnectDemand wifi_page() noexcept {
        ConnectDemand out;
        out.wifi_ = true;
        return out;
    }

    [[nodiscard]] static constexpr ConnectDemand bluetooth(
        svc::BtPageMode mode,
        PairStart start
    ) noexcept {
        ConnectDemand out;
        out.mode_ = mode;
        out.start_ = start;
        return out;
    }

    [[nodiscard]] constexpr ConnectDemand merged_with(ConnectDemand other) const noexcept {
        ConnectDemand out = *this;
        out.wifi_ = wifi_ || other.wifi_;
        if (other.mode_ > mode_) {
            out.mode_ = other.mode_;
            out.start_ = other.start_;
        } else if (other.mode_ == mode_ && other.start_ == PairStart::AutoFirst) {
            out.start_ = PairStart::AutoFirst;
        }
        return out;
    }

    [[nodiscard]] constexpr bool wifi_open() const noexcept { return wifi_; }

    [[nodiscard]] constexpr svc::BtPageMode bt_mode() const noexcept { return mode_; }

    [[nodiscard]] constexpr PairStart pair_start() const noexcept { return start_; }

    [[nodiscard]] constexpr bool same_wifi(ConnectDemand other) const noexcept {
        return wifi_ == other.wifi_;
    }

    [[nodiscard]] constexpr bool same_bluetooth(ConnectDemand other) const noexcept {
        return mode_ == other.mode_ && start_ == other.start_;
    }

    [[nodiscard]] constexpr ConnectDemand adopting_wifi(ConnectDemand from) const noexcept {
        ConnectDemand out = *this;
        out.wifi_ = from.wifi_;
        return out;
    }

    [[nodiscard]] constexpr ConnectDemand adopting_bluetooth(ConnectDemand from) const noexcept {
        ConnectDemand out = *this;
        out.mode_ = from.mode_;
        out.start_ = from.start_;
        return out;
    }

    friend constexpr bool operator==(const ConnectDemand&, const ConnectDemand&) = default;

private:
    bool wifi_ = false;
    svc::BtPageMode mode_ = svc::BtPageMode::Closed;
    PairStart start_ = PairStart::Pick;
};

static_assert(std::is_trivially_copyable_v<ConnectDemand>);

static_assert(
    svc::BtPageMode::Closed < svc::BtPageMode::Paired
    && svc::BtPageMode::Paired < svc::BtPageMode::Pairing
    && svc::BtPageMode::Pairing < svc::BtPageMode::PairingAll
);

}
