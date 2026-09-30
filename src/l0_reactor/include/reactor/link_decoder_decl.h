// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "reactor/cause_set.h"

namespace mister::reactor {

class ILinkDecoder;

enum class DeadlineClass : std::uint8_t {
    A,
    B,
};

enum class OsdBudget : std::uint8_t { Shared, Pinned };

struct LinkDecoderDecl {
    const char* name;
    Cause cause;
    const ILinkDecoder* impl;
    std::uint16_t period_ms;
    DeadlineClass klass;
    OsdBudget osd_budget;
};

}  // namespace mister::reactor
