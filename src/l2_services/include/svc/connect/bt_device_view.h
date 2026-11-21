// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/connect/bt_addr.h"
#include "os/connect/bt_kind.h"
#include "os/connect/bt_name.h"
#include "os/connect/rssi_dbm.h"
#include "svc/connect/view_gen.h"

namespace mister::svc::connect {

struct BtDeviceViewRow {
    TASTY_SEAT_EXEMPT(component);

    os::connect::BtAddr addr{};
    os::connect::BtName name{};
    os::connect::BtKind kind = os::connect::BtKind::Other;
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    std::optional<os::connect::RssiDbm> rssi{};

    friend constexpr bool operator==(const BtDeviceViewRow&, const BtDeviceViewRow&) = default;
};

struct BtDeviceView {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kRows = 16;

    ViewGen gen{};
    std::array<std::optional<BtDeviceViewRow>, kRows> rows{};
};

static_assert(std::is_trivially_copyable_v<BtDeviceViewRow>);
static_assert(std::is_trivially_copyable_v<BtDeviceView>);

}
