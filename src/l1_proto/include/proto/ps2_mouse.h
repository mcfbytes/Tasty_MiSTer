// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/error.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "proto/frame_drops.h"
#include "proto/mouse_delta.h"
#include "proto/ps2_command_port.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"

namespace mister::proto {

class Ps2Mouse {
    TASTY_SEAT_RESIDENT(Input);

public:
    [[nodiscard]] Ex<void> motion(MouseDelta delta, MouseButtons buttons);

    [[nodiscard]] std::optional<Ps2Frame> next_frame() noexcept;

    void accept_control(std::uint16_t word1);

    void end_control() noexcept;

    std::size_t queued() const noexcept { return packets_.size(); }

    static constexpr std::size_t capacity() noexcept { return kPacketQueueCap; }

    FrameDrops overflow_drops() const noexcept { return drops_; }

private:
    struct Packet {
        std::uint16_t word0 = 0, word1 = 0, word2 = 0;

        [[nodiscard]] static Packet of(MouseDelta delta, MouseButtons buttons) noexcept;
    };

    static constexpr std::uint8_t kPacketQueueCap = 16;
    infra::FixedRing<Packet, kPacketQueueCap> packets_{};
    FrameDrops drops_{};

    Ps2CommandPort port_;

    void decode_control(std::uint8_t mouse_ctl);
};

}
