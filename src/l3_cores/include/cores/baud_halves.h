// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "hal/spi_halves.h"

namespace mister::cores {

constexpr hal::SpiHalves BaudRate::halves() const noexcept {
    return hal::SpiHalves::of(v);
}

}
