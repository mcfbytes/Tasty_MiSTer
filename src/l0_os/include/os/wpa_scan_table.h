// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/bssid.h"
#include "os/freq_mhz.h"
#include "os/signal_dbm.h"
#include "os/ssid.h"
#include "os/wifi_security.h"

namespace mister::os {

struct WpaScanTableRow {
    TASTY_SEAT_EXEMPT(component);

    Bssid bssid{};
    FreqMhz freq = FreqMhz::of(0);
    SignalDbm signal = SignalDbm::of(0);
    Ssid ssid{};
    WifiSecurity security = WifiSecurity::Open;
};

static_assert(std::is_trivially_copyable_v<WpaScanTableRow>);
static_assert(std::has_unique_object_representations_v<WpaScanTableRow>);
static_assert(sizeof(WpaScanTableRow) == 44);

class WpaScanTable {
    TASTY_SEAT_EXEMPT(component);

public:
    static constexpr std::size_t kMaxRows = 64;

    [[nodiscard]] static Ex<WpaScanTable> parse(std::string_view text) noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return count_; }

    [[nodiscard]] std::span<const WpaScanTableRow> rows() const noexcept {
        return std::span<const WpaScanTableRow>{rows_.data(), count_};
    }

private:
    std::array<WpaScanTableRow, kMaxRows> rows_{};
    std::size_t count_ = 0;
};

static_assert(std::is_trivially_copyable_v<WpaScanTable>);
static_assert(std::has_unique_object_representations_v<WpaScanTable>);

}
