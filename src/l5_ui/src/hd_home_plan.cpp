// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_home_plan.h"

#include <algorithm>
#include <cstddef>

#include "app/recent_games.h"
#include "ui/hd_plan_inputs.h"
#include "ui/page_context.h"
#include "ui/system_page.h"

namespace mister::ui {
namespace {

constexpr std::size_t kInlineRecent = 5;

}

HdHomePlan::HdHomePlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      cores_(make_system_page(in.ctx.sys)),
      launch_(in),
      pair_(in.ctx),
      all_row_(HdPushRow::Spec{.label = "All recent games", .page = PageId::HdRecentGames}),
      settings_(HdPushRow::Spec{.label = "Settings", .page = PageId::HdSettings}) {
    TASTY_SEAT_BODY(HdHomePlan);
    cores_->on_enter(ctx_);
    const std::span<const app::RecentGames::Record> records = in.recent_games.records();
    has_any_ = !records.empty();
    const std::size_t shown = std::min(kInlineRecent, records.size());
    played_.reserve(shown);
    refs_.reserve(shown + 5);
    for (std::size_t i = 0; i < shown; ++i) {
        played_.emplace_back(launch_, records[i]);
        refs_.emplace_back(played_.back());
    }
    refs_.emplace_back(all_gate_);
    refs_.emplace_back(browse_);
    refs_.emplace_back(pair_);
    refs_.emplace_back(separator_);
    refs_.emplace_back(settings_);
    if (!ctx_.launches.script)
        ++refusals_;
}

PageId HdHomePlan::id() const {
    TASTY_SEAT_BODY(HdHomePlan);
    return PageId::HdMenu;
}

std::string_view HdHomePlan::title() const {
    TASTY_SEAT_BODY(HdHomePlan);
    return "Home";
}

std::span<const std::reference_wrapper<IHdRow>> HdHomePlan::rows() const {
    TASTY_SEAT_BODY(HdHomePlan);
    return refs_;
}

std::uint8_t HdHomePlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdHomePlan);
    return 1;
}

bool HdHomePlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(HdHomePlan);
    return false;
}

std::uint32_t HdHomePlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdHomePlan);
    return refusals_ + (cores_ != nullptr ? cores_->refusals() : 0u) + launch_.refusals();
}

bool HdHomePlan::BrowseRow::shown() const {
    TASTY_SEAT_BODY(BrowseRow);
    return plan_.cores_ != nullptr;
}

app::PageDescription::Row HdHomePlan::BrowseRow::describe() const {
    TASTY_SEAT_BODY(BrowseRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign("Browse cores");
    return r;
}

Transition HdHomePlan::BrowseRow::select() {
    TASTY_SEAT_BODY(BrowseRow);
    if (plan_.cores_ == nullptr)
        return Transition{};
    return plan_.cores_->open_root_child(plan_.ctx_);
}

}
