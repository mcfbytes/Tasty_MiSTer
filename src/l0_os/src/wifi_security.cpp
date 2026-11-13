// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/wifi_security.h"

#include <string_view>

#include "infra/ascii.h"

namespace mister::os {
namespace {

constexpr std::string_view kSae{"SAE"};
constexpr std::string_view kPsk{"PSK"};
constexpr std::string_view kWpa{"WPA"};
constexpr std::string_view kRsn{"RSN"};
constexpr std::string_view kWep{"WEP"};
constexpr std::string_view kEap{"EAP"};
constexpr std::string_view kOwe{"OWE"};
constexpr std::string_view kKeyMgmtNone{"NONE"};
constexpr std::string_view kKeyMgmtPsk{"WPA-PSK"};
constexpr std::string_view kKeyMgmtPskSae{"WPA-PSK SAE"};
constexpr std::string_view kKeyMgmtSae{"SAE"};

[[nodiscard]] bool has(std::string_view flags, std::string_view token) noexcept {
    return infra::ifind(flags, token).has_value();
}

[[nodiscard]] bool blank(std::string_view flags) noexcept {
    return flags.find_first_not_of(" \t") == std::string_view::npos;
}

}

WifiSecurity wifi_security_from_flags(std::string_view flags) noexcept {
    const bool sae = has(flags, kSae);
    const bool psk = has(flags, kPsk);
    if (sae && psk)
        return WifiSecurity::PskSae;
    if (sae)
        return WifiSecurity::Sae;
    if (psk)
        return WifiSecurity::Psk;

    if (has(flags, kWpa)
        || has(flags, kRsn)
        || has(flags, kWep)
        || has(flags, kEap)
        || has(flags, kOwe)) {
        return WifiSecurity::Unsupported;
    }

    if (blank(flags))
        return WifiSecurity::Unsupported;
    return WifiSecurity::Open;
}

std::string_view wifi_security_key_mgmt(WifiSecurity security) noexcept {
    switch (security) {
        case WifiSecurity::Open:
            return kKeyMgmtNone;
        case WifiSecurity::Psk:
            return kKeyMgmtPsk;
        case WifiSecurity::PskSae:
            return kKeyMgmtPskSae;
        case WifiSecurity::Sae:
            return kKeyMgmtSae;
        case WifiSecurity::Unsupported:
            return {};
    }
    return {};
}

std::uint8_t wifi_security_ieee80211w(WifiSecurity security) noexcept {
    switch (security) {
        case WifiSecurity::PskSae:
            return kWifiIeee80211wOptional;
        case WifiSecurity::Sae:
            return kWifiIeee80211wRequired;
        case WifiSecurity::Open:
        case WifiSecurity::Psk:
        case WifiSecurity::Unsupported:
            return kWifiIeee80211wOff;
    }
    return kWifiIeee80211wOff;
}

}
