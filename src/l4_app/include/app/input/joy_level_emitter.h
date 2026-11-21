// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input/input_decode_tally.h"
#include "app/input/pad_egress.h"
#include "infra/tally.h"
#include "infra/xthread/counter.h"
#include "infra/indexed_array.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "svc/input_gates.h"
#include "svc/input/input_service.h"

namespace mister::app::input {

struct JoyLevelEmitterTestAccess;

class JoyLevelEmitter {
    TASTY_SEAT_RESIDENT(Input);

public:
    JoyLevelEmitter(
        const xthread::WakeFlag& edge_reset,
        xthread::Counter& activity,
        svc::input::InputService& svc,
        PadEgress& pads,
        InputDecodeTally& tally
    ) noexcept
        : edge_reset_(edge_reset),
          activity_(activity),
          svc_(svc),
          pads_(pads),
          tally_(tally) {}

    JoyLevelEmitter(const JoyLevelEmitter&) = delete;
    JoyLevelEmitter& operator=(const JoyLevelEmitter&) = delete;

    void publish(bool muted, svc::InputGates gates);

    [[nodiscard]] bool owes_publish(svc::InputGates gates) const noexcept {
        return reset_seen_.peek(edge_reset_) || gates != wm_gates_ || pads_.binds() != binds_seen_;
    }

private:
    friend struct JoyLevelEmitterTestAccess;
    const xthread::WakeFlag& edge_reset_;
    xthread::Counter& activity_;
    svc::input::InputService& svc_;
    PadEgress& pads_;
    InputDecodeTally& tally_;

    struct PlayerLevel {
        svc::JoyMask posted{};
        bool zero_armed = true;
        svc::JoyMask armed_mask{};
        svc::JoyMask armed_autofire{};
        svc::JoyMask autofire{};
    };

    infra::IndexedArray<PlayerLevel, svc::kMaxPlayers, svc::PlayerIndex> levels_{};
    xthread::WakeFlag::Cursor reset_seen_{};
    svc::InputGates wm_gates_{};
    infra::Tally binds_seen_{};
};

}
