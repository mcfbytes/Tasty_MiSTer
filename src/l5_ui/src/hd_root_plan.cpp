// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_root_plan.h"

#include <array>
#include <memory>

#include "app/cmd_verb_sink.h"
#include "app/pending_load.h"
#include "app/recent_games.h"
#include "app/session_identity.h"
#include "app/ui_request.h"
#include "ui/core_verbs.h"
#include "ui/hd_label.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/options_page.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/place_facts.h"

namespace mister::ui {
namespace {

constexpr std::array<std::string_view, 1> kResetBody{"Reset the game?"};
constexpr std::array<std::string_view, 2> kMainMenuBody{
    "Close the game and return",
    "to the main menu?",
};

bool offered(const OptionItem& it) noexcept {
    return it.screen_row.has_value() && it.selectable_index.has_value() && !it.disabled;
}

}

HdRootPlan::HdRootPlan(const HdPlanInputs& in)
    : ctx_(in.ctx),
      now_playing_(in.now_playing),
      recent_games_(in.recent_games),
      verbs_(in.verbs),
      front_end_(in.front_end),
      table_settings_(in.classic == PageId::CoreSettings),
      env_(in.env),
      resume_row_(HdCloseRow::Spec{.label = "Resume"}),
      media_row_(HdPushRow::Spec{.label = "Media", .page = PageId::HdMedia}),
      recent_row_(HdPushRow::Spec{.label = "Recent games", .page = PageId::HdRecentGames}),
      cheats_row_(HdPushRow::Spec{.label = "Cheats", .page = PageId::Cheats}),
      reset_row_(
          HdAskRow::Spec{
              .label = "Reset game",
              .body = kResetBody,
              .action = reset_action_,
              .confirm = in.ctx.launches.confirm
          }
      ),
      core_settings_row_(
          HdPushRow::Spec{
              .label = "Core settings",
              .page = table_settings_ ? PageId::CoreSettings : PageId::HdCoreSettings
          }
      ),
      settings_row_(HdPushRow::Spec{.label = "Settings", .page = PageId::HdSettings}),
      main_menu_row_(
          HdAskRow::Spec{
              .label = "Return to main menu",
              .body = kMainMenuBody,
              .action = main_menu_action_,
              .confirm = in.ctx.launches.confirm
          }
      ) {
    TASTY_SEAT_BODY(HdRootPlan);
    walk_(in.items, in.env);
    refs_ = {
        resume_gate_,
        load_row_,
        media_gate_,
        recent_gate_,
        cheats_gate_,
        reset_gate_,
        separator_,
        core_settings_gate_,
        settings_row_,
        main_menu_gate_
    };

    if (has_reset_ && (!ctx_.ui_requests || !ctx_.launches.confirm))
        ++refusals_;
    if (!front_end_ && (!ctx_.route || !ctx_.launches.confirm))
        ++refusals_;
}

void HdRootPlan::walk_(const proto::ItemTable& table, const BuildEnv& env) {
    BuildEnv page0 = env;
    page0.page = 0;
    page0.flat = false;
    env_ = SlotPickHost::Env{page0};
    primary_ = std::nullopt;
    has_reset_ = has_cheats_ = has_settings_ = has_media_ = false;
    placer_.reset();
    const bool walked = !front_end_ && walk_model_.build(table, page0).has_value();
    if (!front_end_ && !walked)
        ++refusals_;
    if (walked) {
        placer_.emplace(walk_model_, env_.slots, verbs_, PlaceFacts{});

        const GameSlot& slot = placer_->game();
        const OptionItem* const pick = slot.disc_row() != nullptr ? slot.disc_row() : slot.row();
        if (pick != nullptr)
            primary_ = infra::OptRef<const OptionItem>{*pick};
        for (const std::unique_ptr<OptionItem>& owned : walk_model_.items()) {
            const OptionItem& it = *owned;
            const Placement p = placer_->of(it);
            has_reset_ = has_reset_ || p == Placement::ResetGame;
            has_cheats_ = has_cheats_
                || (p == Placement::Cheats
                    && it.page == 0
                    && offered(it)
                    && page0.cheats_available);
            has_settings_ = has_settings_ || (p == Placement::CoreSettings && offered(it));
        }

        has_media_ = placer_->slots().size() > 1;
    }
    if (primary_) {
        load_label_ = hd_label(primary_->label);
        if (load_label_.empty())
            load_label_ = primary_->mounts_image() ? "Load disc" : "Load game";
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
    }
    gate_();
}

void HdRootPlan::gate_() noexcept {
    resume_ = now_playing_.game_loaded() || !primary_;
    has_recent_ = !recent_games_.empty();
    cheats_ = resume_ && has_cheats_;
    reset_ = resume_ && has_reset_ && ctx_.ui_requests.has_value();
    core_settings_ = !front_end_ && (table_settings_ || has_settings_);
    main_menu_ = !front_end_ && ctx_.route.has_value();
}

PageId HdRootPlan::id() const {
    TASTY_SEAT_BODY(HdRootPlan);
    return PageId::HdMenu;
}

std::string_view HdRootPlan::title() const {
    TASTY_SEAT_BODY(HdRootPlan);
    return "Menu";
}

std::span<const std::reference_wrapper<IHdRow>> HdRootPlan::rows() const {
    TASTY_SEAT_BODY(HdRootPlan);
    return refs_;
}

std::uint8_t HdRootPlan::pinned() const noexcept {
    TASTY_SEAT_BODY(HdRootPlan);
    return kPinned;
}

bool HdRootPlan::remembers_focus() const noexcept {
    TASTY_SEAT_BODY(HdRootPlan);
    return false;
}

void HdRootPlan::refresh(const SystemFacts& facts) {
    TASTY_SEAT_BODY(HdRootPlan);
    (void)facts;
    gate_();
}

infra::OptRef<IHdRewalk> HdRootPlan::rewalker() {
    TASTY_SEAT_BODY(HdRootPlan);
    return infra::OptRef<IHdRewalk>{static_cast<IHdRewalk&>(*this)};
}

void HdRootPlan::rewalk(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(HdRootPlan);
    walk_(table, env);
}

std::uint32_t HdRootPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdRootPlan);
    return refusals_ + (picks_ ? picks_->refusals() : 0u);
}

