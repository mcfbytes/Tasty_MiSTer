// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ps2_egress.h"

#include "app/link_tx_channel.h"
#include "proto/link_op.h"
#include "proto/ps2_frame.h"

namespace mister::app {

Ps2Egress::Flushed Ps2Egress::flush_ps2_frames_(
    bool muted,
    const svc::InputGates& gates,
    InputKeyCounts& keys
) {
    Flushed out{false, gates};
    if (muted) {

        while (kbd_.next_frame()) {
        }
        while (mouse_.next_frame()) {
        }
        return out;
    }

    while (inbox_.ring().size() < proto::kLinkTxCapacity) {
        const auto frame = kbd_.next_frame();
        if (!frame)
            break;
        if (!inbox_.push(proto::LinkOp::Ps2Frame{*frame})) {
            keys.keys_dropped.add();
            out.gates.keys_lost = true;
            return out;
        }
        out.framed = true;
        activity_.add();
    }
    while (inbox_.ring().size() < proto::kLinkTxCapacity) {
        const auto frame = mouse_.next_frame();
        if (!frame)
            break;
        if (!inbox_.push(proto::LinkOp::Ps2Frame{*frame}))
            return out;
        out.framed = true;
    }
    return out;
}

svc::InputGates Ps2Egress::sweep_dropped_keys_(
    const svc::InputGates& gates,
    InputKeyCounts& keys
) noexcept {
    svc::InputGates out = gates;
    if (const proto::FrameDrops drops = kbd_.overflow_drops(); drops != wm_frame_drops_) {
        keys.keys_dropped.add(drops.since(wm_frame_drops_));
        out.keys_lost = true;
        wm_frame_drops_ = drops;
        sweep_owed_ = true;
        diag_.appendf(
            "{\"t\":\"input_drop\",\"ring\":\"keys\",\"cap\":%u,"
            "\"dropped\":%s}",
            static_cast<unsigned>(proto::Ps2Keyboard::capacity()),
            keys.keys_dropped.text().c_str()
        );
    }

    if (!sweep_owed_ || kbd_.queued() >= proto::Ps2Keyboard::capacity())
        return out;
    sweep_owed_ = false;
    const unsigned released = kbd_.release_all();

    wm_frame_drops_ = kbd_.overflow_drops();
    keys.key_sweeps.add();
    keys.keys_released.add(svc::Tally{released});
    return out;
}

void Ps2Egress::note_dropped_mouse_() noexcept {
    const proto::FrameDrops drops = mouse_.overflow_drops();
    if (drops == wm_mouse_drops_)
        return;
    wm_mouse_drops_ = drops;
    diag_.appendf(
        "{\"t\":\"input_drop\",\"ring\":\"mouse\",\"cap\":%u,\"dropped\":%s}",
        static_cast<unsigned>(proto::Ps2Mouse::capacity()),
        drops.text().c_str()
    );
}

}
