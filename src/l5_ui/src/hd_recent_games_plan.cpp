// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_recent_games_plan.h"

#include "app/recents/recent_games.h"
#include "ui/hd_plan_inputs.h"
#include "infra/tally.h"

namespace mister::ui {

HdRecentGamesPlan::HdRecentGamesPlan(const HdPlanInputs& in) : launch_(in) {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    const std::span<const app::recents::RecentGames::Record> records = in.recent_games.records();
    rows_.reserve(records.size());
    refs_.reserve(records.size());
    for (const app::recents::RecentGames::Record& r : records) {
        rows_.emplace_back(launch_, r);
        refs_.emplace_back(rows_.back());
    }

    refusals_ = in.recent_games.hidden();
}

PageId HdRecentGamesPlan::id() const {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return PageId::HdRecentGames;
}

std::string_view HdRecentGamesPlan::title() const {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return "Recent games";
}

std::span<const std::reference_wrapper<IRow>> HdRecentGamesPlan::rows() const {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return refs_;
}

infra::Tally HdRecentGamesPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return refusals_.sum(launch_.refusals());
}

}
