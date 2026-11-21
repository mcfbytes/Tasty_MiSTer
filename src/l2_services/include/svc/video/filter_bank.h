// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "svc/video/filter_kernel.h"
#include "svc/video/filter_phase.h"

namespace mister::svc::video {

struct FilterBank {
    bool is_adaptive = false;
    bool digest_valid = false;
    std::array<FilterPhase, kFilterPhases> phases{};
    std::array<FilterPhase, kFilterPhases> adaptive{};
};

}
