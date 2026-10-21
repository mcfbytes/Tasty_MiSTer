// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_slot_sheet_plan.h"

#include "app/session_identity.h"
#include "ui/hd_label.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/options_page.h"
#include "ui/page_context.h"
#include "ui/place_facts.h"

namespace mister::ui {
namespace {

app::PageDescription::Row row_of(std::string_view label, app::PageDescription::RowKind kind) {
    app::PageDescription::Row r{};
    r.kind = kind;
    (void)r.label.assign(label);
    return r;
}

}

HdSlotSheetPlan::HdSlotSheetPlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      now_playing_(in.now_playing),
      verbs_(in.verbs),
      front_end_(in.front_end),
      focus_(slot_focus_of(in.focus)),
      env_(in.env) {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    walk_(in.items, in.env);

    if (slot_) {
        if (!ctx_.launches.browser)
            ++refusals_;
        if (!ctx_.recents)
            ++refusals_;
        if (slot_->mounts_image() && !ctx_.ui_requests)
            ++refusals_;
    }
}

void HdSlotSheetPlan::walk_(const proto::ItemTable& table, const BuildEnv& env) {
    BuildEnv page0 = env;
    page0.page = 0;
    page0.flat = false;
    env_ = SlotPickHost::Env{page0};
    slot_ = {};
    placer_.reset();
    if (focus_ && !front_end_ && walk_model_.build(table, page0).has_value()) {
        placer_.emplace(walk_model_, env_.slots, verbs_, PlaceFacts{});
        for (const OptionItem* it : placer_->slots()) {
            if (focus_of_(*it) == *focus_) {
                slot_ = infra::OptRef<const OptionItem>{*it};
                break;
            }
        }
    }
    title_.clear();
    has_recent_ = false;
    if (!slot_)
        return;
    title_ = hd_label(slot_->label);
    if (!picks_) {
        picks_.emplace(
            env_,
            SlotPickHost::Borrows{
                .asks = ctx_.ui_requests,
                .browser = ctx_.launches.browser,
                .recents = ctx_.recents,
                .names = ctx_.names,
                .info = ctx_.info,
                .files = ctx_.files
            }
        );
    }
    has_recent_ = picks_->has_recents(*slot_);
}

SlotFocus HdSlotSheetPlan::focus_of_(const OptionItem& it) const {
    return SlotFocus{
        it.mounts_image() ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
        proto::IoIndex{it.ioctl_index}
    };
}

bool HdSlotSheetPlan::ejectable_() const {
    if (!slot_ || !slot_->mounts_image())
        return false;
    return !now_playing_.mounted_image(proto::IoIndex{slot_->ioctl_index}).empty();
}

PageId HdSlotSheetPlan::id() const {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return PageId::HdSlotSheet;
}

std::string_view HdSlotSheetPlan::title() const {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return title_;
}

std::span<const std::reference_wrapper<IHdRow>> HdSlotSheetPlan::rows() const {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return refs_;
}

infra::OptRef<IHdRewalk> HdSlotSheetPlan::rewalker() {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return infra::OptRef<IHdRewalk>{static_cast<IHdRewalk&>(*this)};
}

void HdSlotSheetPlan::rewalk(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    walk_(table, env);
}

std::uint32_t HdSlotSheetPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return refusals_ + (picks_ ? picks_->refusals() : 0u);
}

std::uint32_t HdSlotSheetPlan::commands_published() const noexcept {
    TASTY_SEAT_BODY(HdSlotSheetPlan);
    return picks_ ? picks_->commands_published() : 0u;
}

bool HdSlotSheetPlan::ChooseRow::shown() const {
    TASTY_SEAT_BODY(ChooseRow);
    return plan_.has_slot_() && plan_.ctx_.launches.browser.has_value();
}

app::PageDescription::Row HdSlotSheetPlan::ChooseRow::describe() const {
    TASTY_SEAT_BODY(ChooseRow);
    return row_of("Choose file", app::PageDescription::RowKind::Submenu);
}

Transition HdSlotSheetPlan::ChooseRow::select() {
    TASTY_SEAT_BODY(ChooseRow);
    if (!shown())
        return Transition{};
    return plan_.picks_->open(*plan_.slot_, true);
}

bool HdSlotSheetPlan::RecentRow::shown() const {
    TASTY_SEAT_BODY(RecentRow);
    return plan_.has_slot_() && plan_.ctx_.recents.has_value() && plan_.has_recent_;
}

app::PageDescription::Row HdSlotSheetPlan::RecentRow::describe() const {
    TASTY_SEAT_BODY(RecentRow);
    return row_of("Recent files", app::PageDescription::RowKind::Submenu);
}

Transition HdSlotSheetPlan::RecentRow::select() {
    TASTY_SEAT_BODY(RecentRow);
    if (!shown())
        return Transition{};
    return plan_.picks_->open_recents(*plan_.slot_);
}

bool HdSlotSheetPlan::EjectRow::shown() const {
    TASTY_SEAT_BODY(EjectRow);
    return plan_.has_slot_() && plan_.ctx_.ui_requests.has_value() && plan_.ejectable_();
}

app::PageDescription::Row HdSlotSheetPlan::EjectRow::describe() const {
    TASTY_SEAT_BODY(EjectRow);
    return row_of("Eject", app::PageDescription::RowKind::Action);
}

Transition HdSlotSheetPlan::EjectRow::select() {
    TASTY_SEAT_BODY(EjectRow);
    if (!shown())
        return Transition{};
    return plan_.picks_->eject(*plan_.slot_);
}

}
