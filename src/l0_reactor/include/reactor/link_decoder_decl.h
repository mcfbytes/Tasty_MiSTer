// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "reactor/cause_set.h"

namespace mister::reactor {

class ILinkDecoder;

enum class RoundClaim : std::uint8_t { Never, Always, OnWork };

inline constexpr std::chrono::milliseconds kEveryRound{0};

struct LinkDecoderDecl {
    const char* name;
    Cause cause;
    const ILinkDecoder* impl;
    std::chrono::milliseconds period;
    RoundClaim round_claim;
};

}
