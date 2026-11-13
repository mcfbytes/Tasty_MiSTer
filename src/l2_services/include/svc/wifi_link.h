// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "os/wpa_state.h"

namespace mister::svc {

enum class WifiLink : std::uint8_t {
    Disconnected,
    Scanning,
    Associating,
    Handshake,
    Connected,
    Disabled,
};

[[nodiscard]] constexpr WifiLink wifi_link_from_state(os::WpaState state) noexcept {
    switch (state) {
        case os::WpaState::Disconnected:
        case os::WpaState::Inactive:
            return WifiLink::Disconnected;
        case os::WpaState::InterfaceDisabled:
            return WifiLink::Disabled;
        case os::WpaState::Scanning:
            return WifiLink::Scanning;
        case os::WpaState::Authenticating:
        case os::WpaState::Associating:
        case os::WpaState::Associated:
            return WifiLink::Associating;
        case os::WpaState::FourWayHandshake:
        case os::WpaState::GroupHandshake:
            return WifiLink::Handshake;
        case os::WpaState::Completed:
            return WifiLink::Connected;
        case os::WpaState::Unknown:
            return WifiLink::Disconnected;
    }
    return WifiLink::Disconnected;
}

static_assert(std::is_trivially_copyable_v<WifiLink>);
static_assert(std::has_unique_object_representations_v<WifiLink>);

}
