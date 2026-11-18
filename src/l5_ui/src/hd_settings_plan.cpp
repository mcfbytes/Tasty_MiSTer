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

app::PageDescription::Row HdSettingsPlan::HdEnterClassicRow::describe() const {
    TASTY_SEAT_BODY(HdEnterClassicRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign("Advanced (classic menu)");
    return r;
}

std::optional<RowKey> HdSettingsPlan::HdEnterClassicRow::key() const {
    TASTY_SEAT_BODY(HdEnterClassicRow);
    return push_row_key(classic_);
}

Transition HdSettingsPlan::HdEnterClassicRow::select() {
    TASTY_SEAT_BODY(HdEnterClassicRow);
    return Transition::enter_classic();
}

HdSettingsPlan::HdSettingsPlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      scripts_entry_(in.ctx.session),
      controllers_(HdPushRow::Spec{.label = "Controllers", .page = PageId::HdControllers}),
      display_(HdPushRow::Spec{.label = "Display", .page = PageId::HdDisplay}),
      network_(HdPushRow::Spec{.label = "Network", .page = PageId::Network}),
      about_(HdPushRow::Spec{.label = "About", .page = PageId::HdAbout}),
      reboot_(
          HdAskRow::Spec{
              .label = "Reboot",
              .body = kRebootBody,
              .action = reboot_action_,
              .confirm = in.ctx.launches.confirm
          }
      ),
      advanced_(in.classic) {
    TASTY_SEAT_BODY(HdSettingsPlan);
    scripts_entry_.bind(ctx_);
    autosave_on_ = in.ctx.session.autosave;
    refs_ = {
        controllers_,
        display_,
        network_,
        autosave_,
        scripts_,
        about_,
        reboot_,
        separator_,
        advanced_
    };
    if (!in.ctx.launches.confirm)
        refusals_.add();
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

void HdSettingsPlan::refresh(const SystemFacts&, const SessionFacts& session) {
    TASTY_SEAT_BODY(HdSettingsPlan);
    autosave_on_ = session.autosave;
    scripts_entry_.on_facts(session);
}

RowCount HdSettingsPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return RowCount{1};
}

svc::Tally HdSettingsPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return refusals_.sum(scripts_entry_.refusals());
}

std::uint32_t HdSettingsPlan::commands_published() const noexcept {
    TASTY_SEAT_BODY(HdSettingsPlan);
    return published_;
}

app::PageDescription::Row HdSettingsPlan::AutosaveRow::describe() const {
    TASTY_SEAT_BODY(AutosaveRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Value;
    (void)r.label.assign(kAutosaveLabel);
    (void)r.value.assign(autosave_word(plan_.autosave_on_));
    return r;
}

Transition HdSettingsPlan::AutosaveRow::select() {
    TASTY_SEAT_BODY(AutosaveRow);
    if (!push_ask(plan_.ctx_.ui_requests, app::UiRequest::SetAutosave{.on = !plan_.autosave_on_})) {
        plan_.refusals_.add();
    } else {
        ++plan_.published_;
    }
    return Transition{};
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
    return plan_.scripts_entry_.open(plan_.ctx_);
}

Transition HdSettingsPlan::RebootAction::on_yes() {
    TASTY_SEAT_BODY(RebootAction);
    if (!push_ask(plan_.ctx_.ui_requests, app::UiRequest::Reboot{})) {
        plan_.refusals_.add();
    } else {
        ++plan_.published_;
    }
    return Transition::close_all();
}

}
