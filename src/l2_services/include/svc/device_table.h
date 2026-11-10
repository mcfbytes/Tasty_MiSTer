// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "svc/input_device.h"
#include "infra/seat.h"

namespace mister::svc {

struct DeviceTable {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr bool kResetOnReap = true;

    static constexpr unsigned kMaxDevices = DeviceSlot::kCount;
    std::vector<InputDevice> devices;

    std::uint32_t mapped = 0;

    [[nodiscard]] bool full() const noexcept { return devices.size() >= kMaxDevices; }

    [[nodiscard]] Ex<void> add(InputDevice&& dev) {
        if (full())
            return std::unexpected(Error{Errc::slot_range, ERR_SITE(), kMaxDevices});
        devices.push_back(std::move(dev));
        return {};
    }
};

}
