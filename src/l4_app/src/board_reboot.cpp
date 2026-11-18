// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/board_reboot.h"
#include "app/owner_push.h"

#include <unistd.h>

#include <chrono>

#include "app/save_stage_director.h"
#include "hal/boot_handoff.h"

namespace mister::app {

Ex<void> BoardReboot::reboot(bool cold) {
    TASTY_SEAT_BODY(BoardReboot);
    ++reboots_;
    ::sync();
    sleep_ms(sleeper_, std::chrono::milliseconds{kSettleMs});

    if (handoff_)
        (void)handoff_->write_reboot_flag(!cold);
    if (!reset_)
        return std::unexpected(Error{Errc::bridge_state, ERR_SITE(), 0});
    reset_->board_reset();
    return {};
}

bool BoardReboot::order_refusable_(const proto::LinkOp::RebootNow& op) noexcept {
    TASTY_SEAT_BODY(BoardReboot);
    return push_refusing(inbox_, owner_tally_, op);
}

bool BoardReboot::repush_(const proto::LinkOp::RebootNow& op) noexcept {
    TASTY_SEAT_BODY(BoardReboot);
    return inbox_.push(op);
}

void BoardReboot::settle(bool cold) {
    TASTY_SEAT_BODY(BoardReboot);

    if (owed_)
        return;
    const proto::RebootSeq seq = seq_.next();
    seq_ = seq;
    const proto::LinkOp::RebootNow quiesce{.cold = cold, .quiesce = true, .seq = seq};
    owed_ = OwedReboot{.cold = cold, .ordered = order_refusable_(quiesce), .seq = seq};
    due_ = os::Deadline::in(clock_, std::chrono::nanoseconds{kDrainBoundNs});
}

void BoardReboot::quiesced() {
    TASTY_SEAT_BODY(BoardReboot);
    if (!owed_ || owed_->commit)
        return;
    commit_or_finish_();
}

void BoardReboot::commit_or_finish_() {
    if (!director_)
        return finish();
    commit_seq_ = commit_seq_.next();
    owed_->commit = CommitAllOrder{
        .seq = commit_seq_,
        .ordered = director_->order_commit_all(commit_seq_)
    };
    due_ = os::Deadline::in(clock_, proto::kRebootCommitBound);
}

bool BoardReboot::watch() {
    TASTY_SEAT_BODY(BoardReboot);
    if (!owed_)
        return false;
    if (auto& commit = owed_->commit) {
        if (!commit->ordered)
            commit->ordered = director_->order_commit_all(commit->seq);
        if (commit->ordered && director_->commit_all_served(commit->seq)) {
            finish();
            return false;
        }
        if (!due_.expired(clock_))
            return true;
        tally_.reboot_commit_expiries.add();
        finish();
        return false;
    }
    if (!owed_->ordered) {

        owed_->ordered = repush_(
            proto::LinkOp::RebootNow{.cold = owed_->cold, .quiesce = true, .seq = owed_->seq}
        );
    }
    if (!due_.expired(clock_))
        return true;

    ++tally_.reboot_drain_expiries;
    commit_or_finish_();
    return owed_.has_value();
}

void BoardReboot::finish() {
    TASTY_SEAT_BODY(BoardReboot);
    const bool cold = owed_ ? owed_->cold : true;
    owed_.reset();
    ++tally_.reboots_settled;
    (void)reboot(cold);

    (void)order_refusable_(proto::LinkOp::RebootNow{.cold = cold, .quiesce = false});
    failed_ = true;
}

}
