// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu_model.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "app/event.h"
#include "app/hd_crumb.h"
#include "app/identity_latch.h"
#include "app/page_description.h"
#include "app/pause_badge.h"
#include "app/ui_request_ring.h"
#include "infra/ascii.h"
#include "proto/standard_item.h"
#include "proto/status_word.h"
#include "ui/page_ask.h"
#include "ui/browser_launch.h"
#include "ui/browser_page.h"
#include "ui/bt_ops.h"
#include "ui/build_env.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/core_options_page.h"
#include "ui/cheats_page.h"
#include "ui/config_slots_page.h"
#include "ui/core_settings_page.h"
#include "ui/dips_page.h"
#include "ui/hd_about_plan.h"
#include "ui/hd_controllers_plan.h"
#include "ui/hd_core_about_plan.h"
#include "ui/hd_display_plan.h"
#include "ui/hd_home_plan.h"
#include "ui/hd_label.h"
#include "ui/hd_list_page.h"
#include "ui/hd_media_plan.h"
#include "ui/hd_options_view.h"
#include "ui/hd_plan_inputs.h"
#include "ui/hd_recent_games_plan.h"
#include "ui/hd_root_plan.h"
#include "ui/hd_settings_plan.h"
#include "ui/hd_slot_sheet_plan.h"
#include "ui/joymap_page.h"
#include "ui/options_page.h"
#include "ui/osd_renderer.h"
#include "ui/script_launch.h"
#include "ui/script_policy.h"
#include "ui/script_run_page.h"
#include "ui/slot_pick_host.h"
#include "ui/progress_page.h"
#include "ui/info_page.h"
#include "ui/system_page.h"
#include "ui/key_decoder.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

void PageStack::push(std::unique_ptr<Page> p) {
    if (p == nullptr)
        return;
    pages_.push_back(static_cast<std::unique_ptr<Page>&&>(p));
}

ItemId PageStack::pop() {
    if (pages_.empty())
        return kNoItem;
    retired_refusals_ += pages_.back()->refusals();
    pages_.pop_back();
    const ItemId focus = pending_focus_;
    pending_focus_ = kNoItem;
    return focus;
}

void PageStack::replace(std::unique_ptr<Page> p) {
    if (!pages_.empty()) {
        retired_refusals_ += pages_.back()->refusals();
        pages_.pop_back();
    }
    push(static_cast<std::unique_ptr<Page>&&>(p));
}

void PageStack::clear() noexcept {
    for (const std::unique_ptr<Page>& p : pages_)
        retired_refusals_ += p->refusals();
    pages_.clear();
    pending_focus_ = kNoItem;
}

std::uint32_t PageStack::page_refusals() const noexcept {
    std::uint32_t n = retired_refusals_;
    for (const std::unique_ptr<Page>& p : pages_)
        n += p->refusals();
    return n;
}

bool PageStack::any_bt_pairing() const noexcept {
    for (const std::unique_ptr<Page>& p : pages_) {
        if (p->bt_pairing_active())
            return true;
    }
    return false;
}

bool PageStack::any_runs_a_process() const noexcept {
    for (const std::unique_ptr<Page>& p : pages_) {
        if (p->runs_a_process())
            return true;
    }
    return false;
}

bool PageStack::any_takes_focus() const noexcept {
    return focus_top() != nullptr;
}

const Page* PageStack::focus_top() const noexcept {
    for (auto it = pages_.rbegin(); it != pages_.rend(); ++it) {
        if ((*it)->takes_input_focus())
            return it->get();
    }
    return nullptr;
}

bool MenuModel::global_hotkeys_allowed() const noexcept {
    const Page* p = stack_.focus_top();
    return p == nullptr || p->allows_global_hotkeys();
}

bool MenuModel::popups_visible() const noexcept {
    const Page* top = stack_.top();
    if (top == nullptr)
        return true;
    return top->allows_popups();
}

bool MenuModel::menu_present() const noexcept {
    const Page* top = stack_.top();
    return top != nullptr && top->counts_as_menu_present();
}

bool MenuModel::allow_cfg_switch() const {
    const Page* top = stack_.top();
    return top != nullptr && top->allows_cfg_switch();
}

namespace {

constexpr std::int64_t kStatusPeriodNs = 1'000'000'000;

std::optional<std::string_view> declared_title(const proto::ItemTable& items, std::uint8_t page) {
    for (const proto::Item& it : items.items()) {
        if (it.declares_page && it.declared_page == page)
            return items.subfield(it, 1);
    }
    return std::nullopt;
}

}

void MenuModel::seed_navigator() noexcept {
    const Page* top = stack_.top();
    if (top == nullptr) {
        nav_.set_mask(MenuMask{});
        return;
    }
    nav_.set_mask(top->mask());
    nav_.set_index(top->selection());
}

void MenuModel::arm_auto_pop(const PageContext& ctx) noexcept {
    auto_pop_at_ns_ = 0;
    const Page* top = stack_.top();
    if (top == nullptr || !ctx.clock)
        return;
    const std::uint32_t ms = top->auto_pop_ms();
    if (ms == 0)
        return;
    auto_pop_at_ns_ = ctx.clock->now().count() + static_cast<std::int64_t>(ms) * 1000000;
}

