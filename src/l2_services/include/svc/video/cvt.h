// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/video/modeline.h"
#include "svc/video/resolution.h"

namespace mister::svc::video {

Modeline calculate_cvt(
    Resolution active,
    float refresh_rate,
    ReducedBlanking reduced_blanking,
    bool supports_pr
);

}
