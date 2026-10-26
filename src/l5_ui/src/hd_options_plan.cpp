// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_options_plan.h"

#include <optional>
#include <utility>

#include "app/page_description.h"
#include "app/session_identity.h"
#include "ui/hd_label.h"
#include "ui/option_host.h"
#include "ui/options_model.h"
#include "ui/options_surface.h"
#include "ui/placement.h"
#include "ui/saved_baseline.h"

namespace mister::ui {
namespace {

using Row = app::PageDescription::Row;
using RowKind = app::PageDescription::RowKind;

class SelectProbe final : public IOptionHost {
public:
    void refuse() override { refused = true; }

    void write_status(proto::BitField, std::uint32_t) override {}

    void toggle(proto::BitField, bool) override {}

    Transition open_browser(const OptionItem&) override { return Transition{}; }

    bool refused = false;
};

Transition own(Transition t) noexcept {
    if (t.kind == Transition::Kind::Push && t.page == PageId::CoreOptions) {
        t.page = PageId::HdCoreSettings;
    }
    return t;
}

}

std::optional<RowKey> HdOptionRow::key() const {
    TASTY_SEAT_BODY(HdOptionRow);
    return RowKey{item_.item.v};
}

OptionItem::Look HdOptionRow::look_() const {
    return OptionItem::Look{.flat = false, .value = host_.field(item_.bits)};
}

OptionItem::Press HdOptionRow::press_(bool select, bool minus) const {
    return OptionItem::Press{
        .select = select,
        .minus = minus,
        .at_root = host_.walk().page() == 0,
        .value = host_.field(item_.bits)
    };
}

Row HdOptionRow::describe() const {
    TASTY_SEAT_BODY(HdOptionRow);
    Row r = item_.describe(look_());
    (void)r.label.assign(hd_label(r.label.view()));
    return r;
}

Transition HdOptionRow::select() {
    TASTY_SEAT_BODY(HdOptionRow);
    return own(item_.activate(press_(true, false), host_));
}

Transition HdOptionRow::step(int delta) {
    TASTY_SEAT_BODY(HdOptionRow);
    return own(item_.activate(press_(false, delta < 0), host_));
}

std::optional<RowKey> HdSaveRow::key() const {
    TASTY_SEAT_BODY(HdSaveRow);
    return RowKey{0x1'0000u};
}

Row HdSaveRow::describe() const {
    TASTY_SEAT_BODY(HdSaveRow);
    Row r{};
    r.kind = RowKind::Action;
    (void)r.label.assign("Save settings");
    if (baseline_.unsaved(host_.live_words()))
        (void)r.value.assign("Unsaved");
    return r;
}

Transition HdSaveRow::select() {
    TASTY_SEAT_BODY(HdSaveRow);
    host_.save_config();
    return Transition{};
}

HdOptionsPlan::HdOptionsPlan(IOptionsSurface& surface, const HdOptionsView::Spec& spec)
    : surface_(surface),
      placer_(surface.walk(), spec.slots, spec.verbs, spec.facts),
      save_(surface, spec.baseline),
      about_(HdPushRow::Spec{.label = "About this core", .page = PageId::HdCoreAbout}) {
    TASTY_SEAT_BODY(HdOptionsPlan);
    const OptionsModel& walk = surface_.walk();
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
    if (walk.page() != 0)
        return;
    if (!refs_.empty())
        refs_.emplace_back(separator_());
    refs_.emplace_back(save_);
    pinned_ = 1;
    if (about) {
        refs_.emplace_back(about_);
        pinned_ = 2;
    }
}

bool HdOptionsPlan::acts_(const OptionItem& it) const {
    if (it.disabled)
        return false;
    SelectProbe probe;
    (void)it.activate(OptionItem::Press{.select = true, .at_root = true}, probe);
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

std::uint8_t HdOptionsPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdOptionsPlan);
    return pinned_;
}

}