std::unique_ptr<Page> MenuModel::make_page(PageId id, ItemId focus, PageContext& ctx) {
    switch (id) {
        case PageId::System:
            return make_system_page(page_facts_(ctx.sys));
        case PageId::CoreOptions: {
            const auto page = static_cast<std::uint8_t>(focus.v & 0xFFu);

            std::string_view title = items_.core_name();
            if (page != 0)
                title = declared_title(items_, page).value_or(title);
            auto p = std::make_unique<CoreOptionsPage>(
                items_,
                build_env(page),
                title,
                infra::OptRef<const StatusView>{mirror_.status_words()}
            );

            if (p->model().empty_page()) {
                ++root_refusals_;
                return nullptr;
            }
            return p;
        }
        case PageId::Browser: {

            if (!ctx.launches.browser
                || !ctx.storage
                || !ctx.launches.browser->pending.has_value()) {
                ++root_refusals_;
                return nullptr;
            }
            const Page* parent = stack_.top();
            const RowCount h = parent != nullptr ? parent->osd_height() : RowCount{16};
            auto req = static_cast<BrowserSession::Request&&>(*ctx.launches.browser->pending);
            ctx.launches.browser->pending.reset();
            return std::make_unique<BrowserPage>(
                *ctx.storage,
                static_cast<BrowserSession::Request&&>(req),
                h,
                ctx.clock
            );
        }
        case PageId::ScriptRun: {
            if (!ctx.launches.script || !ctx.launches.script->pending.has_value()) {
                ++root_refusals_;
                return nullptr;
            }
            ScriptLaunch::Request req = static_cast<ScriptLaunch::Request&&>(
                *ctx.launches.script->pending
            );
            ctx.launches.script->pending.reset();

            switch (script_policy_for(req.kind).gate_open(ctx.bt)) {
                case IScriptPolicy::OpenGate::Refuse:
                    ++bt_refusals_;
                    return nullptr;
                case IScriptPolicy::OpenGate::NoAdapter:
                    return make_info_page(app::InfoId::BluetoothUnavailable, 0);
                case IScriptPolicy::OpenGate::Proceed:
                    break;
            }
            return std::make_unique<ScriptRunPage>(
                req.kind,
                static_cast<std::string&&>(req.argv0),
                req.bt_reset
            );
        }
        case PageId::Cheats: {

            if (cheats_.count() == 0) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<CheatsPage>(cheats_);
        }
        case PageId::CoreSettings: {

            const auto page = static_cast<std::uint8_t>(focus.v & 0xFFu);
            if (mirror_.option_table().count == 0 || page >= cores::kMaxOptionPages) {
                ++root_refusals_;
                return nullptr;
            }
            CoreSettingsPage::Spec spec{};
            spec.table = &mirror_.option_table();
            spec.page = page;

            const std::string_view declared = mirror_.option_table().page_title[page].view();
            spec.title = !declared.empty()
                ? std::string(declared)
                : (page == 0 ? std::string(items_.core_name()) : std::string{});
            spec.mt32_available = (hd_mask_.v & 1u) != 0;

            const BuildEnv env = build_env(page);
            spec.custom_aspect_ratio[0] = env.custom_aspect_ratio[0];
            spec.custom_aspect_ratio[1] = env.custom_aspect_ratio[1];
            spec.conf_gen = env.conf_gen;
            auto p = std::make_unique<CoreSettingsPage>(
                static_cast<CoreSettingsPage::Spec&&>(spec)
            );
            if (p->empty_page()) {
                ++root_refusals_;
                return nullptr;
            }
            return p;
        }
        case PageId::ConfigSlots: {
            if (mirror_.config_slot_table().count == 0) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<ConfigSlotsPage>(
                mirror_.config_slot_table(),
                focus.v != 0 ? ConfigSlotsPage::Mode::Save : ConfigSlotsPage::Mode::Load,
                mirror_.conf_gen()
            );
        }
        case PageId::ArcadeDip: {

            if (mirror_.dip_table().count == 0) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<DipsPage>(mirror_.dip_table());
        }
        case PageId::JoyMap: {

            if (!ctx.input || !ctx.files) {
                ++root_refusals_;
                return nullptr;
            }
            JoyMapPage::Spec spec{};
            spec.wire = ctx.input;
            spec.files = ctx.files;
            spec.front_end = binding_.front_end();
            if (ctx.identity) {
                app::SessionIdentity cid{};
                if (ctx.identity->copy(cid)) {
                    spec.front_end = cid.front_end;
                    spec.core = cid.core;
                    spec.j_names = std::string(cid.j_names.view());
                }
            }
            return std::make_unique<JoyMapPage>(static_cast<JoyMapPage::Spec&&>(spec));
        }
        case PageId::Confirm: {
            if (!ctx.launches.confirm || !ctx.launches.confirm->pending.has_value()) {
                ++root_refusals_;
                return nullptr;
            }
            ConfirmPage::Spec spec = *ctx.launches.confirm->pending;
            ctx.launches.confirm->pending.reset();
            return std::make_unique<ConfirmPage>(spec);
        }

        case PageId::HdMenu:
            refresh_recent_(ctx);
            if (binding_.front_end()) {
                return make_hd_list_(std::make_unique<HdHomePlan>(plan_inputs_(ctx, focus)), focus);
            }
            return make_hd_list_(std::make_unique<HdRootPlan>(plan_inputs_(ctx, focus)), focus);

        case PageId::HdCoreSettings: {
            const auto page = static_cast<std::uint8_t>(focus.v & 0xFFu);
            std::string title{"Core settings"};
            if (page != 0) {
                const std::optional<std::string_view> declared = declared_title(items_, page);
                title = declared ? hd_label(*declared) : std::string{};
            }
            const BuildEnv env = build_env(page);

            baseline_.take(items_, mirror_.status_words());
            const HdOptionsView::Spec hd{
                .slots = env.slots,
                .verbs = verbs_,
                .facts = PlaceFacts{},
                .memory = hd_focus_,
                .baseline = baseline_,
            };
            auto p = std::make_unique<CoreOptionsPage>(
                items_,
                env,
                title,
                infra::OptRef<const StatusView>{mirror_.status_words()},
                hd
            );
            if (p->model().empty_page() || p->empty()) {
                ++root_refusals_;
                return nullptr;
            }
            return p;
        }

        case PageId::HdCoreAbout:
            return make_hd_list_(
                std::make_unique<HdCoreAboutPlan>(plan_inputs_(ctx, focus)),
                focus
            );
        case PageId::HdSettings:
            return make_hd_list_(std::make_unique<HdSettingsPlan>(plan_inputs_(ctx, focus)), focus);
        case PageId::HdControllers:
            return make_hd_list_(
                std::make_unique<HdControllersPlan>(plan_inputs_(ctx, focus)),
                focus
            );
        case PageId::HdDisplay:
            return make_hd_list_(std::make_unique<HdDisplayPlan>(plan_inputs_(ctx, focus)), focus);
        case PageId::HdAbout:
            return make_hd_list_(std::make_unique<HdAboutPlan>(plan_inputs_(ctx, focus)), focus);
        case PageId::HdMedia:
            return make_hd_list_(std::make_unique<HdMediaPlan>(plan_inputs_(ctx, focus)), focus);
        case PageId::HdSlotSheet:
            return make_hd_list_(
                std::make_unique<HdSlotSheetPlan>(plan_inputs_(ctx, focus)),
                focus
            );
        case PageId::HdRecentGames:
            refresh_recent_(ctx);
            return make_hd_list_(
                std::make_unique<HdRecentGamesPlan>(plan_inputs_(ctx, focus)),
                focus
            );
        case PageId::None:
        case PageId::Info:
        case PageId::Progress:
        case PageId::Unlock:
        case PageId::SelectIni:
        case PageId::Recents:
        case PageId::About:
        case PageId::Common:
        case PageId::Misc:
        case PageId::VideoProc:
        case PageId::Uart:
        case PageId::UartMode:
        case PageId::UartBaud:
        case PageId::FbTerminal:
        case PageId::AtariCart:
        case PageId::KbdMap:
        case PageId::LightGunCal:
        case PageId::AdvancedMapList:
        case PageId::AdvancedMapEdit:
        case PageId::AdvancedMapButtons:
        case PageId::AdvancedMapCapture:
        case PageId::MinimigMain:
        case PageId::MinimigDisk:
        case PageId::MinimigChipset:
        case PageId::MinimigVideo:
        case PageId::MinimigCd32Cdtv:
        case PageId::MinimigLoadConfig:
        case PageId::MinimigSaveConfig:
        case PageId::ArchieMain:
        case PageId::Mt32Pi:
        case PageId::kCount:

            ++root_refusals_;
            return nullptr;
    }

    ++root_refusals_;
    return nullptr;
}

