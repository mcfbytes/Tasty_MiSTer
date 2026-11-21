// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/axi_status_decoder.h"

namespace mister::proto::link {

void AxiStatusSource::bind(
    infra::OptRef<const StatusSnapshotCell> src,
    BuildGeneration gen
) noexcept {
    TASTY_SEAT_BODY(AxiStatusSource);
    src_ = src;
    gen_ = src ? gen : BuildGeneration{};

    reader_.forget();
}

std::optional<StatusChange> AxiStatusSource::take(StatusChange, StatusWord& words) noexcept {
    TASTY_SEAT_BODY(AxiStatusSource);
    if (!src_ || !gen_.built() || !reader_.take_if_changed(*src_, scratch_))
        return std::nullopt;
    if (scratch_.gen != gen_) {
        ++stale_;
        return std::nullopt;
    }
    words = scratch_.words;
    ++adopted_;
    return StatusChange::of(scratch_.stflg);
}

}
