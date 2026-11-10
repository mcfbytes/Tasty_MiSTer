// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/reboot_answer.h"

#include "app/fenced_core_signals.h"
#include "app/nvram_upload.h"
#include "app/window_job_service.h"
#include "svc/storage_lifecycle.h"

namespace mister::app {

RebootAnswer::After RebootAnswer::on_reboot_now(const proto::LinkOp::RebootNow& a) {
    TASTY_SEAT_BODY(RebootAnswer);
    if (a.quiesce) {

        quiesce_ = RebootState{RebootPhase::Quiescing, a.seq};
        hold_reset_once_saved_();

        return After::EndStartAndFail;
    }
    return perform_reboot_();
}

void RebootAnswer::answer_reboot(Host& host) noexcept {
    TASTY_SEAT_BODY(RebootAnswer);
    if (quiesce_.phase == RebootPhase::Idle || quiesce_.phase == RebootPhase::Answered)
        return;
    hold_reset_once_saved_();
    if (quiesce_.phase != RebootPhase::ResetHeld || !io_settled_())
        return;

    if (host.reboot_push_quiesced(quiesce_.seq))
        quiesce_.phase = RebootPhase::Answered;
}

void RebootAnswer::hold_reset_once_saved_() {
    if (quiesce_.phase != RebootPhase::Quiescing || !nvram_.saves_drained())
        return;
    signals_.set_core_reset(true);
    quiesce_.phase = RebootPhase::ResetHeld;
}

bool RebootAnswer::io_settled_() const noexcept {
    return (!storage_life_ || storage_life_->quiesced())
        && nvram_.saves_drained()
        && (!window_jobs_ || !window_jobs_->busy());
}

RebootAnswer::After RebootAnswer::perform_reboot_() const noexcept {
    if (quiesce_.phase == RebootPhase::Idle) {
        return After::CountStale;
    }

    return After::ReleaseBus;
}

}
