// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input_wire.h"
#include "hal/hdmi_int.h"

namespace mister::testing {

struct QuietHdmi final : hal::IHdmiInterrupt {
    bool hdmi_int_asserted() const override { return asserted; }

    bool asserted = false;
};

struct QuietVideoEnv {
    app::InputWire activity;
    QuietHdmi hdmi;
};

}
