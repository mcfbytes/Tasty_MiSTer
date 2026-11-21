// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <type_traits>

#include "infra/indexed_array.h"
#include "infra/tally.h"
#include "proto/autosave_policy.h"
#include "proto/in_place_opens.h"
#include "proto/save_stage_row.h"
#include "proto/save_stage_types.h"

namespace mister::proto {

struct SaveStageReport {

    struct Summary {
        InPlaceOpens in_place{};
        infra::Tally in_place_unmatched{};
        infra::Tally unstaged_adopts{};
        infra::Tally orders_stale{};
        infra::Tally loads_rushed{};
        infra::Tally demotions{};
        infra::Tally in_place_fallbacks{};
        infra::Tally backups_skipped{};
        CommitAllSeq commit_all_served{};

        [[nodiscard]] constexpr bool none_in_place() const noexcept {
            return in_place.none() && in_place_unmatched == infra::Tally{};
        }
    };

    infra::IndexedArray<SaveStageRow, kMaxStagedSaves.size(), SaveStageSlot> rows{};
    Summary summary{};
};

static_assert(std::is_trivially_copyable_v<SaveStageReport>);

}
