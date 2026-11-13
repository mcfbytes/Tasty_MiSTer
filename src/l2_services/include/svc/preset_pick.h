// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/types.h"

namespace mister::svc::video_timing {

struct PresetPick {
    PresetIndex index{};
    bool hpol = false;
    bool vpol = false;
};

}
