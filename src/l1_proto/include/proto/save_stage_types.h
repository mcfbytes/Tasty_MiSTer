// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "infra/serial.h"

namespace mister::proto {

struct SaveStageSlot {
    std::uint32_t v = 0;
    friend constexpr bool operator==(SaveStageSlot, SaveStageSlot) = default;
    friend constexpr auto operator<=>(SaveStageSlot, SaveStageSlot) = default;
};

struct SaveStageSlotCount {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    friend constexpr bool operator==(SaveStageSlotCount, SaveStageSlotCount) = default;
    friend constexpr auto operator<=>(SaveStageSlotCount, SaveStageSlotCount) = default;
};

struct StageEpoch {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::ThroughZero<std::uint32_t>;

    [[nodiscard]] constexpr StageEpoch next() const noexcept { return infra::serial::next(*this); }

    friend constexpr bool operator==(StageEpoch, StageEpoch) = default;
    friend constexpr auto operator<=>(StageEpoch, StageEpoch) = default;
};

struct StageRevision {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr StageRevision next() const noexcept { return StageRevision{v + 1u}; }

    friend constexpr bool operator==(StageRevision, StageRevision) = default;
    friend constexpr auto operator<=>(StageRevision, StageRevision) = default;
};

struct CommitAllSeq {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::ThroughZero<std::uint32_t>;

    [[nodiscard]] constexpr CommitAllSeq next() const noexcept {
        return infra::serial::next(*this);
    }

    friend constexpr bool operator==(CommitAllSeq, CommitAllSeq) = default;
    friend constexpr auto operator<=>(CommitAllSeq, CommitAllSeq) = default;
};

}
