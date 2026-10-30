// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "reactor/cause_set.h"

namespace mister::reactor {

class ILinkDecoder;

enum class RoundClaim : std::uint8_t { Never, Always, OnWork };

struct LinkDecoderDecl {
    const char* name;
    Cause cause;
    const ILinkDecoder* impl;
    std::uint16_t period_ms;
    RoundClaim round_claim;
};

}
