// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/seat.h"
#include "app/input_emit.h"

namespace mister::app {

void InputEmit::on_rt_round(bool tick, CoreEdgeSeq core_edge_seq, bool live) {

    TASTY_SEAT_BODY(InputEmit);
    n_rt_rounds_.add(1);

    const InputGateBits gates = wire_.gates();
    em_.set_gates(
        svc::PadGates{
            .grabbed = gates.has(InputWire::kGateGrabbed),
            .osd_visible = gates.has(InputWire::kGateOsdVisible)
        }
    );

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
    if (reset_seen_.take(wire_.edge_reset())) {
        em_.joysticks().reset_edges();
        n_edge_reset_.add(1);
        counts_cell_.publish(counts());
    }
}

InputEmit::Counts InputEmit::counts() const noexcept {
    Counts c{};
    c.rt_rounds = n_rt_rounds_.get();
    c.rt_errors = ps2_.errors();
    c.edge_resets = n_edge_reset_.get();
    return c;
}

}
