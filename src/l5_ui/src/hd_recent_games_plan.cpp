// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_recent_games_plan.h"

#include "app/recent_games.h"
#include "ui/hd_plan_inputs.h"

namespace mister::ui {

HdRecentGamesPlan::HdRecentGamesPlan(const HdPlanInputs& in) : launch_(in) {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    const std::span<const app::RecentGames::Record> records = in.recent_games.records();
    rows_.reserve(records.size());
    refs_.reserve(records.size());
    for (const app::RecentGames::Record& r : records) {
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

std::span<const std::reference_wrapper<IHdRow>> HdRecentGamesPlan::rows() const {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return refs_;
}

std::uint32_t HdRecentGamesPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdRecentGamesPlan);
    return refusals_ + launch_.refusals();
}

}
