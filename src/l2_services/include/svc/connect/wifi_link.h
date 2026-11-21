// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "os/connect/wpa_state.h"

namespace mister::svc::connect {

enum class WifiLink : std::uint8_t {
    Disconnected,
    Scanning,
    Associating,
    Handshake,
    Connected,
    Disabled,
};

[[nodiscard]] constexpr WifiLink wifi_link_from_state(os::connect::WpaState state) noexcept {
    switch (state) {
        case os::connect::WpaState::Disconnected:
        case os::connect::WpaState::Inactive:
            return WifiLink::Disconnected;
        case os::connect::WpaState::InterfaceDisabled:
            return WifiLink::Disabled;
        case os::connect::WpaState::Scanning:
            return WifiLink::Scanning;
        case os::connect::WpaState::Authenticating:
        case os::connect::WpaState::Associating:
        case os::connect::WpaState::Associated:
            return WifiLink::Associating;
        case os::connect::WpaState::FourWayHandshake:
        case os::connect::WpaState::GroupHandshake:
            return WifiLink::Handshake;
        case os::connect::WpaState::Completed:
            return WifiLink::Connected;
        case os::connect::WpaState::Unknown:
            return WifiLink::Disconnected;
    }
    return WifiLink::Disconnected;
}

static_assert(std::is_trivially_copyable_v<WifiLink>);
static_assert(std::has_unique_object_representations_v<WifiLink>);

}