std::uint32_t HdRootPlan::commands_published() const noexcept {
    TASTY_SEAT_BODY(HdRootPlan);
    return published_ + (picks_ ? picks_->commands_published() : 0u);
}

bool HdRootPlan::LoadRow::shown() const {
    TASTY_SEAT_BODY(LoadRow);
    return plan_.primary_.has_value() && plan_.picks_.has_value();
}

app::PageDescription::Row HdRootPlan::LoadRow::describe() const {
    TASTY_SEAT_BODY(LoadRow);
    app::PageDescription::Row r{};
    r.kind = app::PageDescription::RowKind::Submenu;
    (void)r.label.assign(plan_.load_label_);
    return r;
}

Transition HdRootPlan::LoadRow::select() {
    TASTY_SEAT_BODY(LoadRow);
    if (!shown())
        return Transition{};
    return plan_.picks_->open(*plan_.primary_, true);
}

Transition HdRootPlan::ResetAction::on_yes() {
    TASTY_SEAT_BODY(ResetAction);
    return plan_.reset_yes_();
}

Transition HdRootPlan::MainMenuAction::on_yes() {
    TASTY_SEAT_BODY(MainMenuAction);
    return plan_.main_menu_yes_();
}

Transition HdRootPlan::reset_yes_() {
    const app::UiRequest::ResetCore ask{
        .edge = proto::ResetEdge::osd_toggle(proto::StatusBit{0}, false),
        .scope = app::CoreScope{env_.conf_gen}
    };
    if (!has_reset_ || !push_ask(ctx_.ui_requests, ask)) {
        ++refusals_;
    } else {
        ++published_;
    }
    return Transition{Transition::Kind::CloseAll};
}

Transition HdRootPlan::main_menu_yes_() {
    const app::CmdVerb::LoadCore menu{app::kFrontEndImage, app::CmdVerb::RbfOrMra::Rbf};
    if (!ctx_.route || !ctx_.route->on(menu)) {
        ++refusals_;
    } else {
        ++published_;
    }
    return Transition{Transition::Kind::CloseAll};
}

}
