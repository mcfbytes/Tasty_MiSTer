// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/input/pad_egress.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/counter.h"
#include "infra/xthread/wake_flag.h"
#include "proto/stick_pos.h"
#include "svc/input/input_service.h"
#include "svc/input/stick_index.h"
#include "svc/input_gates.h"

namespace mister::app::input {

class StickLevelEmitter {
    TASTY_SEAT_RESIDENT(Input);

public:
    StickLevelEmitter(
        const xthread::WakeFlag& edge_reset,
        xthread::Counter& activity,
        svc::input::InputService& svc,
        PadEgress& pads
    ) noexcept
        : edge_reset_(edge_reset),
          activity_(activity),
          svc_(svc),
          pads_(pads) {
        forget_(proto::StickPos{});
    }

    StickLevelEmitter(const StickLevelEmitter&) = delete;
    StickLevelEmitter& operator=(const StickLevelEmitter&) = delete;

    void publish(bool muted, svc::InputGates gates);

private:
    const xthread::WakeFlag& edge_reset_;
    xthread::Counter& activity_;
    svc::input::InputService& svc_;
    PadEgress& pads_;

    struct PlayerSticks {
        infra::IndexedArray<
            std::optional<proto::StickPos>,
            svc::input::kSticks.size(),
            svc::input::StickIndex>
            posted{};
    };

    void forget_(std::optional<proto::StickPos> at) noexcept;

    infra::IndexedArray<PlayerSticks, svc::kMaxPlayers, svc::PlayerIndex> levels_{};
    xthread::WakeFlag::Cursor reset_seen_{};
    infra::Tally binds_seen_{};
    infra::Tally gate_ends_seen_{};
};

}
