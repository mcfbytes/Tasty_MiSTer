// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores::atari8 {

enum class RebootPhase : std::uint8_t {
    Rest,
    Mirror,
    ResetHead,
    ResetClear,
    Head,
    Fill,
    Probe,
    Line,
};

}
