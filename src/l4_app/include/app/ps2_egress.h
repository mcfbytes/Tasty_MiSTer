// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input_key_counts.h"
#include "infra/counter.h"
#include "infra/diag_log.h"
#include "infra/seat.h"
#include "proto/frame_drops.h"
#include "proto/ps2_keyboard.h"
#include "proto/ps2_mouse.h"
#include "svc/input_gates.h"

namespace mister::app {
template <SeatTag Seat>
class LinkTxChannel;

class Ps2Egress {
    TASTY_SEAT_RESIDENT(Input);

public:
    struct Flushed {
        bool framed = false;
        svc::InputGates gates{};
    };

    Ps2Egress(
        proto::Ps2Keyboard& kbd,
        proto::Ps2Mouse& mouse,
        LinkTxChannel<SeatTag::Input>& inbox,
        xthread::Counter& activity,
        xthread::DiagLog& diag
    ) noexcept
        : kbd_(kbd),
          mouse_(mouse),
          inbox_(inbox),
          activity_(activity),
          diag_(diag) {}

    Ps2Egress(const Ps2Egress&) = delete;
    Ps2Egress& operator=(const Ps2Egress&) = delete;

    [[nodiscard]] Flushed flush_ps2_frames_(
        bool muted,
        const svc::InputGates& gates,
        InputKeyCounts& keys
    );

    [[nodiscard]] svc::InputGates sweep_dropped_keys_(
        const svc::InputGates& gates,
        InputKeyCounts& keys
    ) noexcept;

    void note_dropped_mouse_() noexcept;

private:
    proto::Ps2Keyboard& kbd_;
    proto::Ps2Mouse& mouse_;
    LinkTxChannel<SeatTag::Input>& inbox_;
    xthread::Counter& activity_;
    xthread::DiagLog& diag_;
    proto::FrameDrops wm_frame_drops_{};
    proto::FrameDrops wm_mouse_drops_{};
    bool sweep_owed_ = false;
};

}
