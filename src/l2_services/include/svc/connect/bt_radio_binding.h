// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>

#include "infra/error.h"
#include "infra/prelude.h"
#include "os/connect/hci_reset.h"
#include "os/connect/usb_device_count.h"

namespace mister::svc::connect {

template <class R>
concept BtRadioBinding = requires(R& radio) {
    { radio.adapter_up() } -> std::same_as<Ex<bool>>;
    { radio.reset_adapter() } -> std::same_as<Ex<os::connect::HciResetOutcome>>;
    { radio.cycle_usb() } -> std::same_as<Ex<os::connect::UsbDeviceCount>>;
};

}
