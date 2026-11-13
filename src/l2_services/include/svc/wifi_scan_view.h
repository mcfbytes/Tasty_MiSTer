// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/freq_mhz.h"
#include "os/signal_dbm.h"
#include "os/ssid.h"
#include "os/wifi_security.h"
#include "svc/view_gen.h"

namespace mister::svc {

struct WifiScanViewRow {
    TASTY_SEAT_EXEMPT(component);

    os::Ssid ssid{};
    os::SignalDbm signal = os::SignalDbm::of(0);
    os::FreqMhz freq = os::FreqMhz::of(0);
    os::WifiSecurity security = os::WifiSecurity::Open;
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
