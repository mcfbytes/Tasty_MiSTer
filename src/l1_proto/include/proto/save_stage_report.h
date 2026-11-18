// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/indexed_array.h"
#include "proto/autosave_policy.h"
#include "proto/in_place_opens.h"
#include "proto/save_stage_row.h"
#include "proto/save_stage_types.h"

namespace mister::proto {

struct SaveStageReport {

    struct Summary {
        InPlaceOpens in_place{};
        StageTally in_place_unmatched{};
        StageTally unstaged_adopts{};
        StageTally orders_stale{};
        StageTally loads_rushed{};
        StageTally demotions{};
        StageTally in_place_fallbacks{};
        StageTally backups_skipped{};
        CommitAllSeq commit_all_served{};

        [[nodiscard]] constexpr bool none_in_place() const noexcept {
            return in_place.none() && in_place_unmatched == StageTally{};
        }
    };

    infra::IndexedArray<SaveStageRow, kMaxStagedSaves.size(), SaveStageSlot> rows{};
    Summary summary{};
};

static_assert(std::is_trivially_copyable_v<SaveStageReport>);

}
