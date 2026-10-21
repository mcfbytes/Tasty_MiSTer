// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_media_plan.h"

#include <memory>

#include "app/session_identity.h"
#include "ui/hd_label.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/options_page.h"
#include "ui/place_facts.h"

namespace mister::ui {
namespace {

std::string_view file_name_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}

HdMediaPlan::HdMediaPlan(const HdPlanInputs& in)
    : now_playing_(in.now_playing),
      verbs_(in.verbs),
      front_end_(in.front_end) {
    TASTY_SEAT_BODY(HdMediaPlan);
    walk_(in.items, in.env);
}

void HdMediaPlan::walk_(const proto::ItemTable& table, const BuildEnv& env) {
    BuildEnv page0 = env;
    page0.page = 0;
    page0.flat = false;
    refs_.clear();
    rows_.clear();
    placer_.reset();
    if (!front_end_ && walk_model_.build(table, page0).has_value()) {
        placer_.emplace(walk_model_, page0.slots, verbs_, PlaceFacts{});
        rows_.reserve(placer_->slots().size());
        for (std::size_t i = 0; i < placer_->slots().size(); ++i)
            rows_.emplace_back(*this, i);
    }
    refs_.reserve(rows_.size());
    for (SlotRow& r : rows_)
        refs_.emplace_back(r);
}

SlotFocus HdMediaPlan::focus_at_(std::size_t at) const {
    const OptionItem& it = slot_(at);
    return SlotFocus{
        it.mounts_image() ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
        proto::IoIndex{it.ioctl_index}
    };
}

infra::OptRef<IHdRewalk> HdMediaPlan::rewalker() {
    TASTY_SEAT_BODY(HdMediaPlan);
    return infra::OptRef<IHdRewalk>{static_cast<IHdRewalk&>(*this)};
}

void HdMediaPlan::rewalk(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(HdMediaPlan);
    walk_(table, env);
}

PageId HdMediaPlan::id() const {
    TASTY_SEAT_BODY(HdMediaPlan);
    return PageId::HdMedia;
}

std::string_view HdMediaPlan::title() const {
    TASTY_SEAT_BODY(HdMediaPlan);
    return "Media";
}

std::span<const std::reference_wrapper<IHdRow>> HdMediaPlan::rows() const {
    TASTY_SEAT_BODY(HdMediaPlan);
    return refs_;
}

std::optional<RowKey> HdMediaPlan::SlotRow::key() const {
    TASTY_SEAT_BODY(SlotRow);
    return RowKey{0x3'0000u | to_item(plan_.focus_at_(at_)).v};
}

app::PageDescription::Row HdMediaPlan::SlotRow::describe() const {
    TASTY_SEAT_BODY(SlotRow);
    const OptionItem& it = plan_.slot_(at_);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign(hd_label(it.label));

    const proto::IoIndex index{it.ioctl_index};
    const std::string_view path = it.mounts_image() ? plan_.now_playing_.mounted_image(index)
                                                    : plan_.now_playing_.loaded_file(index);
    (void)r.value.assign(path.empty() ? std::string_view{"(none)"} : file_name_of(path));
    return r;
}

Transition HdMediaPlan::SlotRow::select() {
    TASTY_SEAT_BODY(SlotRow);
    return Transition{Transition::Kind::Push, PageId::HdSlotSheet, to_item(plan_.focus_at_(at_))};
}

}
