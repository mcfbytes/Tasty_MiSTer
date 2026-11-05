// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input_decode_tally.h"
#include "app/input_wire.h"
#include "infra/indexed_array.h"
#include "infra/seat.h"
#include "svc/input_service.h"

namespace mister::app {
template <SeatTag Seat>
class LinkTxChannel;

class JoyLevelEmitter {
    TASTY_SEAT_RESIDENT(Input);

public:
    JoyLevelEmitter(
        InputWire& wire,
        svc::InputService& svc,
        LinkTxChannel<SeatTag::Input>& inbox,
        InputDecodeTally& tally
    ) noexcept
        : wire_(wire),
          svc_(svc),
          inbox_(inbox),
          tally_(tally) {}

    JoyLevelEmitter(const JoyLevelEmitter&) = delete;
    JoyLevelEmitter& operator=(const JoyLevelEmitter&) = delete;

    void publish(bool muted);

    [[nodiscard]] bool owes_publish() const noexcept {
        return reset_seen_.peek(wire_.edge_reset()) || wire_.gates() != wm_gates_;
    }

private:
    InputWire& wire_;
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
    InputGateBits wm_gates_{};
};

}
