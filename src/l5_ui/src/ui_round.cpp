// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ui_round.h"

#include <algorithm>
#include <cstdint>

#include "app/hd_osd_screen.h"
#include "app/hps_framebuffer.h"
#include "app/info_origin.h"
#include "app/page_description.h"
#include "app/link_event_dispatch.h"
#include "app/recents_store.h"
#include "app/ui_write_lane.h"
#include "app/video_pump.h"
#include "ui/host_sampler.h"
#include "ui/key_decoder.h"
#include "ui/osd_visible_file.h"
#include "ui/page_ask.h"

#include <linux/input-event-codes.h>

namespace mister::ui {

bool UiRound::on(const proto::LinkEvent::ButtonLevel& b) noexcept {
    TASTY_SEAT_BODY(UiRound);
    ++button_levels_;
    osd_held_ = b.osd;

    if (user_held_ && !b.user)
        user_release_pending_ = true;
    user_held_ = b.user;
    return false;
}

void UiRound::on(const app::RawKeyEdge& edge) noexcept {
    TASTY_SEAT_BODY(UiRound);
    ++keys_drained_;

    if (edge.intent == app::KeyIntent::IniSwitch && model_.allow_cfg_switch()) {
        const app::UiRequest::SwitchIni ask{.ini = svc::ini_choice_of_direction(edge.code)};
        (void)push_ask(ctx_.ui_requests, ask);
        return;
    }

    if (idle_clock_.on_key(edge))
        return;

    if (front_end_.on_key(edge, model_.status_words(), model_.front_end(), ui_tx_))
        return;
    proto::KeyCode code = edge.code.wire_key();
    if (!edge.pressed)
        code = code.with_release();
    decoder_.push_raw(code);
}

bool UiRound::on(const proto::LinkEvent::OsdMask& m) noexcept {
    TASTY_SEAT_BODY(UiRound);
    model_.on_hd_mask(HdMask{m.mask.v}, m.front_end, ctx_.ui_requests);
    return false;
}

bool UiRound::on(const proto::LinkEvent::PolledWord& w) {
    TASTY_SEAT_BODY(UiRound);
    (void)info_.decode(w);
    return false;
}

bool UiRound::take_link(const proto::LinkEvent& ev) noexcept {
    TASTY_SEAT_BODY(UiRound);
    return infra::dispatch<app::LinkEventMenuRoutes>(ev, *this);
}

bool UiRound::misrouted(const proto::LinkEvent&) noexcept {
    TASTY_SEAT_BODY(UiRound);
    ++link_event_misrouted_;
    return false;
}

static_assert(app::LinkEventMenuSink<UiRound>);

bool UiRound::poll_buttons(bool link_work) {

    bool drained = link_work;

    if (const app::CoreEdgeSeq edges = binding_.core_edges(); edges != core_edges_seen_) {
        core_edges_seen_ = edges;

        const bool held_at_boot = osd_held_;
        osd_btn_ = ButtonMachine{};
        user_btn_ = ButtonMachine{};
        osd_held_ = false;
        user_held_ = false;
        user_release_pending_ = false;
        ++button_core_resets_;
        if (!boot_core_seen_ && model_.front_end()) {
            boot_core_seen_ = true;
            boot_select_ini_ = held_at_boot;
        }
    }
    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();

    {
        const bool but = osd_held_;
        ButtonMachine& m = osd_btn_;
        if (m.press_edge(but, now, kOsdLongPress))
            ++button_edges_;
        if (m.long_ready(but, now)) {
            ++button_longs_;
            button_actions_.add();

            const auto top = model_.stack().top();
            if (top && top->takes_osd_button_backspace()) {
                decoder_.push_synth(proto::KeyCode{KEY_BACKSPACE}.with_release());
                ++button_injects_;
                drained = true;
            } else if (model_.global_hotkeys_allowed()) {
                model_.open_bt_pairing(ctx_);
            } else {
                ++bt_route_refusals_;
            }
        }
        const std::optional<std::chrono::nanoseconds> held = m.release_hold(but, now);
        if (held) {

            last_hold_ = std::chrono::duration_cast<std::chrono::milliseconds>(*held);
        }
        if (held && !m.consumed()) {

            const bool wants_classic = *held >= kOsdClassicHold
                && *held < kOsdLongPress
                && model_.hd_root()
                && !model_.osd_open();
            if (wants_classic && model_.open_classic_root(ctx_)) {
                ++button_classic_opens_;
            } else {
                decoder_.push_synth(proto::KeyCode{KEY_F12}.with_release());
                ++button_injects_;
            }
            button_actions_.add();
            drained = true;
        }
        m.end_sample(but);
    }

    {
        const bool but = user_held_;
        ButtonMachine& m = user_btn_;
        if (model_.menu_has_focus()) {
            if (m.press_edge(but, now, kUserLongPress))
                ++button_edges_;
            if (m.long_ready(but, now)) {

                ++button_longs_;
                button_actions_.add();
            }
            const std::optional<std::chrono::nanoseconds> released = m.release_hold(but, now);
            if (released && !m.consumed()) {

                ++button_edges_;
                button_actions_.add();
            }
        } else if (user_release_pending_ || m.release_hold(but, now)) {

            ask_user_reset_();
        }
        user_release_pending_ = false;
        m.end_sample(but);
    }
    return drained;
}

void UiRound::ask_user_reset_() {
    const app::UiRequest::ResetCore ask{
        .edge = proto::ResetEdge::user_button(),
        .scope = app::CoreScope{model_.conf_gen()}
    };
    if (!push_ask(ctx_.ui_requests, ask)) {
        ++user_reset_refusals_;
        return;
    }
    ++user_resets_asked_;
}

bool UiRound::publish_visibility(proto::LinkOp::OsdShow show, proto::OsdTarget target) {
    ++vis_pubs_;
    const proto::LinkOp::SetOsdVisible op{.show = show, .target = target};

    if (!ui_tx_.push(op)) {
        ++ui_tx_drops_;
        return false;
    }
    return true;
}

void UiRound::publish_focus_(std::optional<proto::LinkOp::OsdShow> published_show) {
    const app::KeyFocus focus = last_show_ != proto::LinkOp::OsdShow::Menu
        ? app::KeyFocus::Core
        : (model_.allow_cfg_switch() ? app::KeyFocus::MenuIniSwitch : app::KeyFocus::Menu);
    const bool focus_edge = focus != last_focus_;
    if (focus_edge) {
        last_focus_ = focus;
        key_focus_.publish(focus);
    }
    if (focus_edge || published_show.has_value()) {
        osd_file_.on_visible(focus != app::KeyFocus::Core);
    }
}

void UiRound::settle() noexcept {
    TASTY_SEAT_BODY(UiRound);
    if (hd_)
        hd_->release_console();
}

void UiRound::boot_witness() {
    TASTY_SEAT_BODY(UiRound);
    osd_file_.on_visible(last_show_ == proto::LinkOp::OsdShow::Menu);
}

void UiRound::publish_page_record() {
    const auto us_of = [](std::int64_t ns) noexcept {
        return static_cast<std::uint32_t>(std::clamp<std::int64_t>(ns / 1000, 0, UINT32_MAX));
    };
    const PageStack& st = model_.stack();
    app::UiPageRecord rec{
        static_cast<std::uint32_t>(st.depth()),
        static_cast<std::uint32_t>(st.top_id()),
        vis_pubs_,
        model_.cell_refusals(),
        model_.page_refusals()
    };
    rec.button_levels = button_levels_;
    rec.button_edges = button_edges_;
    rec.button_longs = button_longs_;
    rec.button_classic = button_classic_opens_;
    rec.button_injects = button_injects_;
    rec.button_core_resets = button_core_resets_;
    rec.button_actions = button_actions_;
    rec.last_hold_ms = static_cast<std::uint32_t>(last_hold_.count());
    const app::RecentGames& rg = model_.recent_games();
    rec.recent_build_us = us_of(rg.build_ns());
    rec.recent_update_us_max = us_of(rg.update_ns_max());

    rec.lane_refusals = writes_ ? writes_->refusals() : 0;
    rec.lane_failures = writes_ ? writes_->failures() : 0;
    rec.store_errors = ctx_.recents ? ctx_.recents->errors() : 0;
    page_cell_.publish(rec);
}

app::UiRoundEdge UiRound::compose_(app::UiPassWork work) {
    const PublishOnExit republish{this};
    ++ticks_;

    bool drained = work.menu_keys;

    if (poll_buttons(work.link_events))
        drained = true;

    if (boot_select_ini_) {
        boot_select_ini_ = false;
        if (model_.open_select_ini(ctx_))
            drained = true;
    }

    const bool deadline_due = model_.deadline_due(clock_.now().time_since_epoch());
    const auto top = model_.stack().top();
    const bool page_poll = top && top->facts().wake_every_round;

    const bool hd_switchable = hd_ && hd_->available();
    if (hd_switchable != ctx_.session.hd_switchable)
        model_.request_repaint();

    ctx_.session.info_origin = app::info_origin_y(
        video_ && video_->direct_video(),
        fb_ && fb_->raised()
    );
    const bool idle = !drained
        && !page_poll
        && !decoder_.pending()
        && !model_.repaint_pending()
        && !model_.cells_pending()
        && !model_.connect_pending(ctx_)
        && !deadline_due;
    if (!idle) {
        ++pumps_;

        const HostSample host = sampler_.snapshot();
        ctx_.sys = host.sys;
        ctx_.status = host.bar;
        ctx_.session.hd_switchable = hd_switchable;
        ++samples_;

        (void)model_.pump(ctx_, renderer_);
    }

    front_end_.round(
        model_.status_words(),
        model_.front_end() && !model_.launcher_hosting(),
        ctx_.ui_requests
    );

    const app::IniCell::Sample catalog = ini_.sample();
    if (catalog.generation != ini_label_gen_) {
        ini_label_gen_ = catalog.generation;
        ini_label_ = catalog ? catalog.value.index.label_for(catalog.value.active) : std::string{};
    }

    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();
    const app::IdleKnobsCell::Sample knobs = idle_.sample();
    const FrontEndIdleClock::Result idle_state = idle_clock_.round(
        FrontEndIdleClock::Inputs{
            .now = now,
            .any_key = work.menu_keys,
            .on_front_page = model_.stack().top_id() == PageId::Browser
                || model_.stack().top_id() == PageId::HdMenu,
            .front_end = model_.front_end(),
            .knobs_published = static_cast<bool>(knobs),
            .knobs = knobs ? knobs.value : app::IdleKnobs{},
        }
    );
    if (idle_state.ctl) {
        const bool landed = ui_tx_.push(proto::LinkOp::MenuCtl{.enable = *idle_state.ctl});
        idle_clock_.ctl_pushed(landed);
    }

    if (hd_) {
        app::PageDescription described{};
        const app::PageDescription* page = nullptr;
        if (model_.hd_tree() && model_.describe_top(described))
            page = &described;
        const app::HdStatusSlots slots{
            .bluetooth = ctx_.status.bluetooth,
            .rt = ctx_.sys.kernel == os::KernelRt::PreemptRt
        };
        hd_->tick(
            infra::opt_ref_of(page),
            model_.pause_badge(),
            slots,
            model_.style().look(),
            front_end_.bg(),
            idle_clock_.idle(),
            model_.front_end() ? std::string_view{ini_label_} : std::string_view{}
        );
        model_.set_hd_root(hd_->available());
    }
    return app::UiRoundEdge{
        .pumped = !idle,
        .target = hd_ && hd_->intends_hdmi() ? proto::OsdTarget::Vga : proto::OsdTarget::All,
        .menu_present = model_.menu_present()
    };
}

void UiRound::publish_edges_(const app::UiRoundEdge& edge) {
    const std::uint32_t pubs = vis_pubs_;
    const proto::OsdTarget target = edge.target;

    const bool owed = vis_unlatched_ || target != last_target_;
    const proto::LinkOp::OsdShow show = edge.pumped || vis_unlatched_ ? model_.osd_show()
                                                                      : last_show_;

    const bool awake = idle_clock_.idle() == app::FrontEndIdle::Awake;
    std::optional<proto::LinkOp::OsdShow> vis_edge{};
    if (awake && (edge.pumped ? (show != last_show_ || target != last_target_) : owed)) {

        if (last_show_ == proto::LinkOp::OsdShow::Menu && show != last_show_)
            decoder_.reset();
        if (publish_visibility(show, target)) {
            last_show_ = show;
            last_target_ = target;
            vis_unlatched_ = false;
            vis_edge = show;
        } else {
            vis_unlatched_ = true;
        }
    }
    publish_focus_(vis_edge);
    if (vis_pubs_ != pubs)
        publish_page_record();
}

}
