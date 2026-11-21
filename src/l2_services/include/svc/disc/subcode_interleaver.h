// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/seat.h"
#include "svc/disc/track.h"

namespace mister::svc::disc {

using SubcodeBlock = std::span<std::byte, kSubcodeBytes>;

class SubcodeInterleaver {
    TASTY_SEAT_RESIDENT(Io);

public:
    void shape(SubcodeBlock block) const noexcept;
};

}
