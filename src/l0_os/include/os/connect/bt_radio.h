// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "os/bt_probe.h"
#include "os/connect/hci_index.h"
#include "os/connect/hci_reset.h"
#include "os/connect/usb_adapter_cycle.h"

namespace mister::os::connect {

class BtRadio {
    TASTY_SEAT_RESIDENT(Connect);

public:
    BtRadio() noexcept = default;

    explicit BtRadio(UsbAdapterCycle cycle) noexcept : cycle_(std::move(cycle)) {}

    [[nodiscard]] Ex<bool> adapter_up() const noexcept { return bluetooth_adapter_up(); }

    [[nodiscard]] Ex<HciResetOutcome> reset_adapter() const noexcept {
        return HciReset::reset(HciIndex::first());
    }

    [[nodiscard]] Ex<UsbDeviceCount> cycle_usb() const noexcept { return cycle_.cycle(); }

private:
    UsbAdapterCycle cycle_{};
};

}
