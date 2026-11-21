// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/tally.h"
#include "os/raw_monotonic_clock.h"
#include "proto/save_stage_types.h"
#include "proto/stage_key.h"

namespace mister::proto {

enum class StageState : std::uint8_t { Free, Loading, Live, Retiring, Broken };

struct SaveStageRow {
    StageKey key{};
    StageState state = StageState::Free;
    bool present = false;
    StageRevision rev{};
    StageRevision saved{};
    os::RawMonotonicClock::time_point last_write{};
    infra::Tally commits{};
    infra::Tally skipped_identical{};
    infra::Tally faults{};
    std::optional<Error> last_fault{};
    infra::Tally declines{};
};

static_assert(std::is_trivially_copyable_v<SaveStageRow>);

}
