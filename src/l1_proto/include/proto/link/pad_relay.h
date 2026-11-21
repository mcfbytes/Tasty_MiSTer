// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/indexed_array.h"
#include "infra/tally.h"
#include "proto/build_generation.h"
#include "proto/link/pad_levels.h"
#include "proto/pad_word.h"

namespace mister::proto::link {

struct PadRelay {

    struct Level {
        PadWord word{};
        infra::Tally writes{};
        friend constexpr bool operator==(const Level&, const Level&) = default;
    };

    BuildGeneration gen{};
    bool gated = false;
    infra::Tally seq{};
    infra::IndexedArray<Level, PadLevels::kCount, PadLevels::Slot> levels{};
};

static_assert(std::is_trivially_copyable_v<PadRelay>);

}
