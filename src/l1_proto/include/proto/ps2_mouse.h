// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "proto/ps2_command_port.h"
#include "proto/ps2_frame.h"

namespace mister::proto {

class Ps2Mouse {
    TASTY_SEAT_RESIDENT(Input);

public:
    [[nodiscard]] Ex<void> motion(int dx, int dy, int dz, std::uint8_t buttons);

    [[nodiscard]] bool next_frame(Ps2Frame& out) noexcept;

    [[nodiscard]] Ex<void> accept_control(std::uint16_t word1);

    void end_control() noexcept;

    std::size_t queued() const noexcept { return packets_.size(); }

    static constexpr std::size_t capacity() noexcept { return kPacketQueueCap; }

private:
    struct Packet {
        std::uint16_t word0 = 0, word1 = 0, word2 = 0;
    };

    static constexpr std::uint8_t kPacketQueueCap = 16;
    infra::FixedRing<Packet, kPacketQueueCap> packets_{};

    Ps2CommandPort port_;

    void decode_control(std::uint8_t mouse_ctl);
};

}
