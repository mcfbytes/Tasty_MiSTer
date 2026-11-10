// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_options_plan.h"

#include "ui/option_host.h"
#include "ui/options_model.h"
#include "ui/option_item.h"
#include "ui/options_surface.h"
#include "ui/placement.h"

namespace mister::ui {
namespace {

class SelectProbe final : public IOptionHost {
public:
    void refuse() override { refused = true; }

    proto::FieldValue field(proto::BitField) const override { return {}; }

    void write_status(proto::BitField, proto::FieldValue) override {}

    void toggle(proto::BitField, bool) override {}

    Transition open_browser(const OptionItem&) override { return Transition{}; }

    PageId subpage_page() const override { return PageId::None; }

    bool refused = false;
};

}

HdOptionsPlan::HdOptionsPlan(IOptionsSurface& surface, const HdOptionsView::Spec& spec)
    : surface_(surface),
      placer_(surface.model(), spec.slots, spec.verbs, spec.facts),
      save_(surface, spec.baseline),
      about_(HdPushRow::Spec{.label = "About this core", .page = PageId::HdCoreAbout}) {
    TASTY_SEAT_BODY(HdOptionsPlan);
    const OptionsModel& walk = surface_.model();
    bool gap = false;
    bool about = false;
    for (const std::unique_ptr<OptionItem>& owned : walk.items()) {
        const OptionItem& it = *owned;
        const Placement p = placer_.of(it);
        about = about || host_of(p) == PageId::HdCoreAbout;
        if (!it.screen_row || host_of(p) != PageId::HdCoreSettings)
            continue;

        if (!it.selectable_index) {
            gap = !refs_.empty();
            continue;
        }
        if (!acts_(it))
            continue;
        if (gap)
            refs_.emplace_back(separator_());
        gap = false;
        rows_.push_back(std::make_unique<HdOptionRow>(it, surface_));
        refs_.emplace_back(*rows_.back());
    }
    if (walk.page() != proto::kRootSubmenu)
        return;
    if (!refs_.empty())
        refs_.emplace_back(separator_());
    refs_.emplace_back(save_);
    pinned_ = 1;
    if (about) {
        refs_.emplace_back(about_);
        ++pinned_;
    }
}

bool HdOptionsPlan::acts_(const OptionItem& it) const {
    if (it.disabled)
        return false;
    SelectProbe probe;
    (void)it.select(probe);
    return !probe.refused;
}

IHdRow& HdOptionsPlan::separator_() {
    separators_.push_back(std::make_unique<HdSeparatorRow>());
    return *separators_.back();
}

PageId HdOptionsPlan::id() const {
    TASTY_SEAT_BODY(HdOptionsPlan);
    return PageId::HdCoreSettings;
}

std::string_view HdOptionsPlan::title() const {
    TASTY_SEAT_BODY(HdOptionsPlan);
    return surface_.heading();
}

std::span<const std::reference_wrapper<IHdRow>> HdOptionsPlan::rows() const {
    TASTY_SEAT_BODY(HdOptionsPlan);
    return refs_;
}

RowCount HdOptionsPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdOptionsPlan);
    return RowCount{pinned_};
}

}