HdPlanInputs MenuModel::plan_inputs_(PageContext& ctx, ItemId focus) {
    return HdPlanInputs{
        .ctx = ctx,
        .style = style_,
        .items = items_,
        .status = mirror_.status_words(),
        .verbs = verbs_,
        .now_playing = now_playing_,
        .recent_games = recent_games_,
        .env = build_env(0),
        .focus = focus,
        .classic = hd_classic_,
        .front_end = binding_.front_end(),
    };
}

app::RecentGames::Played MenuModel::played_record_() const noexcept {
    const NowPlaying::Played& p = now_playing_.last();

    return app::RecentGames::Played{
        .core_name = binding_.front_end() ? std::string_view{} : browse_home(),
        .core_image = p.core_image.view(),
        .path = p.path.view(),
        .title = p.title.view(),
        .ioctl_index = p.ioctl_index,
        .slot = p.slot_letter
    };
}

void MenuModel::refresh_recent_(PageContext& ctx) {
    if (!ctx.recents || !ctx.files)
        return;
    recent_games_
        .refresh(*ctx.files, *ctx.recents, played_record_(), ctx.clock, played_records_(ctx));
}

std::span<const app::PlayedHistory::Record> MenuModel::played_records_(PageContext& ctx) {
    if (!ctx.played)
        return {};
    (void)ctx.played->load();
    return ctx.played->records();
}

