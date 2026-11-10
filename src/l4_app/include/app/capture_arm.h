// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/types.h"

namespace mister::app {

struct CaptureArm {
    CaptureToken token{};
    bool armed = false;
};

}
