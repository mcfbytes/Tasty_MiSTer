// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/bssid.h"
#include "os/country_code.h"
#include "os/freq_mhz.h"
#include "os/if_name.h"
#include "os/ipv4_addr.h"
#include "os/signal_dbm.h"
#include "os/ssid.h"
#include "os/wpa_state.h"
#include "svc/view_gen.h"
#include "svc/wifi_failure.h"
#include "svc/wifi_link.h"
#include "svc/wifi_presence.h"

namespace mister::svc {

struct WifiView {
    TASTY_SEAT_EXEMPT(component);

    ViewGen gen{};
    WifiPresence presence = WifiPresence::NoAdapter;
    WifiLink link = WifiLink::Disconnected;
    WifiFailure failure = WifiFailure::None;
    std::optional<os::Ssid> ssid{};
    std::optional<os::Ipv4Addr> ipv4{};
    std::optional<os::SignalDbm> signal{};
    std::optional<os::FreqMhz> freq{};

    std::optional<os::IfName> ifname{};
    std::optional<os::Bssid> bssid{};
    os::WpaState wpa_state = os::WpaState::Unknown;
    std::optional<os::CountryCode> country{};
};

static_assert(std::is_trivially_copyable_v<WifiView>);

}
