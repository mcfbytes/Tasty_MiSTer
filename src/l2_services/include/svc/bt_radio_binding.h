// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>

#include "infra/error.h"
#include "os/hci_reset.h"
#include "os/usb_device_count.h"

namespace mister::svc {

template <class R>
concept BtRadioBinding = requires(R& radio) {
    { radio.adapter_up() } -> std::same_as<Ex<bool>>;
    { radio.reset_adapter() } -> std::same_as<Ex<os::HciResetOutcome>>;
    { radio.cycle_usb() } -> std::same_as<Ex<os::UsbDeviceCount>>;
};

}
