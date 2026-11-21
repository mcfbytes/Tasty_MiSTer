// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/connect/bssid.h"
#include "os/connect/country_code.h"
#include "os/connect/freq_mhz.h"
#include "os/connect/if_name.h"
#include "os/connect/ipv4_addr.h"
#include "os/connect/signal_dbm.h"
#include "os/connect/ssid.h"
#include "os/connect/wpa_state.h"
#include "svc/connect/view_gen.h"
#include "svc/connect/wifi_failure.h"
#include "svc/connect/wifi_link.h"
#include "svc/connect/wifi_presence.h"

namespace mister::svc::connect {

struct WifiView {
    TASTY_SEAT_EXEMPT(component);

    ViewGen gen{};
    WifiPresence presence = WifiPresence::NoAdapter;
    WifiLink link = WifiLink::Disconnected;
    WifiFailure failure = WifiFailure::None;
    std::optional<os::connect::Ssid> ssid{};
    std::optional<os::connect::Ipv4Addr> ipv4{};
    std::optional<os::connect::SignalDbm> signal{};
    std::optional<os::connect::FreqMhz> freq{};

    std::optional<os::connect::IfName> ifname{};
    std::optional<os::connect::Bssid> bssid{};
    os::connect::WpaState wpa_state = os::connect::WpaState::Unknown;
    std::optional<os::connect::CountryCode> country{};
};

static_assert(std::is_trivially_copyable_v<WifiView>);

}
