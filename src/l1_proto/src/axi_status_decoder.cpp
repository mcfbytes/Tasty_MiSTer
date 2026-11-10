// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/axi_status_decoder.h"

namespace mister::proto {

void AxiStatusDecoder::bind(
    infra::OptRef<const StatusSnapshotCell> src,
    std::uint16_t gen
) noexcept {
    TASTY_SEAT_BODY(AxiStatusDecoder);
    src_ = src;
    gen_ = src ? gen : std::uint16_t{0};

    reader_.forget();
}

void AxiStatusDecoder::service() noexcept {
    TASTY_SEAT_BODY(AxiStatusDecoder);

    if (!session_.flush_owed()) {
        ++flush_refusals_;
        return;
    }
    if (!src_ || gen_ == 0 || !reader_.take_if_changed(*src_, scratch_))
        return;
    if (scratch_.gen != gen_) {
        ++stale_;
        return;
    }
    last_ = scratch_.stflg.flag;
    if (!session_.adopt_from_core(scratch_.words))
        ++flush_refusals_;
    ++adopted_;
    published_ = session_.status().value();
    cell_.publish(published_);
}

}
