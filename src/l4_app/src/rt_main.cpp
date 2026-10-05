// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/rt_main.h"

#include <algorithm>

#include "app/input_emit.h"
#include "app/input_wire.h"
#include "app/osd_wire.h"
#include "app/link_session.h"
#include "app/core_frame_counter.h"
#include "app/replay_gate.h"
#include "app/scanout_relay.h"
#include "infra/message_sum.h"
#include "proto/spi_fio_queue.h"
#include "reactor/executive.h"
#include "reactor/round_timer.h"

namespace mister::app {

static_assert(
    ReplayGate::kMaxRoundWords + CoreFrameCounter::kMaxRoundWords + ScanoutRelay::kMaxRoundWords <=
        proto::tightest_round_budget_words(),
    "the replay's, the frame reader's and the scanout relay's words together fit the round budget");

void RtMain::start() noexcept {
    TASTY_SEAT_BODY(RtMain);
    stopping_ = false;
    result_ = exec_.enter();
    if (!result_) return;

    park_.wait(park_ms());
    loop_();
    result_ = exec_.fault();
}

void RtMain::stop() noexcept { exec_.stop(); }

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
    round(tick);
    if (wire_held_()) {
        exec_.end_round(false);
        return;
    }
    exec_.service_round();

    const bool drained = drain_fio_();
    const bool wrote = !drained && exec_.osd_budget_open() && osd_ != nullptr &&
                       !switch_window_() && osd_->flush_one();
    exec_.end_round(wrote);
}

void RtMain::settle() noexcept { exec_.settle(); }

bool RtMain::drain_fio_() noexcept {

    std::uint32_t spent = replay_ != nullptr ? replay_->take_round_words() : 0u;
    if (frames_ != nullptr) spent += frames_->take_round_words();
    if (scanout_ != nullptr) spent += scanout_->take_round_words();
    if (session_ == nullptr) return false;
    proto::SpiFioQueue& q = session_->binder().fio_queue();
    if (session_->park().engaged()) {
        q.abandon();
        return false;
    }
    return q.drain(round_budget_words_ - std::min(spent, round_budget_words_)) != 0;
}

void RtMain::round(bool tick) {
    TASTY_SEAT_BODY(RtMain);
    note_switch_();

    if (input_ != nullptr) input_->apply_edge_reset();

    if (session_ != nullptr) {
        const LinkSession::RoundEntry entry = session_->poll(tick);
        drain_ops();
        session_->tick(entry);
        if (wire_held_()) {
            note_switch_();
            return;
        }
    }

    if (session_ != nullptr) session_->reap_storage();

    replay_step_();

    frames_step_();

    scanout_step_();

    if (input_ != nullptr) input_->on_rt_round(tick, core_edge_seq_(), session_live_());

    if (tick && session_ != nullptr) session_->poll_osd_mask();

    if (tick && session_ != nullptr) session_->publish_doorbell_stats();
    note_switch_();
}

void RtMain::drain_ops() noexcept {
    if (session_ == nullptr) return;

    bool closed = session_->park().engaged();
    unsigned n = 0;
    n += drain_(session_->link_inbox(), n, kLinkOpBudget, closed);
    closed = closed || session_->park().engaged();
    n += drain_(session_->ui_inbox(), n, kLinkOpBudget, closed);

    if (wire_ != nullptr) wire_->consume_kick();
    (void)drain_(session_->input_inbox(), 0, kInputOpBudget, closed);
}

unsigned RtMain::drain_(LinkTxChannel& inbox, unsigned already, unsigned budget,
                        bool windows_closed) noexcept {
    const bool pads = &inbox == &session_->input_inbox();
    unsigned got = 0;
    while (already + got < budget && !wire_held_()) {
        const auto op = inbox.pop();
        if (!op) break;
        ++got;

        if (pads && replay_ != nullptr && replay_->suppresses(*op)) continue;
        const std::uint32_t ends = session_->downloads_closed();
        const std::uint32_t resets = session_->resets_pulsed();
        session_->deliver(*op, &inbox, windows_closed);

        if (replay_ != nullptr && session_->downloads_closed() != ends) {
            replay_->note_download_end(session_->last_download_index());
        }

        if (replay_ != nullptr && session_->resets_pulsed() != resets) replay_->note_core_reset();
        const auto ftx = infra::as<proto::LinkOp::FileTx>(*op);

        if (ftx && ftx->phase != proto::LinkOp::FileTxPhase::Whole) break;
        const auto sp = infra::as<proto::LinkOp::StagePayload>(*op);
        if (sp && sp->payload != proto::FileId{} && sp->copy_word == 0) break;
    }
    return got;
}

void RtMain::scanout_step_() noexcept {
    if (scanout_ == nullptr) return;

    using Wire = ScanoutRelay::Wire;
    if (!scanout_->collect()) {
        scanout_->step(Wire::Held);
        return;
    }
    if (session_ != nullptr && session_->session_starting())
        scanout_->step(Wire::CoreStarting);
    else if (wire_held_() || (session_ != nullptr &&
                              (session_->park().engaged() || !session_->ui_inbox().drained())))
        scanout_->step(Wire::Held);
    else
        scanout_->step(Wire::Ready);
}

bool RtMain::replay_ready_() const noexcept {
    return session_live_() && (session_ == nullptr || !session_->park().engaged());
}

void RtMain::replay_step_() noexcept {
    if (replay_ == nullptr) return;
    const proto::LateAnswers late =
        session_ != nullptr ? session_->take_late_answers() : proto::LateAnswers{};
    replay_->tick(replay_ready_(), core_edge_seq_(), late,
                  static_cast<std::uint32_t>(replay_ring_->size()));
    for (unsigned n = 0; n < ReplayGate::kReplayBudget && replay_->wants_record(); ++n) {
        const auto m = replay_ring_->pop();
        if (!m) break;
        infra::dispatch<infra::AllRouted>(*m, *replay_);
    }
    replay_->settle();
}

void RtMain::frames_step_() noexcept {
    if (frames_ == nullptr) return;
    const std::int32_t movie =
        replay_ != nullptr && replay_->armed() ? replay_->status().movie_frame : -1;
    frames_->settle(replay_ready_(), core_edge_seq_(), movie);
}

Ex<void> RtMain::pump_boot() {

    if (session_ == nullptr || exec_.run_entered()) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0u});
    }
    LinkSession& s = *session_;
    if (!s.session_starting()) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(s.state())});
    }
    while (s.session_starting()) {

        if (const IStopSignal* stop = s.board_ops().stop;
            stop != nullptr && stop->stop_requested()) {
            return std::unexpected(
                Error{Errc::cancelled, ERR_SITE(), static_cast<std::uint32_t>(s.state())});
        }
        drain_ops();

        s.streams().pump_on_caller();
        s.binder().service_on_caller(s.clock().now().count());
        if (s.session_starting()) {
            return std::unexpected(
                Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(s.state())});
        }
    }
    return {};
}

bool RtMain::switch_window_() const noexcept {
    return session_ != nullptr && (session_->session_starting() || session_->park().engaged());
}

bool RtMain::wire_held_() const noexcept { return session_ != nullptr && session_->wire_held(); }

bool RtMain::session_live_() const noexcept {
    return session_ == nullptr || session_->liveness().session_live();
}

std::uint32_t RtMain::core_edge_seq_() const noexcept {
    return session_ != nullptr ? session_->core_edge_seq() : 0u;
}

void RtMain::note_switch_() const noexcept {
    if (timer_ != nullptr && switch_window_())
        timer_->note_lifecycle(reactor::RoundLifecycle::Switch);
}

}  // namespace mister::app
