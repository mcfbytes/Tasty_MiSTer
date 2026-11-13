// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"
#include "os/bssid.h"
#include "os/freq_mhz.h"
#include "os/ipv4_addr.h"
#include "os/ssid.h"
#include "os/wpa_network_id.h"
#include "os/wpa_state.h"

namespace mister::os {

inline constexpr std::size_t kWpaStatusKeyMgmtChars = 32;

class WpaStatus {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<WpaStatus> parse(std::string_view text) noexcept;

    [[nodiscard]] WpaState state() const noexcept { return state_; }

    [[nodiscard]] const std::optional<Ssid>& ssid() const noexcept { return ssid_; }

    [[nodiscard]] const std::optional<Bssid>& bssid() const noexcept { return bssid_; }

    [[nodiscard]] const std::optional<FreqMhz>& freq() const noexcept { return freq_; }

    [[nodiscard]] const std::optional<WpaNetworkId>& id() const noexcept { return id_; }

    [[nodiscard]] const std::optional<Ipv4Addr>& ip() const noexcept { return ip_; }

    [[nodiscard]] std::string_view key_mgmt() const noexcept { return key_mgmt_.view(); }

private:
    WpaState state_ = WpaState::Unknown;
    std::optional<Ssid> ssid_{};
    std::optional<Bssid> bssid_{};
    std::optional<FreqMhz> freq_{};
    std::optional<WpaNetworkId> id_{};
    std::optional<Ipv4Addr> ip_{};
    FixedStr<kWpaStatusKeyMgmtChars, StrFit::Clip> key_mgmt_{};
};

static_assert(std::is_trivially_copyable_v<WpaStatus>);

}
