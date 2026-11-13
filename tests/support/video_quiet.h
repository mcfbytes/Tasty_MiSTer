// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input_gates_cell.h"
#include "hal/hdmi_int.h"
#include "infra/counter.h"

namespace mister::testing {

struct QuietHdmi final : hal::IHdmiInterrupt {
    bool hdmi_int_asserted() const override { return asserted; }

    bool asserted = false;
};

struct QuietVideoEnv {
    xthread::Counter activity;
    app::InputGatesCell gates;
    QuietHdmi hdmi;
};

}
