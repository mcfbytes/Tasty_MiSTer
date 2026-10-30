// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/quiesce_ack.h"

namespace mister::app {

void QuiesceAck::poll(SwitchPark& sp) noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    if (parked())
        return;
    ParkBody b{.taken = seen_.taken(), .gen = gen_, .quiet = false};
    (void)sp.observe(seen_, b);

    const std::uint32_t g = seen_.taken();
    if (!engaged() && g != 0 && g != over_)
        gen_ = g;
}

void QuiesceAck::step(SwitchPark& sp, bool held_back) noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    if (!asked())
        return;

    ParkBody b{.taken = seen_.taken(), .gen = gen_, .quiet = !held_back};
    if (sp.observe(seen_, b) && seen_.taken() == gen_)
        acked_ = gen_;
}

void QuiesceAck::withdraw() noexcept {
    TASTY_SEAT_BODY(QuiesceAck);

    over_ = gen_;
}

void QuiesceAck::release() noexcept {
    TASTY_SEAT_BODY(QuiesceAck);
    over_ = gen_;
}

}
