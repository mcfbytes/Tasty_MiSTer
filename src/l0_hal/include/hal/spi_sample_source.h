// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/spi_sample.h"

namespace mister::hal {

class ISpiSampleSource {
public:
    virtual ~ISpiSampleSource() = default;

    [[nodiscard]] virtual SpiSample sample() const noexcept = 0;
};

}  // namespace mister::hal
