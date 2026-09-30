// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "hal/hdmi_int.h"
#include "hal/pin_levels.h"

namespace mister::hal {

class HdmiIntLevel final : public IHdmiInterrupt {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdmiIntLevel(const PinLevelCell& cell) noexcept : cell_(&cell) {}

    bool hdmi_int_asserted() const override {
        TASTY_SEAT_BODY(HdmiIntLevel);
        const auto s = cell_->sample();
        return s && s.value.hdmi_int;
    }

private:
    const PinLevelCell* cell_;
};

}  // namespace mister::hal
