// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

#include "svc/filter_kernel.h"
#include "svc/filter_phase.h"

namespace mister::svc {

struct FilterBank {
    bool is_adaptive = false;
    bool digest_valid = false;
    std::array<FilterPhase, kFilterPhases> phases{};
    std::array<FilterPhase, kFilterPhases> adaptive{};
};

}
