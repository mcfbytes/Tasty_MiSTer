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
    refs_ = {style_row_, theme_row_};
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

app::PageDescription::Row HdDisplayPlan::ThemeRow::describe() const {
    TASTY_SEAT_BODY(ThemeRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Value;
    r.steps = true;
    (void)r.label.assign("Theme");
    (void)r.value.assign(app::hd_theme(plan_.style_.theme()).name);
    return r;
}

Transition HdDisplayPlan::ThemeRow::select() {
    TASTY_SEAT_BODY(ThemeRow);
    return step(1);
}

Transition HdDisplayPlan::ThemeRow::step(int delta) {
    TASTY_SEAT_BODY(ThemeRow);
    constexpr auto kCount = static_cast<unsigned>(app::kHdThemes.size());
    const unsigned at = app::hd_theme_index(plan_.style_.theme());
    const auto next = static_cast<std::uint8_t>((at + (delta < 0 ? kCount - 1u : 1u)) % kCount);
    plan_.style_.choose_theme(next);
    if (!push_ask(plan_.ctx_.ui_requests, app::UiRequest::SetHdTheme{.theme = next})) {
        ++plan_.refusals_;
    } else {
        ++plan_.published_;
    }
    return Transition{};
}

}
