// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input_decode_tally.h"
#include "infra/counter.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "svc/input_gates.h"
#include "svc/input_service.h"

namespace mister::app {
template <SeatTag Seat>
class LinkTxChannel;

struct JoyLevelEmitterTestAccess;

class JoyLevelEmitter {
    TASTY_SEAT_RESIDENT(Input);

public:
    JoyLevelEmitter(
        const xthread::WakeFlag& edge_reset,
        xthread::Counter& activity,
        svc::InputService& svc,
        LinkTxChannel<SeatTag::Input>& inbox,
        InputDecodeTally& tally
    ) noexcept
        : edge_reset_(edge_reset),
          activity_(activity),
          svc_(svc),
          inbox_(inbox),
          tally_(tally) {}

    JoyLevelEmitter(const JoyLevelEmitter&) = delete;
    JoyLevelEmitter& operator=(const JoyLevelEmitter&) = delete;

    void publish(bool muted, svc::InputGates gates);

    [[nodiscard]] bool owes_publish(svc::InputGates gates) const noexcept {
        return reset_seen_.peek(edge_reset_) || gates != wm_gates_;
    }

private:
    friend struct JoyLevelEmitterTestAccess;
    const xthread::WakeFlag& edge_reset_;
    xthread::Counter& activity_;
    svc::InputService& svc_;
    LinkTxChannel<SeatTag::Input>& inbox_;
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
};

}
