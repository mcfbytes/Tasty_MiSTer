// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::os {

enum class WpaState : std::uint8_t {
    Disconnected,
    Inactive,
    InterfaceDisabled,
    Scanning,
    Authenticating,
    Associating,
    Associated,
    FourWayHandshake,
    GroupHandshake,
    Completed,
    Unknown,
};

struct WpaStateRow {
    std::string_view text;
    WpaState state;
};

inline constexpr std::size_t kWpaStateCount = 10;
inline constexpr std::array<WpaStateRow, kWpaStateCount> kWpaStateRows{{
    {"DISCONNECTED", WpaState::Disconnected},
    {"INACTIVE", WpaState::Inactive},
    {"INTERFACE_DISABLED", WpaState::InterfaceDisabled},
    {"SCANNING", WpaState::Scanning},
    {"AUTHENTICATING", WpaState::Authenticating},
    {"ASSOCIATING", WpaState::Associating},
    {"ASSOCIATED", WpaState::Associated},
    {"4WAY_HANDSHAKE", WpaState::FourWayHandshake},
    {"GROUP_HANDSHAKE", WpaState::GroupHandshake},
    {"COMPLETED", WpaState::Completed},
}};

inline constexpr std::string_view kWpaStateUnknown{"UNKNOWN"};

[[nodiscard]] constexpr WpaState wpa_state_from_text(std::string_view text) noexcept {
    for (const WpaStateRow& row : kWpaStateRows) {
        if (row.text == text)
            return row.state;
    }
    return WpaState::Unknown;
}

[[nodiscard]] constexpr std::string_view wpa_state_text(WpaState state) noexcept {
    for (const WpaStateRow& row : kWpaStateRows) {
        if (row.state == state)
            return row.text;
    }
    return kWpaStateUnknown;
}

static_assert(std::is_trivially_copyable_v<WpaState>);
static_assert(std::has_unique_object_representations_v<WpaState>);

}