std::unique_ptr<Page> MenuModel::make_hd_list_(std::unique_ptr<IHdPlan> plan, ItemId focus) {
    auto p = std::make_unique<HdListPage>(std::move(plan), focus, hd_focus_);

    if (p->empty()) {
        ++root_refusals_;
        return nullptr;
    }
    return p;
}

void MenuModel::apply(const Transition& t, PageContext& ctx) {
    switch (t.kind) {
        case Transition::Kind::Stay:
            return;
        case Transition::Kind::Push: {
            const bool from_hd = stack_.top_id() == PageId::HdSettings && t.page == PageId::System;
            auto p = make_page(t.page, t.focus, ctx);
            if (p == nullptr)
                return;
            if (from_hd)
                p->set_selection(RowIndex{static_cast<std::uint32_t>(SystemItem::Exit)});
            stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
            forget_hd_window_(stack_.depth() - 1);
            if (Page* top = stack_.top())
                top->on_enter(ctx);

            if (from_hd && binding_.front_end()) {
                if (Page* sys = stack_.top())
                    apply(sys->open_root_child(ctx), ctx);
            }
            break;
        }
        case Transition::Kind::Pop:

            stack_.set_pending_focus(t.focus);
            (void)stack_.pop();
            forget_hd_window_(stack_.depth());
            break;
        case Transition::Kind::Replace: {

            if (t.page == PageId::HdMenu) {
                style_.choose_hd();
                stack_.clear();
                break;
            }
            auto p = make_page(t.page, t.focus, ctx);
            if (p == nullptr)
                return;
            stack_.replace(static_cast<std::unique_ptr<Page>&&>(p));
            forget_hd_window_(stack_.depth() - 1);
            if (Page* top = stack_.top())
                top->on_enter(ctx);
            break;
        }
        case Transition::Kind::CloseAll:

            stack_.clear();
            break;
        case Transition::Kind::Leave:

            if (stack_.bottom_id() == PageId::HdMenu && stack_.depth() > 1) {
                stack_.set_pending_focus(t.focus);
                (void)stack_.pop();
            } else {
                stack_.clear();
            }
            break;
    }

    if (stack_.empty() && style_.take_reopen())
        open_root_(ctx, true);
    seed_navigator();
    arm_auto_pop(ctx);
    repaint_ = true;
}

SystemFacts MenuModel::page_facts_(const SystemFacts& f) const noexcept {
    SystemFacts out = f;
    if (hd_tree()) {
        out.hd_switchable = false;
        out.in_hd_tree = true;
    }
    return out;
}

bool MenuModel::hd_tree() const noexcept {
    if (stack_.bottom_id() == PageId::HdMenu)
        return true;
    return hd_root_ && style_.hd() && stack_.depth() == 1 && stack_.top_id() == PageId::Progress;
}

bool MenuModel::describe_top(app::PageDescription& out) {
    const Page* top = stack_.top();
    if (top == nullptr)
        return false;
    const std::size_t depth = stack_.depth();
    const bool remember = depth >= 1 && depth <= kHdWindowDepths;
    out.first = remember ? hd_window_first_[depth - 1] : 0;
    if (!top->describe(out))
        return false;
    if (remember)
        hd_window_first_[depth - 1] = out.first;

    if (depth > 1) {
        out.hub = {};
        const Page* hub = stack_.bottom();
        if (hub != nullptr && hub->describe_hub(out))
            out.hub.depth = static_cast<std::uint8_t>(std::min<std::size_t>(depth - 1, 0xFF));
    }

    if (stack_.top_id() == PageId::CoreOptions
        && !items_.core_name().empty()
        && infra::ieq(out.title.view(), items_.core_name()))
        (void)out.title.assign("Advanced");
    out.crumb = app::hd_crumb(items_.core_name(), out.title.view());
    if (now_playing_.game_loaded()) {
        (void)out.subtitle.assign(now_playing_.title());
    } else if (!binding_.front_end()) {
        (void)out.subtitle.assign(now_playing_.idle_line(items_, build_env(0)));
    }
    return true;
}

void MenuModel::on_request_done(
    proto::CorrelationTag tag,
    std::int64_t unix_time,
    PageContext& ctx
) {
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    app::SessionIdentity cid{};
    const std::span<const cores::FileSlot> slots = identity_.copy(cid)
        ? cid.slots
        : std::span<const cores::FileSlot>{};
    const bool recorded = now_playing_
                              .on_done(asks ? asks->pending(tag) : std::nullopt, slots, unix_time);
    if (asks)
        asks->forget(tag);

    if (recorded && ctx.played) {
        const app::RecentGames::Played p = played_record_();
        std::string image{p.core_image};

        if (image.empty() && ctx.files.has_value())
            image = app::RecentGames::core_image_of(*ctx.files, p.core_name);
        (void)ctx.played->record(
            app::PlayedHistory::Record{
                .core_image = std::move(image),
                .core_name = std::string{p.core_name},
                .path = std::string{p.path},
                .title = std::string{p.title},
                .ioctl_index = p.ioctl_index,
                .slot = p.slot,
                .unix_time = unix_time,
            }
        );
    }

    if (recent_games_.built() && ctx.recents.has_value() && ctx.files.has_value()) {
        recent_games_
            .update(*ctx.files, *ctx.recents, played_record_(), ctx.clock, played_records_(ctx));
    }
}

