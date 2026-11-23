// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/byte_count.h"
#include "svc/sector_types.h"

namespace mister::svc {

enum class SectorOutcome : std::uint8_t { Good, Faulty };

struct SectorRead {
    infra::ByteCount bytes{};
    SectorStatus status = kSectorClear;
    SectorOutcome outcome = SectorOutcome::Good;

    std::chrono::microseconds busy{};

    [[nodiscard]] constexpr bool good() const noexcept { return outcome == SectorOutcome::Good; }
};

}
