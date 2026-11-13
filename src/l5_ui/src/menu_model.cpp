// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/menu_model.h"

#include "ui/connect_port.h"
#include "ui/misc_page.h"
#include "ui/select_ini_page.h"
#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
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
#include "ui/browser_launch.h"
#include "ui/browser_page.h"
#include "ui/build_env.h"
#include "ui/confirm_launch.h"
#include "ui/bluetooth_plan.h"
#include "ui/bt_devices_plan.h"
#include "ui/bt_pair_plan.h"
#include "ui/country_pick_plan.h"
#include "ui/network_plan.h"
#include "ui/text_entry_launch.h"
#include "ui/wifi_advanced_plan.h"
#include "ui/wifi_networks_plan.h"
#include "ui/wifi_saved_plan.h"
#include "ui/text_entry_page.h"
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
#include "ui/option_item.h"
#include "ui/osd_renderer.h"
#include "ui/script_launch.h"
#include "ui/script_run_page.h"
#include "ui/slot_pick_host.h"
#include "ui/progress_page.h"
#include "ui/info_page.h"
#include "ui/system_page.h"
#include "ui/key_decoder.h"
#include "ui/page_context.h"
#include "ui/page_home.h"
#include "ui/ui_event.h"

namespace mister::ui {

bool MenuModel::global_hotkeys_allowed() const noexcept {
    const auto p = stack_.focus_top();
    return !p || p->facts().allows_global_hotkeys;
}

bool MenuModel::popups_visible() const noexcept {
    const auto top = stack_.top();
    if (!top)
        return true;
    return top->facts().allows_popups;
}

bool MenuModel::menu_present() const noexcept {
    const auto top = stack_.top();
    return top && top->facts().counts_as_menu_present;
}

bool MenuModel::allow_cfg_switch() const {
    const auto top = stack_.top();
    return top && top->facts().allows_cfg_switch;
}

namespace {

std::optional<std::string_view> declared_title(
    const proto::ItemTable& items,
    proto::SubmenuNo page
) {
    for (const proto::Item& it : items.items()) {
        if (it.declares_page && it.declared_page == page)
            return items.subfield(it, 1);
    }
    return std::nullopt;
}

}

void MenuModel::seed_navigator() noexcept {
    const auto top = stack_.top();
    if (!top) {
        nav_.set_mask(MenuMask{});
        return;
    }
    nav_.set_mask(top->mask());
    nav_.set_index(top->selection());
}

void MenuModel::arm_auto_pop(const PageContext& ctx) noexcept {

    const PageStack& pages = stack_;
    deadlines_.arm(pages.top(), ctx.clock);
}

std::unique_ptr<Page> MenuModel::make_page(PageId id, const PageArg& arg, PageContext& ctx) {
    if (!arg.fits(page_takes(id))) {
        ++root_refusals_;
        return nullptr;
    }
    switch (id) {
        case PageId::System: {

            SessionFacts session = ctx.session;
            session.in_hd_tree = hd_tree();
            return make_system_page(ctx.sys, session);
        }
        case PageId::CoreOptions:
            return make_core_options_page_(*arg.submenu_or_root());

        case PageId::SelectIni:
            return std::make_unique<SelectIniPage>(ctx.ini);
        case PageId::Misc:
            return std::make_unique<MiscPage>(ctx.ini);
        case PageId::Browser: {

            if (!ctx.launches.browser || !ctx.storage) {
                ++root_refusals_;
                return nullptr;
            }
            std::optional<BrowserSession::Request> req = ctx.launches.browser->take();
            if (!req) {
                ++root_refusals_;
                return nullptr;
            }
            const auto parent = stack_.top();
            const RowCount h = parent ? parent->osd_height() : kFullOsdRows;
            return std::make_unique<BrowserPage>(
                *ctx.storage,
                static_cast<BrowserSession::Request&&>(*req),
                h,
                ctx.clock
            );
        }
        case PageId::ScriptRun: {
            if (!ctx.launches.script) {
                ++root_refusals_;
                return nullptr;
            }
            std::optional<ScriptLaunch::Request> armed = ctx.launches.script->take();
            if (!armed) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<ScriptRunPage>(static_cast<ScriptLaunch::Request&&>(*armed));
        }
        case PageId::Cheats: {

            if (cheats_.count() == 0) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<CheatsPage>(cheats_);
        }
        case PageId::CoreSettings:
            return make_core_settings_page_(*arg.submenu_or_root());
        case PageId::ConfigSlots: {
            if (mirror_.config_slot_table().count == 0) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<ConfigSlotsPage>(
                mirror_.config_slot_table(),
                *arg.slots_mode(),
                mirror_.conf_gen()
            );
        }
        case PageId::ArcadeDip: {

            if (mirror_.dip_table().labels.count.empty()) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<DipsPage>(mirror_.dip_table());
        }
        case PageId::JoyMap: {

            if (!ctx.joymap || !ctx.files) {
                ++root_refusals_;
                return nullptr;
            }
            JoyMapPage::Spec spec{};
            spec.ends = ctx.joymap;
            spec.files = ctx.files;
            spec.front_end = binding_.front_end();
            if (ctx.identity) {
                const std::optional<app::SessionIdentity> cid = ctx.identity->copy();
                if (cid) {
                    spec.front_end = cid->front_end;
                    spec.core = std::string(cid->core.view());
                    spec.j_names = std::string(cid->j_names.view());
                }
            }
            return std::make_unique<JoyMapPage>(static_cast<JoyMapPage::Spec&&>(spec));
        }
        case PageId::Confirm: {
            if (!ctx.launches.confirm) {
                ++root_refusals_;
                return nullptr;
            }
            std::optional<ConfirmPage::Spec> spec = ctx.launches.confirm->take();
            if (!spec) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<ConfirmPage>(*spec);
        }
        case PageId::TextEntry: {
            std::optional<TextEntryRequest> request;
            if (ctx.launches.entry)
                request = ctx.launches.entry->take();
            if (!request) {
                ++root_refusals_;
                return nullptr;
            }
            return std::make_unique<TextEntryPage>(*request);
        }

        case PageId::HdMenu:
            refresh_recent_(ctx);
            if (binding_.front_end()) {
                return make_hd_list_(std::make_unique<HdHomePlan>(plan_inputs_(ctx, arg)), arg);
            }
            return make_hd_list_(std::make_unique<HdRootPlan>(plan_inputs_(ctx, arg)), arg);

        case PageId::HdCoreSettings: {
            const proto::SubmenuNo page = *arg.submenu_or_root();
            std::string title{"Core settings"};
            if (page != proto::kRootSubmenu) {
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
            return make_hd_list_(std::make_unique<HdCoreAboutPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdSettings:
            return make_hd_list_(std::make_unique<HdSettingsPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdControllers:
            return make_hd_list_(std::make_unique<HdControllersPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdDisplay:
            return make_hd_list_(std::make_unique<HdDisplayPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdAbout:
            return make_hd_list_(std::make_unique<HdAboutPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdMedia:
            return make_hd_list_(std::make_unique<HdMediaPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdSlotSheet:
            return make_hd_list_(std::make_unique<HdSlotSheetPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::HdRecentGames:
            refresh_recent_(ctx);
            return make_hd_list_(std::make_unique<HdRecentGamesPlan>(plan_inputs_(ctx, arg)), arg);

        case PageId::Network:
            return make_hd_list_(std::make_unique<NetworkPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::WifiNetworks:
            return make_hd_list_(std::make_unique<WifiNetworksPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::WifiSaved:
            return make_hd_list_(std::make_unique<WifiSavedPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::WifiAdvanced:
            return make_hd_list_(std::make_unique<WifiAdvancedPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::CountryPick:
            return make_hd_list_(std::make_unique<CountryPickPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::Bluetooth:
            return make_hd_list_(std::make_unique<BluetoothPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::BtPair:
            return make_hd_list_(std::make_unique<BtPairPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::BtDevices:
            return make_hd_list_(std::make_unique<BtDevicesPlan>(plan_inputs_(ctx, arg)), arg);
        case PageId::None:
        case PageId::Info:
        case PageId::Progress:
        case PageId::Unlock:
        case PageId::Recents:
        case PageId::About:
        case PageId::Common:
        case PageId::VideoProc:
        case PageId::Uart:
        case PageId::UartMode:
        case PageId::UartBaud:
        case PageId::FbTerminal:
        case PageId::KbdMap:
        case PageId::LightGunCal:
        case PageId::AdvancedMapList:
        case PageId::AdvancedMapEdit:
        case PageId::AdvancedMapButtons:
        case PageId::AdvancedMapCapture:
        case PageId::Mt32Pi:
        case PageId::kCount:

            ++root_refusals_;
            return nullptr;
    }

    ++root_refusals_;
    return nullptr;
}

std::unique_ptr<Page> MenuModel::make_core_options_page_(proto::SubmenuNo page) {
    std::string_view title = items_.core_name();
    if (page != proto::kRootSubmenu)
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

std::unique_ptr<Page> MenuModel::make_core_settings_page_(proto::SubmenuNo page) {
    if (mirror_.option_table().count == 0 || page.v >= cores::kMaxOptionPages) {
        ++root_refusals_;
        return nullptr;
    }
    CoreSettingsPage::Spec spec{mirror_.option_table()};
    spec.page = page;

    const std::optional<std::string_view> declared = mirror_.option_table().title(page);
    spec.title = declared
        ? std::string(*declared)
        : (page == proto::kRootSubmenu ? std::string(items_.core_name()) : std::string{});
    spec.mt32_available = hd_mask_.has(kMt32Present);

    const BuildEnv env = build_env(page);
    spec.custom_aspect_ratio[0] = env.custom_aspect_ratio[0];
    spec.custom_aspect_ratio[1] = env.custom_aspect_ratio[1];
    spec.conf_gen = env.conf_gen;
    auto p = std::make_unique<CoreSettingsPage>(static_cast<CoreSettingsPage::Spec&&>(spec));
    if (p->empty_page()) {
        ++root_refusals_;
        return nullptr;
    }
    return p;
}

HdPlanInputs MenuModel::plan_inputs_(PageContext& ctx, const PageArg& arg) {
    return HdPlanInputs{
        .ctx = ctx,
        .style = style_,
        .items = items_,
        .status = mirror_.status_words(),
        .verbs = verbs_,
        .now_playing = now_playing_,
        .recent_games = recent_games_,
        .env = build_env(proto::kRootSubmenu),
        .arg = arg,
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
        .row = p.row
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

std::unique_ptr<Page> MenuModel::make_hd_list_(std::unique_ptr<IHdPlan> plan, const PageArg& arg) {
    auto p = std::make_unique<HdListPage>(std::move(plan), arg, hd_focus_);

    if (p->empty()) {
        ++root_refusals_;
        return nullptr;
    }
    return p;
}

void MenuModel::apply(const Transition& t, PageContext& ctx) {
    switch (t.kind()) {
        case Transition::Kind::Stay:
            return;
        case Transition::Kind::Push: {
            const Transition::Push& a = *t.as_push();
            const bool from_hd = stack_.top_id() == PageId::HdSettings && a.page == PageId::System;
            auto p = make_page(a.page, a.arg, ctx);
            if (p == nullptr)
                return;
            if (from_hd)
                p->set_selection(system_row_of(SystemItem::Exit));
            stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
            forget_hd_window_(stack_.top_level());
            if (auto top = stack_.top())
                top->on_enter(ctx);

            if (from_hd && binding_.front_end()) {
                if (auto sys = stack_.top())
                    apply(sys->open_root_child(ctx), ctx);
            }
            break;
        }
        case Transition::Kind::Pop: {

            const StackDepth popped = stack_.top_level();
            stack_.pop();
            forget_hd_window_(popped);
            break;
        }
        case Transition::Kind::Replace: {
            const Transition::Replace& a = *t.as_replace();
            auto p = make_page(a.page, a.arg, ctx);
            if (p == nullptr)
                return;
            stack_.replace(static_cast<std::unique_ptr<Page>&&>(p));
            forget_hd_window_(stack_.top_level());
            if (auto top = stack_.top())
                top->on_enter(ctx);
            break;
        }
        case Transition::Kind::CloseAll:

            stack_.clear();
            break;
        case Transition::Kind::ChooseHdStyle:

            style_.choose_hd();
            reopen_root_(ctx);
            break;
        case Transition::Kind::ChooseClassicStyle:

            style_.choose_classic();
            reopen_root_(ctx);
            break;
        case Transition::Kind::Leave:

            if (stack_.bottom_id() == PageId::HdMenu && stack_.depth() > 1) {
                stack_.pop();
            } else {
                stack_.clear();
            }
            break;
        case Transition::Kind::kCount:
            return;
    }
    seed_navigator();
    arm_auto_pop(ctx);
    repaint_ = true;
}

void MenuModel::reopen_root_(PageContext& ctx) {
    stack_.clear();
    open_root_(ctx, true);
}

bool MenuModel::hd_tree() const noexcept {
    if (stack_.bottom_id() == PageId::HdMenu)
        return true;
    return hd_root_ && style_.hd() && stack_.depth() == 1 && stack_.top_id() == PageId::Progress;
}

bool MenuModel::describe_top(app::PageDescription& out) {
    const auto top = stack_.top();
    if (!top)
        return false;
    const StackDepth level = stack_.top_level();

    const auto window = hd_window_first_.find(level);
    out.first = window ? *window : RowIndex{};
    if (!top->describe(out))
        return false;
    if (window)
        *window = out.first;

    if (level.above_bottom()) {
        out.hub = {};
        const auto hub = stack_.bottom();
        if (hub && hub->describe_hub(out))
            out.hub.depth = level.hub_depth();
    }

    if (stack_.top_id() == PageId::CoreOptions
        && !items_.core_name().empty()
        && infra::ieq(out.title.view(), items_.core_name()))
        (void)out.title.assign("Advanced");
    out.crumb = app::hd_crumb(items_.core_name(), out.title.view());
    if (now_playing_.game_loaded()) {
        (void)out.subtitle.assign(now_playing_.title());
    } else if (!binding_.front_end()) {
        (void)out.subtitle.assign(now_playing_.idle_line(items_, build_env(proto::kRootSubmenu)));
    }
    return true;
}

void MenuModel::on_request_done(
    proto::CorrelationTag tag,
    std::int64_t unix_time,
    PageContext& ctx
) {
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    const std::optional<app::SessionIdentity> cid = identity_.copy();
    const std::span<const cores::FileSlot> slots = cid ? cid->slots
                                                       : std::span<const cores::FileSlot>{};
    const bool recorded = now_playing_
                              .on_done(asks ? asks->pending(tag) : std::nullopt, slots, unix_time);
    if (asks)
        asks->forget(tag);
    const app::RecentGames::Played p = played_record_();

    if (recorded && ctx.played && p.row) {
        std::string image{p.core_image};

        if (image.empty() && ctx.files.has_value())
            image = app::RecentGames::core_image_of(*ctx.files, p.core_name);
        (void)ctx.played->record(
            app::PlayedHistory::Record{
                .core_image = std::move(image),
                .core_name = std::string{p.core_name},
                .path = std::string{p.path},
                .title = std::string{p.title},
                .row = *p.row,
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

void MenuModel::forget_hd_window_(StackDepth level) noexcept {
    if (auto window = hd_window_first_.find(level))
        *window = RowIndex{};
}

bool MenuModel::show_progress(PageContext& ctx, cores::Progress progress) {
    const auto top = stack_.top();
    const auto view = top ? top->progress_view() : infra::OptRef<IProgressView>{};
    if (progress.total == 0) {
        transfers_.clear();
        if (!view)
            return false;
        stack_.pop();
        seed_navigator();

        arm_auto_pop(ctx);
        invalidate_surface();
        repaint_ = true;
        return true;
    }
    transfers_.begin(ctx);
    if (view) {
        view->set_progress(progress);

        arm_auto_pop(ctx);
        repaint_ = true;
        return true;
    }

    if (menu_has_focus()) {
        ++progress_masked_;
        return false;
    }
    auto p = make_progress_page(transfers_.name());
    if (p == nullptr)
        return false;
    if (auto v = p->progress_view())
        v->set_progress(progress);
    place_overlay(static_cast<std::unique_ptr<Page>&&>(p));
    if (auto t = stack_.top())
        t->on_enter(ctx);
    seed_navigator();
    arm_auto_pop(ctx);
    repaint_ = true;
    return true;
}

void MenuModel::place_overlay(std::unique_ptr<Page> p) {
    const auto top = stack_.top();
    if (top && !top->facts().takes_input_focus) {
        stack_.replace(static_cast<std::unique_ptr<Page>&&>(p));
    } else {
        stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
    }
    forget_hd_window_(stack_.top_level());
}

bool MenuModel::push_info(PageContext& ctx, app::InfoId id) {
    auto p = make_info_page(id);
    if (p == nullptr)
        return false;
    place_overlay(static_cast<std::unique_ptr<Page>&&>(p));
    if (auto top = stack_.top())
        top->on_enter(ctx);
    seed_navigator();

    arm_auto_pop(ctx);
    repaint_ = true;
    return true;
}

MenuModel::KeyPhase MenuModel::pump_key_(PageContext& ctx) {
    KeyPhase out{};

    std::optional<UiEvent> ev;
    if (ctx.keys) {
        const auto rt = stack_.top();
        ctx.keys->set_repeat_all(rt && rt->facts().repeat_all_keys);
        ev = ctx.keys->pop();
    }
    if (!ev.has_value())
        return out;

    if (bootcore_armed_) {
        bootcore_armed_ = false;
        ++bootcore_cancels_;
    }

    const UiButtonMask buttons = ev->buttons;

    constexpr std::uint16_t kRearm = UiEvent::Menu
        | UiEvent::Select
        | UiEvent::Up
        | UiEvent::Down
        | UiEvent::Left
        | UiEvent::Right
        | UiEvent::Plus
        | UiEvent::Minus;
    if (buttons.has_any({kRearm})) {
        ++help_rearms_;
        if (ctx.clock) {
            deadlines_.rearm_help(ctx.clock->now().time_since_epoch());
        }
    }

    if (!menu_has_focus()) {
        if (buttons.has_any({UiEvent::Menu})) {
            if (osd_open()) {

                stack_.clear();
                seed_navigator();
                arm_auto_pop(ctx);
                show_latch_.mark_closed();
                ++overlay_trades_;
            }
            open_root(ctx);
            out.did = true;
        }
        out.round_over = true;
        return out;
    }

    bool routed = false;
    if (KeyDecoder::is_make(ev->raw, svc::EvdevCode{KEY_F11})) {
        routed = true;
        const auto run = stack_.top();
        const bool mid_script = run && run->holds_live_script();
        if (mid_script) {
            ++bt_refusals_;
        } else {
            open_bt_pairing(ctx);
            out.did = true;
        }
    }

    auto top = stack_.top();
    if (routed)
        top = std::nullopt;

    const HdKeyRemap::Axis axis = top ? remap_.axis(*this, buttons) : HdKeyRemap::Axis{};
    const bool across = axis.move != HdKeyRemap::Axis::Move::None;
    if (top) {
        bool moved = false;
        if (across) {
            moved = remap_.apply(axis, nav_);
        } else if (buttons.has_any({UiEvent::Up})) {
            moved = nav_.move(Navigator::Direction::Up);
        } else if (buttons.has_any({UiEvent::Down})) {
            moved = nav_.move(Navigator::Direction::Down);
        }
        if (moved) {
            top->set_selection(nav_.index());
            repaint_ = true;
        }
    }

    if (top) {
        UiEvent forwarded = *ev;
        constexpr std::uint16_t kSides = UiEvent::Left | UiEvent::Right;
        forwarded.buttons = across ? buttons.without({kSides}) : buttons;

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
        out.did = true;
    }
    return out;
}

bool MenuModel::pump(PageContext& ctx, OsdRenderer& out) {
    ++pumps_;

    sync_cells();
    stamp_session(ctx);

    if (ctx.connect) {
        const ConnectDemand wanted = stack_.connect_demand();
        if (ctx.connect->pump(wanted) && wanted != ConnectDemand{})
            repaint_ = true;
    }

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

    if (auto top = stack_.top()) {
        const MenuMask before = top->mask();
        top->on_facts(ctx.sys, ctx.session);

        if (top->mask() != before) {
            nav_.set_mask(top->mask());
            nav_.set_index(top->selection());
        }

        out.set_visible_rows(
            binding_.front_end() ? proto::OsdSurface::kMaxRows : osd_rows_of(top->osd_height())
        );
    }

    bool did = false;

    if (auto top = stack_.top()) {
        const Transition t = top->on_tick(ctx);
        if (t.kind() != Transition::Kind::Stay) {
            apply(t, ctx);
            did = true;
        } else if (top->facts().wake_every_round) {

            repaint_ = true;
        }
    }

    if (ctx.clock && deadlines_.fired(ctx.clock->now().time_since_epoch())) {
        deadlines_.consume_pop();
        if (!stack_.empty()) {
            stack_.pop();
            seed_navigator();
            arm_auto_pop(ctx);
            repaint_ = true;
            did = true;
        }
    }

    const KeyPhase key = pump_key_(ctx);
    did = did || key.did;
    if (key.round_over)
        return did;

    if (show_latch_.on_open(osd_open())) {

        if (!cleared_this_round) {
            ++invalidations_;
            out.clear();
            repaint_ = true;
        }
    }

    if (repaint_) {
        repaint_ = false;
        if (auto top = stack_.top()) {
            out.set_height(top->osd_height());

            const bool strip = top->facts().has_title_strip || menu_has_focus();
            out.set_stripe(strip ? OsdRenderer::Stripe::Title : OsdRenderer::Stripe::Blank);
            top->render(out);

            proto::InfoWindow window = top->info_window().value_or(proto::InfoWindow{});
            if (!window.empty())
                window.y = ctx.session.info_origin;
            out.publish_info_window(window);
            did = true;
        }
    }

    if (binding_.front_end() && ctx.clock) {
        if (deadlines_.take_status_tick(ctx.clock->now().time_since_epoch())) {

            ctx.status.sdram_cfg = sdram_cfg();
            out.composite_status_bar(ctx.status);
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
    stamp_session(ctx);

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

    sdram_latch_.on_mask(m, front_end, asks);

    if (!(m == hd_mask_)) {
        hd_mask_ = m;

        const auto top = stack_.top();
        if (auto co = top ? top->mask_rebuild() : infra::OptRef<IMaskRebuild>{}) {
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

void MenuModel::on_progress(cores::Progress progress, PageContext& ctx) {

    sync_cells();

    if (!binding_.ui_capable()) {
        ++progress_refusals_;
        return;
    }
    (void)show_progress(ctx, progress);
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
     "rows`, never a core name"},
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
    return app::pause_badge(
        pause_probe_,
        binding_.pause_rule(),
        mirror_.status_words().status,
        osd_show() == proto::LinkOp::OsdShow::Menu
    );
}

std::string_view MenuModel::browse_home() const noexcept {
    return binding_.browse_home(items_.core_name());
}

app::RememberedStem MenuModel::file_stem() const noexcept {
    return binding_.file_stem(items_.core_name());
}

void MenuModel::stamp_session(PageContext& ctx) const noexcept {
    ctx.session.core_loaded = binding_.ui_capable() && !binding_.front_end();
    ctx.session.front_end = binding_.front_end();

    const std::string_view name = binding_.front_end() ? std::string_view{} : browse_home();
    (void)ctx.session.core_dir.assign(name);

    ctx.session.in_hd_tree = hd_tree();
}

bool MenuModel::cells_pending() const noexcept {
    return mirror_.pending();
}

bool MenuModel::connect_pending(const PageContext& ctx) const noexcept {
    return ctx.connect && ctx.connect->pending(stack_.connect_demand());
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
    deadlines_.disarm();
    repaint_ = true;
}

void MenuModel::disarm_launches_(PageContext& ctx) noexcept {
    if (ctx.launches.browser)
        ctx.launches.browser->disarm();
    if (ctx.launches.script)
        ctx.launches.script->disarm();
    if (ctx.launches.confirm)
        ctx.launches.confirm->disarm();
    if (ctx.launches.entry)
        ctx.launches.entry->disarm();
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

BuildEnv MenuModel::build_env(proto::SubmenuNo page) const noexcept {
    BuildEnv env;

    env.hdmask = hd_mask_.osd_word();
    env.page = page;
    env.flat = flat_;

    env.core_dir = binding_.front_end() ? std::string_view{} : browse_home();
    env.file_stem = binding_.front_end() ? app::RememberedStem{} : file_stem();

    {
        const std::optional<app::SessionIdentity> cid = identity_.copy();
        if (cid) {
            if (!cid->cue_dir.empty())
                env.cue_dir = cid->cue_dir;

            constexpr std::size_t kSlotsInAByte = std::numeric_limits<std::uint8_t>::max();
            env.slots = cid->slots.first(std::min(cid->slots.size(), kSlotsInAByte));
            env.image_no_zip = cid->image_no_zip;
        }
    }

    env.cheats_available = cheats_.count() != 0;
    env.manual_available = false;
    env.dip_count = mirror_.dip_table().labels.count;
    env.conf_gen = mirror_.conf_gen();
    return env;
}

bool MenuModel::core_options_available() const {
    OptionsModel probe;
    probe.build(items_, build_env(proto::kRootSubmenu));
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

        auto p = make_page(row.page, PageArg{}, ctx);
        if (p == nullptr)
            continue;
        if (row.arm == RootArm::System) {

            p->set_selection(system_row_of(SystemItem::Exit));
        }
        stack_.push(static_cast<std::unique_ptr<Page>&&>(p));
        forget_hd_window_(stack_.top_level());
        if (auto top = stack_.top())
            top->on_enter(ctx);

        if (binding_.front_end() && row.page == PageId::System) {
            if (auto sys = stack_.top())
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

    if (stack_.any_bt_pairing()) {
        ++bt_refusals_;
        return;
    }
    const PageArg arg{PairStart::AutoFirst};
    apply(
        osd_open() ? Transition::replace(PageId::BtPair, arg)
                   : Transition::push(PageId::BtPair, arg),
        ctx
    );
}

bool MenuModel::open_select_ini(PageContext& ctx) {
    auto p = make_page(PageId::SelectIni, PageArg{}, ctx);
    if (p == nullptr)
        return false;
    stack_.clear();
    stack_.push(std::move(p));
    if (auto page = stack_.top())
        page->on_enter(ctx);
    seed_navigator();
    arm_auto_pop(ctx);
    invalidate_surface();
    repaint_ = true;
    return true;
}

}
