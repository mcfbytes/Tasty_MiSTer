// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/autosave_policy.h"
#include "proto/in_place_opens.h"
#include "proto/save_stage_report.h"
#include "proto/save_stage_row.h"
#include "proto/save_stage_types.h"
#include "proto/stage_key.h"

namespace mister::proto {

enum class SaveBackup : std::uint64_t { None, SessionStart };

struct SaveStageOrder {
    TASTY_SEAT_EXEMPT(component);
    enum class Kind : std::uint64_t { Commit, CommitAll, kCount };

    struct Commit {
        static constexpr Kind kKind = Kind::Commit;
        StageKey key{};
        StageRevision rev{};
        SaveBackup backup = SaveBackup::None;
    };

    struct CommitAll {
        static constexpr Kind kKind = Kind::CommitAll;
        CommitAllSeq seq{};
    };

    static constexpr std::size_t kStore = sizeof(Commit);
    static_assert(sizeof(Commit) >= sizeof(CommitAll), "the store holds every alternative");

    using Alternatives = std::tuple<Commit, CommitAll>;

    Kind kind = Kind::Commit;
    alignas(alignof(Commit)) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<SaveStageOrder> && infra::alternatives_are_total<SaveStageOrder>());

}
