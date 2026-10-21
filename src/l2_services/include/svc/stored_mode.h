// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/modeline.h"

namespace mister::svc::video_timing {

struct StoredMode {
    Modeline mode{};
    bool explicitly_requested = false;
    bool fully_custom = false;
};

}
