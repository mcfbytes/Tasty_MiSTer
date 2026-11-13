// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace mister::os {

enum class WifiSecurity : std::uint8_t {
    Open,
    Psk,
    PskSae,
    Sae,
    Unsupported,
};

inline constexpr std::uint8_t kWifiIeee80211wOff = 0;
inline constexpr std::uint8_t kWifiIeee80211wOptional = 1;
inline constexpr std::uint8_t kWifiIeee80211wRequired = 2;

[[nodiscard]] WifiSecurity wifi_security_from_flags(std::string_view flags) noexcept;

[[nodiscard]] std::string_view wifi_security_key_mgmt(WifiSecurity security) noexcept;

[[nodiscard]] std::uint8_t wifi_security_ieee80211w(WifiSecurity security) noexcept;

static_assert(std::is_trivially_copyable_v<WifiSecurity>);
static_assert(std::has_unique_object_representations_v<WifiSecurity>);

}
