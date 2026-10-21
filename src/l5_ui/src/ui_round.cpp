// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/ui_round.h"

#include "app/hd_osd_screen.h"
#include "app/page_description.h"
#include "app/link_event_dispatch.h"
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
    const std::int64_t now = clock_.now().count();

    {
        const bool but = osd_held_;
        ButtonMachine& m = osd_btn_;
        if (but && !m.last) {
            m.long_at_ns = now + kOsdLongPressNs;
            m.pressed_at_ns = now;
            ++button_edges_;
        }
        if (but && !m.consumed && now >= m.long_at_ns) {
            m.consumed = true;
            ++button_longs_;
            input_.note_button_action();

            if (model_.stack().top_id() == PageId::ScriptRun) {
                decoder_.push_synth(proto::KeyCode{KEY_BACKSPACE | KeyDecoder::kUpstroke});
                ++button_injects_;
                drained = true;
            } else if (model_.global_hotkeys_allowed()) {
                model_.open_bt_pairing(ctx_);
            } else {
                ++bt_route_refusals_;
            }
        }
        if (!but && m.last) {

            last_hold_ms_ = static_cast<std::uint32_t>((now - m.pressed_at_ns) / 1'000'000);
        }
        if (!but && m.last && !m.consumed) {
            const std::int64_t held_ns = now - m.pressed_at_ns;

            const bool wants_classic = held_ns >= kOsdClassicHoldNs
                && held_ns < kOsdLongPressNs
                && model_.hd_root()
                && !model_.osd_open();
            if (wants_classic && model_.open_classic_root(ctx_)) {
                ++button_classic_opens_;
            } else {
                decoder_.push_synth(proto::KeyCode{KEY_F12 | KeyDecoder::kUpstroke});
                ++button_injects_;
            }
            input_.note_button_action();
            drained = true;
        }
        if (!but)
            m.consumed = false;
        m.last = but;
    }

    {
        const bool but = user_held_;
        ButtonMachine& m = user_btn_;
        if (model_.menu_has_focus()) {
            if (but && !m.last) {
                m.long_at_ns = now + kUserLongPressNs;
                ++button_edges_;
            }
            if (but && !m.consumed && now >= m.long_at_ns) {

                m.consumed = true;
                ++button_longs_;
                input_.note_button_action();
            }
            if (!but && m.last && !m.consumed) {

                ++button_edges_;
                input_.note_button_action();
            }
        } else if (user_release_pending_ || (!but && m.last)) {

            ask_user_reset_();
        }
        user_release_pending_ = false;
        if (!but)
            m.consumed = false;
        m.last = but;
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
    rec.last_hold_ms = last_hold_ms_;
    page_cell_.publish(rec);
}

void UiRound::tick(bool link_work) {
    const PublishOnExit republish{this};
    ++ticks_;

    bool drained = false;
    for (unsigned i = 0; i < kKeyDrainBudget; ++i) {
        const auto e = input_.pop_ui_key();
        if (!e.has_value())
            break;
        std::uint32_t code = e->code;
        if (e->pressed == 0)
            code |= KeyDecoder::kUpstroke;
        decoder_.push_raw(proto::KeyCode{code});
        ++keys_drained_;
        drained = true;
    }

    if (poll_buttons(link_work))
        drained = true;

    const bool deadline_due = model_.deadline_due(clock_.now().count());
    const Page* top = model_.stack().top();
    const bool page_poll = top != nullptr && top->wake_every_round();

    const bool hd_switchable = hd_ && hd_->available();
    if (hd_switchable != ctx_.sys.hd_switchable)
        model_.request_repaint();
    const bool idle = !drained
        && !page_poll
        && !decoder_.pending()
        && !model_.repaint_pending()
        && !model_.cells_pending()
        && !deadline_due;
    if (!idle) {
        ++pumps_;

        sampler_.sample(ctx_.sys, ctx_.status);
        ctx_.sys.hd_switchable = hd_switchable;
        ++samples_;

        (void)model_.pump(ctx_, renderer_);
    }

    if (hd_) {
        app::PageDescription described{};
        const app::PageDescription* page = nullptr;
        if (model_.hd_tree() && model_.describe_top(described))
            page = &described;
        hd_->tick(page, model_.pause_badge());
        model_.set_hd_root(hd_->available());
    }
    const proto::OsdTarget target = hd_ && hd_->intends_hdmi() ? proto::OsdTarget::Vga
                                                               : proto::OsdTarget::All;
    if (idle) {

        if (vis_unlatched_ || target != last_target_) {
            const proto::LinkOp::OsdShow show = vis_unlatched_ ? model_.osd_show() : last_show_;
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
        return;
    }

    const proto::LinkOp::OsdShow show = model_.osd_show();
    if (show != last_show_ || target != last_target_) {

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
}

}
