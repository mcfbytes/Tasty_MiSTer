// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input/input_emit.h"

#include <algorithm>

#include "os/timespec.h"

namespace mister::app::input {

void InputEmit::on_rt_round(bool tick, CoreEdgeSeq core_edge_seq, bool live) {

    TASTY_SEAT_BODY(InputEmit);
    n_rt_rounds_.add(1);

    if (core_edge_seq != wm_core_edge_seq_) {
        wm_core_edge_seq_ = core_edge_seq;
        ps2_.forget();
    }

    if (live) {
        if (tick)
            ps2_.probe();
        ps2_.service();
    }
    counts_cell_.publish(counts());
}

void InputEmit::apply_edge_reset() {
    TASTY_SEAT_BODY(InputEmit);
    if (reset_seen_.take(edge_reset_)) {
        em_.joysticks().reset_edges();
        n_edge_reset_.add(1);
        counts_cell_.publish(counts());
    }
}

void InputEmit::relay(bool gated) noexcept {
    TASTY_SEAT_BODY(InputEmit);
    em_.gate_pads(gated);
    answer_relay_();
    const std::optional<proto::link::PadRelay> moved = em_.take_relay();
    if (!moved || !relay_ends_.input_rx)
        return;

    const os::MonoInstant at = os::steady_now();
    relay_cell_.publish(*moved);
    relay_tally_.publishes.add();
    if (relay_owed_)
        relay_tally_.superseded.add();
    const bool bound = relay_ends_.route && relay_ends_.route->sample().value.bound;
    if (bound) {

        const bool held_gated = relay_tally_.input_gated && (!relay_owed_ || relay_owed_->gated);
        relay_tally_.input_gated = held_gated && moved->gated;
        relay_owed_ = RelayOwed{.seq = moved->seq, .gated = moved->gated, .at = at};
    } else {

        relay_tally_.unbound.add();
        relay_tally_.input_gated = moved->gated;
        relay_owed_.reset();
    }

    relay_ends_.input_rx->request_wake();
}

void InputEmit::answer_relay_() noexcept {
    if (!relay_owed_ || !relay_ends_.route)
        return;
    const auto r = relay_ends_.route->sample();
    if (!r)
        return;
    if (r.value.relay_seq == relay_owed_->seq) {
        relay_tally_.acked.add();
        relay_tally_.lag_last = us_between(relay_owed_->at, r.value.relay_read.at);
        relay_tally_.lag_max = std::max(relay_tally_.lag_max, relay_tally_.lag_last);
    } else if (r.value.bound) {
        return;
    }

    relay_tally_.input_gated = relay_owed_->gated;
    relay_owed_.reset();
}

InputEmit::Counts InputEmit::counts() const noexcept {
    Counts c{};
    c.rt_rounds = n_rt_rounds_.get();
    c.rt_errors = ps2_.errors();
    c.edge_resets = n_edge_reset_.get();
    return c;
}

}
