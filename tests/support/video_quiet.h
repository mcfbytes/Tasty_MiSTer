// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input/input_gates_cell.h"
#include "hal/hdmi_int.h"
#include "infra/xthread/counter.h"
#include "infra/prelude.h"

namespace mister::testing {

struct QuietHdmi final : hal::IHdmiInterrupt {
    bool hdmi_int_asserted() const override { return asserted; }

    bool asserted = false;
};

struct QuietVideoEnv {
    xthread::Counter activity;
    app::input::InputGatesCell gates;
    QuietHdmi hdmi;
};

}
