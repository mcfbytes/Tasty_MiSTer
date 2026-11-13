// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::os {

class UsbDeviceCount {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr UsbDeviceCount() noexcept = default;

    [[nodiscard]] constexpr UsbDeviceCount plus_one() const noexcept {
        return UsbDeviceCount{count_ + 1};
    }

    [[nodiscard]] constexpr bool none() const noexcept { return count_ == 0; }

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return count_; }

    friend constexpr bool operator==(const UsbDeviceCount&, const UsbDeviceCount&) = default;

private:
    constexpr explicit UsbDeviceCount(std::uint32_t count) noexcept : count_(count) {}

    std::uint32_t count_ = 0;
};

static_assert(std::is_trivially_copyable_v<UsbDeviceCount>);
static_assert(std::has_unique_object_representations_v<UsbDeviceCount>);

}
