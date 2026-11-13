// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_media_plan.h"

#include <memory>

#include "app/session_identity.h"
#include "infra/path_parts.h"
#include "ui/hd_label.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/option_item.h"
#include "ui/place_facts.h"

namespace mister::ui {

HdMediaPlan::HdMediaPlan(const HdPlanInputs& in)
    : now_playing_(in.now_playing),
      verbs_(in.verbs),
      front_end_(in.front_end) {
    TASTY_SEAT_BODY(HdMediaPlan);
    walk_(in.items, in.env);
}

void HdMediaPlan::walk_(const proto::ItemTable& table, const BuildEnv& env) {
    refs_.clear();
    rows_.clear();
    if (page0_.run(table, env, !front_end_, verbs_)) {
        rows_.reserve(page0_.placer().slots().size());
        for (std::size_t i = 0; i < page0_.placer().slots().size(); ++i) {
            const std::optional<proto::FileRowId> row = page0_.placer().slots()[i]->file_row();
            if (!row)
                continue;
            rows_.emplace_back(*this, i, *row);
        }
    }
    refs_.reserve(rows_.size());
    for (SlotRow& r : rows_)
        refs_.emplace_back(r);
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

    return slot_row_key(row_);
}

app::PageDescription::Row HdMediaPlan::SlotRow::describe() const {
    TASTY_SEAT_BODY(SlotRow);
    const OptionItem& it = plan_.slot_(at_);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign(hd_label(it.label));

    const std::string_view path = plan_.now_playing_.path_of(row_);

    (void)r.value.assign(path.empty() ? std::string_view{"(none)"} : infra::basename(path));
    return r;
}

Transition HdMediaPlan::SlotRow::select() {
    TASTY_SEAT_BODY(SlotRow);
    return Transition::push(PageId::HdSlotSheet, PageArg{row_});
}

}