void MenuModel::on_request_refused(proto::CorrelationTag tag, PageContext& ctx) {
    if (ctx.ui_requests)
        ctx.ui_requests->forget(tag);
}

void MenuModel::on_core_load_done(proto::CorrelationTag tag, PageContext& ctx) {
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    now_playing_.on_core_loaded(asks ? asks->pending(tag) : std::nullopt);
    if (asks)
        asks->forget(tag);
}

void MenuModel::forget_hd_window_(std::size_t depth0) noexcept {
    if (depth0 < kHdWindowDepths)
        hd_window_first_[depth0] = 0;
}

bool MenuModel::show_progress(PageContext& ctx, std::uint16_t cur, std::uint16_t max) {
    Page* top = stack_.top();
    IProgressView* view = top != nullptr ? top->progress_view() : nullptr;
    if (max == 0) {
        in_transfer_ = false;
        if (view == nullptr)
            return false;
        stack_.pop();
        seed_navigator();

        arm_auto_pop(ctx);
        invalidate_surface();
        repaint_ = true;
        return true;
    }
    if (!in_transfer_) {
        in_transfer_ = true;
        take_transfer_name_(ctx);
    }
    if (view != nullptr) {
        view->set_progress(cur, max);

        arm_auto_pop(ctx);
        repaint_ = true;
        return true;
    }

    if (menu_has_focus()) {
        ++progress_masked_;
        return false;
    }
    auto p = make_progress_page(std::string_view(transfer_name_, transfer_name_len_));
    if (p == nullptr)
        return false;
    if (IProgressView* v = p->progress_view(); v != nullptr)
        v->set_progress(cur, max);
    place_overlay(static_cast<std::unique_ptr<Page>&&>(p));
    if (Page* t = stack_.top())
        t->on_enter(ctx);
    seed_navigator();
    arm_auto_pop(ctx);
    repaint_ = true;
    return true;
}

void MenuModel::take_transfer_name_(const PageContext& ctx) noexcept {
    transfer_name_len_ = 0;
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    if (!asks || asks->load_asks() == load_asks_seen_)
        return;
    load_asks_seen_ = asks->load_asks();
    const std::string_view leaf = progress_leaf(asks->last_load_path());
    std::copy(leaf.begin(), leaf.end(), transfer_name_);
    transfer_name_len_ = static_cast<std::uint8_t>(leaf.size());
}

void MenuModel::place_overlay(std::unique_ptr<Page> p) {
    const Page* top = stack_.top();
    if (top != nullptr && !top->takes_input_focus()) {
        stack_.replace(static_cast<std::unique_ptr<Page>&&>(p));
    } else {
        stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
    }
    forget_hd_window_(stack_.depth() - 1);
}

bool MenuModel::push_info(PageContext& ctx, app::InfoId id) {
    auto p = make_info_page(id, 0);
    if (p == nullptr)
        return false;
    place_overlay(static_cast<std::unique_ptr<Page>&&>(p));
    if (Page* top = stack_.top())
        top->on_enter(ctx);
    seed_navigator();

    arm_auto_pop(ctx);
    repaint_ = true;
    return true;
}

