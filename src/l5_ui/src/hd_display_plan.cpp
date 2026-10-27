// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_display_plan.h"

#include <array>

#include "app/hd_themes.h"
#include "app/ui_request.h"
#include "ui/hd_plan_inputs.h"
#include "ui/osd_style_preference.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"

namespace mister::ui {
namespace {

constexpr std::array<std::string_view, 3> kClassicBody{
    "Switch to the classic menu?",
    "Switch back from",
    "System > Use HD OSD.",
};

HdAskRow::Spec style_spec(IHdAction& action, infra::OptRef<ConfirmLaunch> confirm) {
    HdAskRow::Spec
        s{.label = "Menu style", .body = kClassicBody, .action = action, .confirm = confirm};
    (void)s.value.assign("HD");
    return s;
}

}

HdDisplayPlan::HdDisplayPlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      style_(in.style),
      style_row_(style_spec(classic_, in.ctx.launches.confirm)) {
    TASTY_SEAT_BODY(HdDisplayPlan);
    refs_ = {style_row_, layout_row_, colors_row_, display_row_, opacity_row_};
    if (!in.ctx.launches.confirm)
        ++refusals_;
}

PageId HdDisplayPlan::id() const {
    TASTY_SEAT_BODY(HdDisplayPlan);
    return PageId::HdDisplay;
}

std::string_view HdDisplayPlan::title() const {
    TASTY_SEAT_BODY(HdDisplayPlan);
    return "Display";
}

std::span<const std::reference_wrapper<IHdRow>> HdDisplayPlan::rows() const {
    TASTY_SEAT_BODY(HdDisplayPlan);
    return refs_;
}

std::uint32_t HdDisplayPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdDisplayPlan);
    return refusals_;
}

std::uint32_t HdDisplayPlan::commands_published() const noexcept {
    TASTY_SEAT_BODY(HdDisplayPlan);
    return published_;
}

Transition HdDisplayPlan::ClassicAction::on_yes() {
    TASTY_SEAT_BODY(ClassicAction);
    plan_.style_.choose_classic();
    if (!push_ask(plan_.ctx_.ui_requests, app::UiRequest::SetOsdStyle{.style = 0})) {
        ++plan_.refusals_;
    } else {
        ++plan_.published_;
    }
    return Transition{Transition::Kind::CloseAll};
}

app::PageDescription::Row HdDisplayPlan::LookRow::describe() const {
    TASTY_SEAT_BODY(LookRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Value;
    r.steps = true;
    (void)r.label.assign(label_);
    (void)r.value.assign(app::hd_look_choice_name(field_, plan_.style_.look()));
    return r;
}

Transition HdDisplayPlan::LookRow::select() {
    TASTY_SEAT_BODY(LookRow);
    return step(1);
}

Transition HdDisplayPlan::LookRow::step(int delta) {
    TASTY_SEAT_BODY(LookRow);
    const app::HdLook look = plan_.style_.look();
    const std::uint8_t next = app::hd_look_step(field_, look, delta);
    if (next == app::hd_look_get(look, field_))
        return Transition{};
    plan_.style_.choose_look(field_, next);
    if (!push_ask(
            plan_.ctx_.ui_requests,
            app::UiRequest::SetHdLook{.field = field_, .value = next}
        )) {
        ++plan_.refusals_;
    } else {
        ++plan_.published_;
    }
    return Transition{};
}

}
