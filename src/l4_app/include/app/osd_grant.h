// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "hal/reg_lend.h"
#include "infra/seat.h"
#include "infra/telemetry.h"

namespace mister::app {

enum class OsdPageRoute : std::uint8_t { Link, Lw };

struct OsdGrant {
    hal::reg::Lend block{};
    std::uint16_t gen = 0;

    [[nodiscard]] bool live() const noexcept { return block.lent(); }
};

static_assert(std::is_trivially_copyable_v<OsdGrant>);
using OsdGrantCell = xthread::Telemetry<OsdGrant, SeatTag::Main>;

}
