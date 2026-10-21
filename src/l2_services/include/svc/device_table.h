// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "svc/input_device.h"
#include "infra/seat.h"

namespace mister::svc {

struct DeviceTable {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::vector<InputDevice> devices;

    std::uint32_t mapped = 0;
};

}
