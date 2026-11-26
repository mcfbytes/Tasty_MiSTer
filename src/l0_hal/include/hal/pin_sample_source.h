// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/pin_sample.h"

namespace mister::hal {

class IPinSampleSource {
public:
    virtual ~IPinSampleSource() = default;

    [[nodiscard]] virtual PinSample sample() const noexcept = 0;
};

}
