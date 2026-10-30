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

        rebooting_ = true;
        reboot_answered_ = false;
        reset_held_ = false;
        reboot_seq_ = a.seq;
        hold_reset_once_saved_();

        return After::EndStartAndFail;
    }
    return perform_reboot_();
}

void RebootAnswer::answer_reboot(Host& host) noexcept {
    TASTY_SEAT_BODY(RebootAnswer);
    if (!rebooting_ || reboot_answered_)
        return;
    hold_reset_once_saved_();
    if (!reset_held_ || !io_settled_())
        return;

    reboot_answered_ = host.reboot_push_quiesced(reboot_seq_);
}

void RebootAnswer::hold_reset_once_saved_() {
    if (reset_held_ || !nvram_.saves_drained())
        return;
    signals_.set_core_reset(true);
    reset_held_ = true;
}

bool RebootAnswer::io_settled_() const noexcept {
    return (!storage_life_ || storage_life_->quiesced())
        && nvram_.writes_settled()
        && nvram_.saves_drained()
        && (!window_jobs_ || !window_jobs_->busy());
}

RebootAnswer::After RebootAnswer::perform_reboot_() const noexcept {
    if (!rebooting_) {
        return After::CountStale;
    }

    return After::ReleaseBus;
}

}