bool MenuModel::pump(PageContext& ctx, OsdRenderer& out) {
    ++pumps_;

    sync_cells();
    stamp_facts(ctx);

    bool cleared_this_round = false;
    if (invalidate_) {
        invalidate_ = false;
        ++invalidations_;
        out.clear();
        repaint_ = true;
        cleared_this_round = true;
    }

    if (!binding_.ui_capable())
        return false;

    if (Page* top = stack_.top()) {
        const MenuMask before = top->mask();
        top->on_facts(page_facts_(ctx.sys));

        if (top->mask() != before) {
            nav_.set_mask(top->mask());
            nav_.set_index(top->selection());
        }

        out.set_visible_rows(
            binding_.front_end() ? proto::OsdSurface::kMaxRows : top->osd_height().v
        );
    }

    bool did = false;

    if (Page* top = stack_.top()) {
        const Transition t = top->on_tick(ctx);
        if (t.kind != Transition::Kind::Stay) {
            apply(t, ctx);
            did = true;
        } else if (top->wake_every_round()) {

            repaint_ = true;
        }
    }

    if (auto_pop_at_ns_ != 0 && ctx.clock && ctx.clock->now().count() >= auto_pop_at_ns_) {
        auto_pop_at_ns_ = 0;
        ++auto_pops_;
        if (!stack_.empty()) {
            stack_.set_pending_focus(kNoItem);
            (void)stack_.pop();
            seed_navigator();
            arm_auto_pop(ctx);
            repaint_ = true;
            did = true;
        }
    }

    std::optional<UiEvent> ev;
    if (ctx.keys) {
        const Page* rt = stack_.top();
        const bool reboot_row = rt != nullptr
            && rt->id() == PageId::System
            && rt->selection().v == static_cast<std::uint32_t>(SystemItem::Reboot);
        const bool browser = rt != nullptr && rt->id() == PageId::Browser;
        ctx.keys->set_repeat_all(reboot_row || browser);
        ev = ctx.keys->pop();
    }

    if (ev.has_value()) {

        if (bootcore_armed_) {
            bootcore_armed_ = false;
            ++bootcore_cancels_;
        }

        const std::uint16_t buttons = ev->buttons;

        constexpr std::uint16_t kRearm = UiEvent::Menu
            | UiEvent::Select
            | UiEvent::Up
            | UiEvent::Down
            | UiEvent::Left
            | UiEvent::Right
            | UiEvent::Plus
            | UiEvent::Minus;
        if ((buttons & kRearm) != 0) {
            ++help_rearms_;
            if (ctx.clock) {
                help_rearm_at_ns_ = ctx.clock->now().count();
            }
        }

        if (!menu_has_focus()) {
            if ((buttons & UiEvent::Menu) != 0) {
                if (osd_open()) {

                    stack_.clear();
                    seed_navigator();
                    arm_auto_pop(ctx);
                    osd_was_open_ = false;
                    ++overlay_trades_;
                }
                open_root(ctx);
                did = true;
            }
            return did;
        }

        bool routed = false;
        if ((ev->raw.v & KeyDecoder::kUpstroke) == 0 && (ev->raw.v & 0xFFFFu) == KEY_F11) {
            routed = true;
            const Page* run = stack_.top();
            const bool mid_script = run != nullptr && run->holds_pairing_hotkey();
            if (mid_script) {
                ++bt_refusals_;
            } else {
                open_bt_pairing(ctx);
                did = true;
            }
        }

        Page* top = stack_.top();
        if (routed)
            top = nullptr;

        const HdKeyRemap::Axis axis = top != nullptr ? remap_.axis(*this, buttons)
                                                     : HdKeyRemap::Axis{};
        const bool across = axis.move != HdKeyRemap::Axis::Move::None;
        if (top != nullptr) {
            bool moved = false;
            if (across) {
                moved = remap_.apply(axis, nav_);
            } else if ((buttons & UiEvent::Up) != 0) {
                moved = nav_.move(Navigator::Direction::Up);
            } else if ((buttons & UiEvent::Down) != 0) {
                moved = nav_.move(Navigator::Direction::Down);
            }
            if (moved) {
                top->set_selection(nav_.index());
                repaint_ = true;
            }
        }

        if (top != nullptr) {
            UiEvent forwarded = *ev;
            constexpr std::uint16_t kSides = UiEvent::Left | UiEvent::Right;
            forwarded.buttons = across ? static_cast<std::uint16_t>(buttons & ~kSides) : buttons;

            const HdKeyRemap::Refresh rw = remap_.refresh(*this, forwarded);
            Transition t = top->handle(rw.ev);

            if (top->selection() != nav_.index())
                nav_.set_index(top->selection());
            if (rw.one_page) {
                const HdKeyRemap::Settled s = HdKeyRemap::settle(t, stack_.depth());
                if (s.dropped)
                    disarm_launches_(ctx);
                t = s.t;
            }
            apply(t, ctx);

            repaint_ = true;
            did = true;
        }
    }

    const bool open_now = osd_open();
    if (open_now != osd_was_open_) {
        osd_was_open_ = open_now;
        if (open_now) {
            ++show_edges_;

            if (!cleared_this_round) {
                ++invalidations_;
                out.clear();
                repaint_ = true;
            }
        }
    }

    if (repaint_) {
        repaint_ = false;
        if (Page* top = stack_.top()) {
            out.set_height(top->osd_height());

            const bool strip = top->has_title_strip() || menu_has_focus();
            out.set_stripe(strip ? OsdRenderer::Stripe::Title : OsdRenderer::Stripe::Blank);
            static_cast<const Page*>(top)->render(out);
            did = true;
        }
    }

    if (binding_.front_end() && ctx.clock) {
        const std::int64_t now = ctx.clock->now().count();
        if (now >= status_due_ns_) {
            status_due_ns_ = now + kStatusPeriodNs;

            ctx.status.sdram_cfg = sdram_cfg_;
            out.composite_status_bar(ctx.status);
            ++status_paints_;
            did = true;
        }
    }
    return did;
}

void MenuModel::on_core_loaded(proto::CoreType type, bool front_end, PageContext& ctx) {

    sync_cells();
    set_core_type(type);

    set_front_end(front_end);
    latch_browse_home();
    stamp_facts(ctx);

    stack_.clear();
    hd_focus_.clear();
    baseline_.clear();
    seed_navigator();
    arm_auto_pop(ctx);
    invalidate_surface();
    const bool hosted = launcher_cell_.sample().value.hosting;
    if (binding_.ui_capable() && binding_.front_end() && !hosted)
        open_root(ctx);
}

