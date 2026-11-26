// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rt_main.h"

#include "hal/disk_lamp.h"

#include "app/input/input_emit.h"
#include "app/osd_wire.h"
#include "app/delivered.h"
#include "app/link_session.h"
#include "app/link_op_facts.h"
#include "app/core_frame_counter.h"
#include "app/disk_relays.h"
#include "app/replay/replay_gate.h"
#include "app/round_budget.h"
#include "app/save_upload_relay.h"
#include "app/launcher/scanout_relay.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "proto/link/spi_fio_queue.h"
#include "reactor/executive.h"
#include "reactor/round_timer.h"

namespace mister::app {

static_assert(
    hal::DiskLamp::kTick == std::chrono::nanoseconds{reactor::kTickNs},
    "DiskLamp::kTick must be the executive's tick"
);

void RtMain::start() noexcept {
    TASTY_SEAT_BODY(RtMain);
    stopping_ = false;
    exec_.enter();
    result_ = {};

    park_.wait(park_ms());
    loop_();
    result_ = exec_.fault();
}

void RtMain::stop() noexcept {
    exec_.stop();
}

Ex<void> RtMain::result() const noexcept {
    TASTY_SEAT_BODY(RtMain);
    return result_;
}

void RtMain::serve() noexcept {
    const bool tick = exec_.dispatch_ready();

    if (exec_.take_stop()) {
        stopping_ = true;
        return;
    }

    RoundBudget budget{round_budget_words_};
    round(tick);

    if (transfer_holds_()) {
        exec_.end_round(false);
        return;
    }
    exec_.service_round();
    if (wire_held_()) {
        exec_.end_round(false);
        return;
    }

    const bool drained = drain_fio_(budget);
    const bool wrote = !drained
        && exec_.round_unclaimed()
        && osd_
        && !switch_window_()
        && osd_->flush_one();
    exec_.end_round(wrote);
}

void RtMain::settle() noexcept {
    exec_.settle();
}

bool RtMain::drain_fio_(RoundBudget& budget) noexcept {

    if (session_) {
        budget.debit(RoundAct::Disk, session_->disks().take_round_words());
        budget.debit(RoundAct::Mailbox, session_->mailbox().take_round_words());
    }

    if (replay_)
        budget.debit(RoundAct::Replay, replay_->take_round_words());
    if (frames_)
        budget.debit(RoundAct::Frames, frames_->take_round_words());
    if (scanout_)
        budget.debit(RoundAct::Scanout, scanout_->take_round_words());
    if (!session_)
        return false;

    budget.debit(RoundAct::Saves, session_->saves().take_round_words());
    proto::link::SpiFioQueue& q = session_->binder().fio_queue();

    const bool parked = session_->park().engaged();
    if (parked && !session_->saves().holds_fio()) {
        q.abandon();
        return false;
    }

    session_->service_saves(budget.remaining());
    budget.debit(RoundAct::Saves, session_->saves().take_round_words());
    if (parked)
        return false;
    return q.drain(budget.remaining()) != proto::FioWordCount{};
}

void RtMain::round(bool tick) {
    TASTY_SEAT_BODY(RtMain);
    note_switch_();

    if (input_)
        input_->apply_edge_reset();

    if (session_) {
        const LinkSession::RoundEntry entry = session_->poll(tick);
        drain_ops();
        session_->tick(entry);
        if (wire_held_()) {
            note_switch_();
            return;
        }
    }

    if (session_)
        session_->reap_storage();

    replay_step_();

    if (input_)
        input_->relay(replay_ && replay_->armed());

    frames_step_();

    scanout_step_();

    if (lamp_)
        lamp_->on_round(tick, *disk_activity_);

    if (input_)
        input_->on_rt_round(tick, core_edge_seq_(), session_live_());

    if (tick && session_)
        session_->poll_osd_mask();
    note_switch_();
}

void RtMain::drain_ops() noexcept {
    if (!session_)
        return;

    const bool save_window = session_->saves().holds_fio();

    bool closed = session_->park().engaged();
    OpRoundBudget link_ops{kLinkOpBudget};
    if (!save_window) {
        drain_(session_->link_inbox(), link_ops, closed);
        closed = closed || session_->park().engaged();
        drain_(session_->ui_inbox(), link_ops, closed);
    }

    if (input_kick_)
        input_kick_->consume();
    OpRoundBudget input_ops{kInputOpBudget};
    drain_(session_->input_inbox(), input_ops, closed);
}

template <SeatTag Producer>
void RtMain::drain_(
    LinkTxChannel<Producer>& inbox,
    OpRoundBudget& budget,
    bool windows_closed
) noexcept {
    constexpr bool pads = Producer == SeatTag::Input;

    constexpr bool pieces = Producer == SeatTag::Main;
    while (!budget.exhausted() && (pieces || !wire_held_())) {
        const bool dat_held = pieces && dat_window_held_();
        const bool admits = dat_held || (pieces && off_link_sole_hold_());
        if (pieces && wire_held_() && !admits)
            break;
        const auto op = inbox.pop();
        if (!op) {

            if (pieces && session_)
                session_->held().release_window();
            break;
        }
        budget.take();

        if (dat_held && dat_hold_for(infra::kind_of(*op)) == DatHold::Release) {
            session_->held().release_window();
        }

        if (pads && replay_ && replay_->suppresses(*op))
            continue;

        if (pads && session_->drops_live_pad(*op))
            continue;
        const std::uint32_t resets = session_->tally().resets_pulsed;
        const Delivered d = session_->deliver(*op, inbox, windows_closed);

        if (timer_ && round_class_for(infra::kind_of(*op)) == RoundClass::Lifecycle) {
            timer_->note_lifecycle(reactor::RoundLifecycle::Rung);
        }

        if (replay_ && d.edge == Delivered::Edge::DownloadClosed) {
            if (const auto end = session_->last_download())
                replay_->note_download_end(*end);
        }

        if (replay_ && session_->tally().resets_pulsed != resets)
            replay_->note_core_reset();

        if (d.round_spent && round_cost_for(infra::kind_of(*op)) == RoundCost::Payload)
            break;
    }
}

void RtMain::scanout_step_() noexcept {
    if (!scanout_)
        return;

    using Wire = launcher::ScanoutRelay::Wire;
    if (!scanout_->collect()) {
        scanout_->step(Wire::Held);
        return;
    }
    if (session_ && session_->session_starting())
        scanout_->step(Wire::CoreStarting);
    else if (
        wire_held_()
        || (session_
            && (session_->park().engaged()
                || !session_->ui_inbox().drained()
                || !session_->prelude_settled()))
    )
        scanout_->step(Wire::Held);
    else
        scanout_->step(Wire::Ready);
}

bool RtMain::replay_ready_() const noexcept {
    return session_live_() && (!session_ || !session_->park().engaged());
}

void RtMain::replay_step_() noexcept {
    if (!replay_)
        return;
    const proto::LateAnswers late = session_ ? session_->take_late_answers() : proto::LateAnswers{};
    replay_->tick(
        replay_ready_(),
        core_edge_seq_(),
        late,
        static_cast<std::uint32_t>(replay_ring_.size())
    );
    for (unsigned n = 0; n < replay::ReplayGate::kReplayBudget && replay_->wants_record(); ++n) {
        const auto m = replay_ring_.pop();
        if (!m)
            break;
        infra::dispatch<infra::AllRouted>(*m, *replay_);
    }
    replay_->settle();
}

void RtMain::frames_step_() noexcept {
    if (!frames_)
        return;
    MoviePosition movie = kNoMovieFrame;
    if (replay_ && replay_->armed())
        movie = replay_->status().movie_frame.value_or(kNoMovieFrame);
    frames_->settle(replay_ready_(), core_edge_seq_(), movie);
}

Ex<void> RtMain::pump_boot() {

    if (!session_ || exec_.run_entered()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0u});
    }
    LinkSession& s = *session_;
    if (!s.session_starting()) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(s.state())}
        );
    }
    while (s.session_starting()) {

        if (const infra::OptRef<IStopSignal> stop = s.board_ops().stop;
            stop && stop->stop_requested()) {
            return std::unexpected(
                Error{Errc::cancelled, ERR_SITE(), static_cast<std::uint32_t>(s.state())}
            );
        }

        s.settle_frames();
        drain_ops();

        s.disks().pump_on_caller();
        s.settle_boot_slot_mount();

        if (s.wire_hold() != LinkSession::WireHold::Frames)
            s.binder().service_on_caller();
        if (s.session_starting()) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(s.state())}
            );
        }
    }
    return {};
}

bool RtMain::switch_window_() const noexcept {
    return session_ && (session_->session_starting() || session_->park().engaged());
}

bool RtMain::wire_held_() const noexcept {
    return session_ && session_->wire_held();
}

bool RtMain::dat_window_held_() const noexcept {
    return session_ && session_->held().dat_window_held();
}

bool RtMain::off_link_sole_hold_() const noexcept {
    return session_ && session_->held().off_link_sole_hold();
}

bool RtMain::transfer_holds_() const noexcept {
    if (!session_)
        return false;
    const LinkSession::WireHold hold = session_->wire_hold();
    return hold == LinkSession::WireHold::Transfer || hold == LinkSession::WireHold::Frames;
}

bool RtMain::session_live_() const noexcept {
    return !session_ || session_->liveness().session_live();
}

CoreEdgeSeq RtMain::core_edge_seq_() const noexcept {
    return session_ ? session_->core_edge_seq() : CoreEdgeSeq{};
}

void RtMain::note_switch_() const noexcept {
    if (timer_ && switch_window_())
        timer_->note_lifecycle(reactor::RoundLifecycle::Switch);
}

}
