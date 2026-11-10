// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/modeline.h"
#include "svc/resolution.h"

namespace mister::svc {

Modeline calculate_cvt(
    Resolution active,
    float refresh_rate,
    ReducedBlanking reduced_blanking,
    bool supports_pr
);

}