void MenuModel::on_hd_mask(HdMask m, bool front_end, infra::OptRef<app::UiRequestRing> asks) {

    sync_cells();

    if (front_end && sdram_cfg_ == 0 && (m.v & 0x8000u) != 0) {
        sdram_cfg_ = static_cast<std::uint16_t>(m.v);
    }

    if (front_end && sdram_cfg_ != 0 && !sdram_reported_) {
        if (push_ask(asks, app::UiRequest::SdramDetected{.osd_mask = sdram_cfg_})) {
            sdram_reported_ = true;
        } else {
            ++sdram_ask_refusals_;
        }
    }

    if (!(m == hd_mask_)) {
        hd_mask_ = m;

        Page* top = stack_.top();
        if (auto co = top != nullptr ? top->mask_rebuild() : infra::OptRef<IMaskRebuild>{}) {
            co->rebuild(items_, build_env(co->page()));
            seed_navigator();
        }
        repaint_ = true;
    }
}

void MenuModel::on_info(app::InfoId id, PageContext& ctx) {

    sync_cells();
    if (id == app::InfoId::SaveWritten)
        baseline_.rebase(items_, mirror_.status_words());

    if (!binding_.ui_capable()) {
        ++info_refusals_;
        return;
    }
    (void)push_info(ctx, id);
}

void MenuModel::on_progress(std::uint16_t cur, std::uint16_t max, PageContext& ctx) {

    sync_cells();

    if (!binding_.ui_capable()) {
        ++progress_refusals_;
        return;
    }
    (void)show_progress(ctx, cur, max);
}

namespace {

enum class RootArm : std::uint8_t {
    HdMenu,
    CoreSettings,
    CoreOptions,
    System,
    kCount,
};

struct RootRow {
    RootArm arm;
    PageId page;
    std::string_view evidence;
};

inline constexpr std::array<RootRow, static_cast<std::size_t>(RootArm::kCount)> kRootArms{{
    {RootArm::HdMenu,
     PageId::HdMenu,
     "the HD root, ahead of every classic arm; it holds only while hd_root_ is set"},
    {RootArm::CoreSettings,
     PageId::CoreSettings,
     "menu.cpp:1621 — MENU_NONE2 routes a core with its own hardcoded tree "
     "BEFORE the generic arm; the predicate is `the core declared option "
     "rows`, never a core name (mandate 3)"},
    {RootArm::CoreOptions,
     PageId::CoreOptions,
     "req / menu.cpp:1644-1648 — MENU_NONE2's UNCONDITIONAL else arm "
     "reaches MENU_GENERIC_MAIN1; has_menu() (menu.cpp:1640) guards only "
     "the Win+Menu chord that goes to MENU_COMMON1 instead"},
    {RootArm::System,
     PageId::System,
     "menu.cpp:2230-2236 — stock falls to MENU_COMMON1; PageId::Common "
     "has no factory in this tree, so System is the fallback (item)"},
}};

consteval bool root_arms_are_indexed_by_arm() {
    for (std::size_t i = 0; i < kRootArms.size(); ++i) {
        if (static_cast<std::size_t>(kRootArms[i].arm) != i)
            return false;
    }
    return true;
}

static_assert(
    root_arms_are_indexed_by_arm(),
    "the array IS the priority order; an index that does not match "
    "its arm means the order was silently reshuffled"
);

}

void MenuModel::latch_browse_home() noexcept {
    binding_.latch_home(identity_);
}

app::HdOsdStatus::Badge MenuModel::pause_badge() const noexcept {
    proto::StatusWord word{};
    for (std::size_t i = 0; i < std::size(word.words); ++i)
        word.words[i] = mirror_.status_words().w[i];
    return app::pause_badge(
        pause_probe_,
        binding_.pause_rule(),
        word,
        osd_show() == proto::LinkOp::OsdShow::Menu
    );
}

std::string_view MenuModel::browse_home() const noexcept {
    return binding_.browse_home(items_.core_name());
}

app::RememberedStem MenuModel::file_stem() const noexcept {
    return binding_.file_stem(items_.core_name());
}

void MenuModel::stamp_facts(PageContext& ctx) const noexcept {
    ctx.sys.core_loaded = binding_.ui_capable() && !binding_.front_end();
    ctx.sys.front_end = binding_.front_end();

    const std::string_view name = binding_.front_end() ? std::string_view{} : browse_home();
    std::size_t n = name.size();
    if (n > sizeof(ctx.sys.core_dir) - 1)
        n = sizeof(ctx.sys.core_dir) - 1;
    for (std::size_t i = 0; i < n; ++i)
        ctx.sys.core_dir[i] = name[i];
    ctx.sys.core_dir[n] = '\0';
}

bool MenuModel::cells_pending() const noexcept {
    return mirror_.pending();
}

void MenuModel::sync_cells() {
    CoreCellMirror::Changed c = mirror_.sync();
    if (c.table)
        set_item_table(static_cast<proto::ItemTable&&>(*c.table));
    if (c.repaint)
        repaint_ = true;
}

