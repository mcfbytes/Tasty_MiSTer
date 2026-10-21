// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_settings_plan.h"

#include <array>

#include "app/ui_request.h"
#include "ui/hd_plan_inputs.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/system_page.h"

namespace mister::ui {
namespace {

constexpr std::array<std::string_view, 1> kRebootBody{"Reboot MiSTer?"};

}

HdSettingsPlan::HdSettingsPlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      sys_(make_system_page(in.ctx.sys)),
      controllers_(HdPushRow::Spec{.label = "Controllers", .page = PageId::HdControllers}),
      display_(HdPushRow::Spec{.label = "Display", .page = PageId::HdDisplay}),
      about_(HdPushRow::Spec{.label = "About", .page = PageId::HdAbout}),
      reboot_(
          HdAskRow::Spec{
              .label = "Reboot",
              .body = kRebootBody,
              .action = reboot_action_,
              .confirm = in.ctx.launches.confirm
          }
      ),
      advanced_(HdPushRow::Spec{.label = "Advanced (classic menu)", .page = in.classic}) {
    TASTY_SEAT_BODY(HdSettingsPlan);

    if (sys_ != nullptr)
        sys_->on_enter(ctx_);
    refs_ = {controllers_, display_, scripts_, about_, reboot_, separator_, advanced_};
    if (sys_ == nullptr)
        ++refusals_;
    if (!in.ctx.launches.confirm)
        ++refusals_;
}

PageId HdSettingsPlan::id() const {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return PageId::HdSettings;
}

std::string_view HdSettingsPlan::title() const {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return "Settings";
}

std::span<const std::reference_wrapper<IHdRow>> HdSettingsPlan::rows() const {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return refs_;
}

void HdSettingsPlan::refresh(const SystemFacts& facts) {
    TASTY_SEAT_BODY(HdSettingsPlan);
    if (sys_ != nullptr)
        sys_->on_facts(facts);
}

std::uint8_t HdSettingsPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return 1;
}

std::uint32_t HdSettingsPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return refusals_ + (sys_ != nullptr ? sys_->refusals() : 0u);
}

std::uint32_t HdSettingsPlan::commands_published() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return published_;
}

bool HdSettingsPlan::ScriptsRow::shown() const {
    TASTY_SEAT_BODY(ScriptsRow);
    return plan_.sys_ != nullptr;
}

app::PageDescription::Row HdSettingsPlan::ScriptsRow::describe() const {
    TASTY_SEAT_BODY(ScriptsRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign("Scripts");
    return r;
}

Transition HdSettingsPlan::ScriptsRow::select() {
    TASTY_SEAT_BODY(ScriptsRow);
    if (plan_.sys_ == nullptr)
        return Transition{};
    return plan_.sys_->open_scripts(plan_.ctx_);
}

Transition HdSettingsPlan::RebootAction::on_yes() {
    TASTY_SEAT_BODY(RebootAction);
    if (!push_ask(plan_.ctx_.ui_requests, app::UiRequest::Reboot{})) {
        ++plan_.refusals_;
    } else {
        ++plan_.published_;
    }
    return Transition{Transition::Kind::CloseAll};
}

}
