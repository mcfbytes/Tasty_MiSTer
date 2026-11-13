// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_options_view.h"

#include <memory>

#include "ui/hd_focus_memory.h"
#include "ui/hd_list_page.h"
#include "ui/hd_options_plan.h"
#include "ui/options_model.h"
#include "ui/options_surface.h"

namespace mister::ui {

HdOptionsView::HdOptionsView(IOptionsSurface& surface, const Spec& spec) noexcept
    : surface_(surface),
      spec_(spec),
      list_(make_list_()) {
    TASTY_SEAT_BODY(HdOptionsView);
}

HdOptionsView::~HdOptionsView() = default;

std::unique_ptr<HdListPage> HdOptionsView::make_list_() const {
    const PageArg key{surface_.model().page()};
    return std::make_unique<HdListPage>(
        std::make_unique<HdOptionsPlan>(surface_, spec_),
        key,
        spec_.memory
    );
}

PageId HdOptionsView::id() const {
    TASTY_SEAT_BODY(HdOptionsView);
    return PageId::HdCoreSettings;
}

BuildEnv HdOptionsView::walk_env(const BuildEnv& env) const {
    TASTY_SEAT_BODY(HdOptionsView);
    BuildEnv out = env;
    out.flat = false;
    return out;
}

void HdOptionsView::on_model_rebuilt() {
    TASTY_SEAT_BODY(HdOptionsView);
    retired_refusals_.add(list_->refusals());
    list_ = make_list_();
}

bool HdOptionsView::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(HdOptionsView);
    return list_->walk(out);
}

Transition HdOptionsView::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(HdOptionsView);
    return list_->handle(ev);
}

MenuMask HdOptionsView::mask() const {
    TASTY_SEAT_BODY(HdOptionsView);
    return list_->mask();
}

void HdOptionsView::set_selection(RowIndex i) {
    TASTY_SEAT_BODY(HdOptionsView);
    list_->set_selection(i);
}

RowIndex HdOptionsView::selection() const {
    TASTY_SEAT_BODY(HdOptionsView);
    return list_->selection();
}

bool HdOptionsView::empty() const {
    TASTY_SEAT_BODY(HdOptionsView);
    return list_->empty();
}

svc::Tally HdOptionsView::refusals() const noexcept {
    TASTY_SEAT_BODY(HdOptionsView);
    return retired_refusals_.sum(list_->refusals());
}

}
