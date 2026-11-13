// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>

#include "infra/seat.h"
#include "os/bt_addr.h"
#include "os/bt_kind.h"
#include "os/bt_name.h"
#include "os/rssi_dbm.h"
#include "svc/view_gen.h"

namespace mister::svc {

struct BtDeviceViewRow {
    TASTY_SEAT_EXEMPT(component);

    os::BtAddr addr{};
    os::BtName name{};
    os::BtKind kind = os::BtKind::Other;
    bool paired = false;
    bool trusted = false;
    bool connected = false;
    std::optional<os::RssiDbm> rssi{};

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
