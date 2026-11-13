// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "hal/reg_lend.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "os/uio_line_ref.h"
#include "proto/build_generation.h"

namespace mister::app {

enum class StatusRoute : std::uint8_t { Link, Lw };

struct SysGrant {
    hal::reg::Lend block{};
    os::UioLineRef line{};
    proto::BuildGeneration gen{};

    [[nodiscard]] bool live() const noexcept { return block.lent() && line.fd() >= 0; }
};

static_assert(std::is_trivially_copyable_v<SysGrant>);
using SysGrantCell = xthread::Telemetry<SysGrant, SeatTag::Main>;

}
