// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/connect/freq_mhz.h"
#include "os/connect/signal_dbm.h"
#include "os/connect/ssid.h"
#include "os/connect/wifi_security.h"
#include "svc/connect/view_gen.h"

namespace mister::svc::connect {

struct WifiScanViewRow {
    TASTY_SEAT_EXEMPT(component);

    os::connect::Ssid ssid{};
    os::connect::SignalDbm signal = os::connect::SignalDbm::of(0);
    os::connect::FreqMhz freq = os::connect::FreqMhz::of(0);
    os::connect::WifiSecurity security = os::connect::WifiSecurity::Open;
    bool saved = false;
};

static_assert(std::is_trivially_copyable_v<WifiScanViewRow>);

struct WifiScanView {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kRows = 24;

    ViewGen gen{};
    std::array<std::optional<WifiScanViewRow>, kRows> rows{};
};

static_assert(std::is_trivially_copyable_v<WifiScanView>);

}
