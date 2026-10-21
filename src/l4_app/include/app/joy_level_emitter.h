// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/input_decode_tally.h"
#include "app/input_wire.h"
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
        return wire_.edge_reset_seq() != wm_edge_reset_ || wire_.gates() != wm_gates_;
    }

private:
    InputWire& wire_;
    svc::InputService& svc_;
    LinkTxChannel<SeatTag::Input>& inbox_;
    InputDecodeTally& tally_;
    std::uint32_t last_joy_posted_[svc::kMaxPlayers]{};
    bool zero_armed_[svc::kMaxPlayers]{true, true, true, true, true, true};
    std::uint32_t armed_mask_[svc::kMaxPlayers]{};
    std::uint32_t armed_autofire_[svc::kMaxPlayers]{};
    std::uint32_t wm_edge_reset_ = 0;
    std::uint32_t wm_gates_ = 0;
};

}