void MenuModel::drop_pages_() noexcept {
    stack_.clear();
    seed_navigator();
    auto_pop_at_ns_ = 0;
    repaint_ = true;
}

void MenuModel::disarm_launches_(PageContext& ctx) noexcept {
    if (ctx.launches.browser)
        ctx.launches.browser->pending.reset();
    if (ctx.launches.script)
        ctx.launches.script->pending.reset();
    if (ctx.launches.confirm)
        ctx.launches.confirm->pending.reset();
}

void MenuModel::close_osd() noexcept {
    TASTY_SEAT_BODY(MenuModel);
    if (stack_.any_runs_a_process()) {
        ++close_refusals_;
        return;
    }
    drop_pages_();
}

void MenuModel::set_item_table(proto::ItemTable t) {

    if (!(t == items_))
        baseline_.clear();
    items_ = std::move(t);
    pause_probe_ = proto::find_standard_item(items_, proto::StandardItem::PauseOnOsd);
    drop_pages_();
    invalidate_surface();
}

void MenuModel::set_status_words(StatusView s) noexcept {
    if (mirror_.set_status_words(s))
        repaint_ = true;
}

BuildEnv MenuModel::build_env(std::uint8_t page) const noexcept {
    BuildEnv env;

    env.hdmask = proto::OsdMask{static_cast<std::uint16_t>(hd_mask_.v & 0xFFFFu)};
    env.page = page;
    env.flat = flat_;

    env.core_dir = binding_.front_end() ? std::string_view{} : browse_home();
    env.file_stem = binding_.front_end() ? app::RememberedStem{} : file_stem();

    {
        app::SessionIdentity cid{};
        if (identity_.copy(cid)) {
            if (!cid.cue_dir.empty())
                env.cue_dir = cid.cue_dir;
            env.slots = cid.slots.first(cid.slots.size() > 255 ? 255 : cid.slots.size());
            env.image_no_zip = cid.image_no_zip;
        }
    }

    env.cheats_available = cheats_.count() != 0;
    env.manual_available = false;
    env.dip_count = mirror_.dip_table().count;
    env.conf_gen = mirror_.conf_gen();
    return env;
}

bool MenuModel::core_options_available() const {
    OptionsModel probe;
    (void)probe.build(items_, build_env(0));
    return probe.selectable_count().v != 0 && !probe.empty_page();
}

void MenuModel::open_root(PageContext& ctx) {
    open_root_(ctx, true);
}

void MenuModel::open_root_(PageContext& ctx, bool hd_allowed) {
    if (!stack_.empty())
        return;
    if (!binding_.ui_capable())
        return;

    const bool front_end = binding_.front_end();
    const bool has_option_table = mirror_.option_table().count != 0;
    const auto holds = [this, hd_allowed, front_end, has_option_table](RootArm arm) noexcept {
        switch (arm) {
            case RootArm::HdMenu:
                return hd_allowed && hd_root_ && style_.hd();
            case RootArm::CoreSettings:
                return !front_end && has_option_table;
            case RootArm::CoreOptions:

                return !front_end && core_options_available();
            case RootArm::System:
                return true;
            case RootArm::kCount:
                return false;
        }
        return false;
    };

    hd_classic_ = PageId::System;
    for (const RootRow& row : kRootArms) {
        if (row.arm == RootArm::HdMenu)
            continue;
        if (holds(row.arm)) {
            hd_classic_ = row.page;
            break;
        }
    }

    for (const RootRow& row : kRootArms) {
        if (!holds(row.arm)) {
            if (row.arm != RootArm::HdMenu)
                ++root_refusals_;
            continue;
        }

        auto p = make_page(row.page, kNoItem, ctx);
        if (p == nullptr)
            continue;
        if (row.arm == RootArm::System) {

            p->set_selection(RowIndex{static_cast<std::uint32_t>(SystemItem::Exit)});
        }
        stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
        forget_hd_window_(stack_.depth() - 1);
        if (Page* top = stack_.top())
            top->on_enter(ctx);

        if (binding_.front_end() && row.page == PageId::System) {
            if (Page* sys = stack_.top())
                apply(sys->open_root_child(ctx), ctx);
        }
        seed_navigator();
        arm_auto_pop(ctx);
        repaint_ = true;
        return;
    }
}

bool MenuModel::open_classic_root(PageContext& ctx) {
    if (!stack_.empty() || !binding_.ui_capable())
        return false;
    open_root_(ctx, false);
    return !stack_.empty();
}

void MenuModel::open_bt_pairing(PageContext& ctx) {
    if (!ctx.launches.script) {
        ++bt_refusals_;
        return;
    }
    ScriptLaunch::Request req;
    req.kind = ScriptKind::BtPair;
    req.bt_reset = ctx.bt_reset_before_pair;
    ctx.launches.script->pending = static_cast<ScriptLaunch::Request&&>(req);
    const auto kind = osd_open() ? Transition::Kind::Replace : Transition::Kind::Push;
    apply(Transition{kind, PageId::ScriptRun}, ctx);
}

}
