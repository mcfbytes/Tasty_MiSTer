// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/usb_device_count.h"

namespace mister::os {

class UsbAdapterCycle {
    TASTY_SEAT_RESIDENT(Connect);

public:
    static constexpr std::string_view kSysfsRoot{"/sys/bus/usb/devices"};

    explicit UsbAdapterCycle(std::string root = std::string{kSysfsRoot}) noexcept
        : root_(std::move(root)) {}

    [[nodiscard]] Ex<UsbDeviceCount> cycle() const noexcept;

private:
    std::string root_;
};

}
