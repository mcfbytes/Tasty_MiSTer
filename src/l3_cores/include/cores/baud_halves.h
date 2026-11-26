// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"
#include "hal/link_halves.h"

namespace mister::cores {

constexpr hal::LinkHalves BaudRate::halves() const noexcept {
    return hal::LinkHalves::of(v);
}

}
