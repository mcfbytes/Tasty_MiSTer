// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/link_encoder.h"

namespace mister::app {

struct Delivered {

    enum class Edge : std::uint8_t { None, DownloadClosed, ResetPulsed };

    ILinkEncoder::Outcome outcome = ILinkEncoder::Outcome::Dropped;
    bool round_spent = false;
    Edge edge = Edge::None;
};

}
