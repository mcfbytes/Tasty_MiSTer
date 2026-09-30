// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/quiesce_ack.h"

namespace mister::app {

void QuiesceAck::poll(QuiesceChannel& ch) noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    if (asked_ || parked_) return;
    const auto q = ch.take_ask();
    if (!q) return;
    asked_ = true;
    gen_ = q->gen;
}

void QuiesceAck::step(QuiesceChannel& ch, bool held_back) noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    if (!asked_) return;

    if (held_back) return;
    if (!ch.post_ack(Quiesced{gen_})) {
        ++drops_;
        return;
    }
    asked_ = false;
    parked_ = true;
}

void QuiesceAck::withdraw() noexcept {
    TASTY_SEAT_BODY(QuiesceAck);

    asked_ = false;
    parked_ = false;
}

void QuiesceAck::release() noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    parked_ = false;
}

}  // namespace mister::app
