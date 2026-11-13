// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/bssid.h"
#include "os/ssid.h"
#include "os/wpa_network_id.h"

namespace mister::os {

struct WpaNetworkListRow {
    TASTY_SEAT_EXEMPT(component);

    WpaNetworkId id = *WpaNetworkId::of(0);
    Ssid ssid{};
    std::optional<Bssid> bssid{};
    bool current = false;
    bool disabled = false;
};

static_assert(std::is_trivially_copyable_v<WpaNetworkListRow>);

class WpaNetworkList {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxRows = 32;

    [[nodiscard]] static Ex<WpaNetworkList> parse(std::string_view text) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] std::span<const WpaNetworkListRow> rows() const noexcept {
        return std::span<const WpaNetworkListRow>{rows_.data(), count_};
    }

private:
    std::array<WpaNetworkListRow, kMaxRows> rows_{};
    std::size_t count_ = 0;
};

static_assert(std::is_trivially_copyable_v<WpaNetworkList>);

}
