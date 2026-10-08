// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/activity_source.h"
#include "hal/hdmi_int.h"

namespace mister::testing {

struct QuietActivity final : app::IActivitySource {
    std::uint32_t activity_seq() const noexcept override { return seq; }
    bool input_grabbed() const noexcept override { return grabbed; }
    std::uint32_t seq = 0;
    bool grabbed = false;
};

struct QuietHdmi final : hal::IHdmiInterrupt {
    bool hdmi_int_asserted() const override { return asserted; }
    bool asserted = false;
};

struct QuietVideoEnv {
    QuietActivity activity;
    QuietHdmi hdmi;
};

}  // namespace mister::testing
