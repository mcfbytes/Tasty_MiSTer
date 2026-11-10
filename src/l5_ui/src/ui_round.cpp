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
    ++button_levels_;
    osd_held_ = b.osd;

    if (user_held_ && !b.user)
        user_release_pending_ = true;
    user_held_ = b.user;
    return false;
}

bool UiRound::on(const proto::LinkEvent::OsdMask& m) noexcept {
    model_.on_hd_mask(HdMask{m.mask.v}, m.front_end, ctx_.ui_requests);
    return false;
}

bool UiRound::on(const proto::LinkEvent::PolledWord& w) {
    (void)info_.decode(w);
    return false;
}

bool UiRound::take_link(const proto::LinkEvent& ev) noexcept {
    TASTY_SEAT_BODY(UiRound);
    return infra::dispatch<app::LinkEventRoutes<app::EventSink::Ui>>(ev, *this);
}

bool UiRound::misrouted(const proto::LinkEvent&) noexcept {
    TASTY_SEAT_BODY(UiRound);
    ++link_event_misrouted_;
    return false;
}

static_assert(app::LinkEventUiSink<UiRound>);

bool UiRound::poll_buttons(bool link_work) {

    bool drained = link_work;

    if (const std::uint32_t edges = input_.core_edges(); edges != core_edges_seen_) {
        core_edges_seen_ = edges;
        osd_btn_ = ButtonMachine{};
        user_btn_ = ButtonMachine{};
        settling_ = true;
        osd_held_ = false;
        user_held_ = false;
        user_release_pending_ = false;
        ++button_core_resets_;
    }
    if (settling_) {
        if (osd_held_ || user_held_)
            return false;
        settling_ = false;
    }
    const std::chrono::nanoseconds now = clock_.now().time_since_epoch();

    {
        const bool but = osd_held_;
        ButtonMachine& m = osd_btn_;
        if (m.press_edge(but, now, kOsdLongPress))
            ++button_edges_;
        if (m.long_ready(but, now)) {
            ++button_longs_;
            input_.note_button_action();

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
            input_.note_button_action();
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
                input_.note_button_action();
            }
            const std::optional<std::chrono::nanoseconds> released = m.release_hold(but, now);
            if (released && !m.consumed()) {

                ++button_edges_;
                input_.note_button_action();
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

    const bool focus = show == proto::LinkOp::OsdShow::Menu;
    input_.update_osd_request(focus);
    osd_file_.on_visible(focus);
    return true;
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
    rec.last_hold_ms = static_cast<std::uint32_t>(last_hold_.count());
    const app::RecentGames& rg = model_.recent_games();
    rec.recent_build_us = us_of(rg.build_ns());
    rec.recent_update_us_max = us_of(rg.update_ns_max());

    rec.lane_refusals = writes_ ? writes_->refusals() : 0;
    rec.lane_failures = writes_ ? writes_->failures() : 0;
    rec.store_errors = ctx_.recents ? ctx_.recents->errors() : 0;
    page_cell_.publish(rec);
}

app::UiRoundEdge UiRound::compose_(bool link_work) {
    const PublishOnExit republish{this};
    ++ticks_;

    bool drained = false;
    for (unsigned i = 0; i < kKeyDrainBudget; ++i) {
        const auto e = input_.pop_ui_key();
        if (!e.has_value())
            break;
        proto::KeyCode code = e->code.wire_key();
        if (!e->pressed)
            code = code.with_release();
        decoder_.push_raw(code);
        ++keys_drained_;
        drained = true;
    }

    if (poll_buttons(link_work))
        drained = true;

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

    if (hd_) {
        app::PageDescription described{};
        const app::PageDescription* page = nullptr;
        if (model_.hd_tree() && model_.describe_top(described))
            page = &described;
        const app::HdStatusSlots slots{
            .bluetooth = ctx_.status.bluetooth,
            .rt = ctx_.sys.kernel == os::KernelRt::PreemptRt
        };
        hd_->tick(infra::opt_ref_of(page), model_.pause_badge(), slots, model_.style().look());
        model_.set_hd_root(hd_->available());
    }
    return app::UiRoundEdge{
        .pumped = !idle,
        .target = hd_ && hd_->intends_hdmi() ? proto::OsdTarget::Vga : proto::OsdTarget::All
    };
}

void UiRound::publish_edges_(const app::UiRoundEdge& edge) {
    const std::uint32_t pubs = vis_pubs_;
    const proto::OsdTarget target = edge.target;

    const bool owed = vis_unlatched_ || target != last_target_;
    const proto::LinkOp::OsdShow show = edge.pumped || vis_unlatched_ ? model_.osd_show()
                                                                      : last_show_;
    if (edge.pumped ? (show != last_show_ || target != last_target_) : owed) {

        if (last_show_ == proto::LinkOp::OsdShow::Menu && show != last_show_)
            decoder_.reset();
        if (publish_visibility(show, target)) {
            last_show_ = show;
            last_target_ = target;
            vis_unlatched_ = false;
        } else {
            vis_unlatched_ = true;
        }
    }
    if (vis_pubs_ != pubs)
        publish_page_record();
}

}
