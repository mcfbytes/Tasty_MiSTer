// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/ssid.h"
#include "os/wpa_network_id.h"
#include "svc/view_gen.h"

namespace mister::svc {

struct WifiSavedViewRow {
    TASTY_SEAT_EXEMPT(component);

    os::WpaNetworkId id = *os::WpaNetworkId::of(0);
    os::Ssid ssid{};
    bool current = false;
    bool disabled = false;
};

static_assert(std::is_trivially_copyable_v<WifiSavedViewRow>);

struct WifiSavedView {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kRows = 16;

    ViewGen gen{};
    std::array<std::optional<WifiSavedViewRow>, kRows> rows{};
};

static_assert(std::is_trivially_copyable_v<WifiSavedView>);

}
