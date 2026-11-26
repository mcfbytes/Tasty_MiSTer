// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ui_round.h"

#include <algorithm>
#include <cstdint>

#include "app/hd/hd_osd_screen.h"
#include "app/hps_framebuffer.h"
#include "app/info_origin.h"
#include "app/info_plane.h"
#include "app/hd/page_description.h"
#include "app/link_event_dispatch.h"
#include "app/recents/recents_store.h"
#include "app/recents/ui_write_lane.h"
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

void UiRound::on(const app::input::RawKeyEdge& edge) noexcept {
    TASTY_SEAT_BODY(UiRound);
    ++keys_drained_;

    if (edge.intent == app::input::KeyIntent::IniSwitch && model_.allow_cfg_switch()) {
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
    model_.on_hd_mask(m.mask, m.front_end, ctx_.ui_requests);
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
    const os::RawMonotonicClock::time_point now = clock_.now();

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

bool UiRound::terminal_owns_keys_() const noexcept {
    return model_.stack().key_owner() == PageKeys::Terminal;
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
    const app::input::KeyFocus focus = terminal_owns_keys_()
        ? app::input::KeyFocus::Terminal
        : (last_show_ == proto::LinkOp::OsdShow::Menu
               ? (model_.allow_cfg_switch() ? app::input::KeyFocus::MenuIniSwitch
                                            : app::input::KeyFocus::Menu)
               : app::input::KeyFocus::Core);
    const bool focus_edge = focus != last_focus_;
    if (focus_edge) {
        last_focus_ = focus;
        key_focus_.publish(focus);
    }
    if (focus_edge || published_show.has_value()) {
        osd_file_.on_visible(app::input::raises_osd_gate(focus));
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
        label_of(st.top_id()),
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
    const app::recents::RecentGames& rg = model_.recent_games();
    rec.recent_build_us = us_of(rg.build_ns());
    rec.recent_update_us_max = us_of(rg.update_ns_max());

    rec.lane_refusals = writes_ ? writes_->refusals() : 0;
    rec.lane_failures = writes_ ? writes_->failures() : 0;
    rec.store_errors = ctx_.recents ? ctx_.recents->errors() : 0;
    page_cell_.publish(rec);
}

bool UiRound::host_board_(os::RawMonotonicClock::time_point now) {
    bool moved = false;
    if (!board_.active().is_empty() && model_.stack().empty()) {
        moved = model_.open_indicators(ctx_, board_);
    }
    const bool up = model_.stack().top_id() == PageId::Indicators;

    const bool shown_on_hd = hd_ && model_.hd_tree();
    const bool changed = board_.round(
        now,
        up || shown_on_hd ? board_.active() : app::IndicatorSet{}
    );
    if (up && board_.active().is_empty())
        moved = model_.close_indicators(ctx_) || moved;
    if (changed && up)
        model_.request_repaint();
    return moved || changed;
}

std::optional<app::InfoPlane> UiRound::info_plane_() const noexcept {
    if (!video_)
        return std::nullopt;
    const app::VideoGeometryRecord geo = video_->geometry_record();
    return app::info_plane_for(
        app::InfoPlaneFacts{
            .rotation = ctx_.osd_rotation,
            .direct_video = video_->direct_video(),
            .hd_style = hd_ && model_.style().hd(),
            .output = video_->output_size(),
            .core_lines = geo.valid ? std::optional{app::OutputPixels::of(geo.height)}
                                    : std::nullopt,
        }
    );
}

void UiRound::tick_autosave_([[maybe_unused]] os::RawMonotonicClock::time_point now) {

    if (menu_focus_) {
        const bool focus = model_.menu_has_focus();
        if (!last_menu_focus_ || *last_menu_focus_ != focus) {
            last_menu_focus_ = focus;
            menu_focus_->publish(focus);
        }
    }
    if (!autosave_ || !save_director_)
        return;
    const app::AutosaveKnobsCell::Sample knobs = autosave_->sample();
    const app::SaveDirectorCell::Sample status = save_director_->sample();
    const AutosaveClock::Inputs in{
        .knobs = knobs ? knobs.value : app::AutosaveKnobs{},
        .status = status ? std::optional<app::SaveDirectorStatus>{status.value} : std::nullopt,
    };
    (void)autosave_clock_.round(in, board_);
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

    if (info_notices_ && info_notice_reader_.take_if_changed(*info_notices_, info_notice_))
        (void)info_.decode(info_notice_);

    if (host_board_(clock_.now()))
        drained = true;

    const bool deadline_due = model_.deadline_due(clock_.now());
    const auto top = model_.stack().top();
    const bool page_poll = top && top->facts().wake_every_round;

    const bool hd_switchable = hd_ && hd_->available();
    if (hd_switchable != ctx_.session.hd_switchable)
        model_.request_repaint();

    bool autosave_on = false;
    if (autosave_) {
        const app::AutosaveKnobsCell::Sample sampled = autosave_->sample();
        autosave_on = sampled && sampled.value.enabled;
    }
    if (autosave_on != ctx_.session.autosave)
        model_.request_repaint();

    ctx_.session.info_origin = app::info_origin_y(
        video_ && video_->direct_video(),
        fb_ && fb_->raised()
    );
    ctx_.session.info_plane = info_plane_();

    const app::IdleKnobsCell::Sample idle_knobs = idle_.sample();
    const bool console_view = idle_knobs
        && idle_knobs.value.fb_terminal
        && !(video_ && video_->direct_video());
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
        ctx_.session.autosave = autosave_on;
        ctx_.session.console_view = console_view;
        ++samples_;

        (void)model_.pump(ctx_, renderer_);
    }

    const bool terminal_top = terminal_owns_keys_();
    front_end_.round(
        model_.status_words(),
        model_.front_end() && !model_.launcher_hosting() && !terminal_top,
        ctx_.ui_requests
    );

    const app::IniCell::Sample catalog = ini_.sample();
    if (catalog.generation != ini_label_gen_) {
        ini_label_gen_ = catalog.generation;
        ini_label_ = catalog ? catalog.value.index.label_for(catalog.value.active) : std::string{};
    }

    const os::RawMonotonicClock::time_point now = clock_.now();
    const app::IdleKnobsCell::Sample knobs = idle_knobs;
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

    tick_autosave_(now);

    if (hd_) {
        app::hd::PageDescription described{};
        const app::hd::PageDescription* page = nullptr;
        if (model_.hd_tree() && model_.describe_top(described))
            page = &described;
        const app::hd::HdStatusSlots slots{
            .bluetooth = ctx_.status.bluetooth,
            .rt = ctx_.sys.kernel == os::KernelRt::PreemptRt,
            .indicators = page ? board_.active() : app::IndicatorSet{}
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

    const proto::OsdTarget menu_target = (hd_ && hd_->intends_hdmi()) || terminal_top
        ? proto::OsdTarget::Vga
        : proto::OsdTarget::All;
    return app::UiRoundEdge{
        .pumped = !idle,
        .target = model_.stack().top_id() == PageId::Indicators ? board_.target() : menu_target,
        .menu_present = model_.menu_present()
    };
}

void UiRound::publish_edges_(const app::UiRoundEdge& edge) {
    const std::uint32_t pubs = vis_pubs_;
    const proto::OsdTarget target = edge.target;

    const bool owed = vis_unlatched_ || target != last_target_;
    const proto::LinkOp::OsdShow show = edge.pumped || vis_unlatched_ ? model_.osd_show()
                                                                      : last_show_;

    const bool awake = idle_clock_.idle() == app::hd::FrontEndIdle::Awake;
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
