// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/autosave_clock.h"

#include "proto/autosave_policy.h"

namespace mister::ui {

bool AutosaveClock::round(const Inputs& in, IndicatorBoard& board) noexcept {
    TASTY_SEAT_BODY(AutosaveClock);
    return post_(in, board);
}

bool AutosaveClock::post_(const Inputs& in, IndicatorBoard& board) noexcept {
    if (!in.status)
        return false;
    const svc::Tally saves = in.status->confirmed.sum(in.status->direct);
    const bool saved = !(saves == seen_saves_);
    const bool failed = !(in.status->fault_episodes == seen_faults_);
    seen_saves_ = saves;
    seen_faults_ = in.status->fault_episodes;
    if (!in.knobs.saved_toast)
        return false;
    bool posted = false;
    if (saved)
        posted = board.post(app::IndicatorId::Saved, *in.knobs.saved_toast);
    if (failed)
        posted = board.post(app::IndicatorId::SaveFailed, proto::kSaveFailedToast) || posted;
    return posted;
}

}
