// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/ps2_frame.h"

namespace mister::proto {

class Ps2Mouse {
    TASTY_SEAT_RESIDENT(Input);

public:
    Ex<void> motion(int dx, int dy, int dz, std::uint8_t buttons);

    [[nodiscard]] bool next_frame(Ps2Frame& out) noexcept;

    Ex<void> accept_control(std::uint16_t word1);

    void end_control() noexcept;

    std::size_t queued() const noexcept { return packet_count_; }
    static constexpr std::size_t capacity() noexcept { return kPacketQueueCap; }

private:
    struct Packet {
        std::uint16_t word0 = 0, word1 = 0, word2 = 0;
    };
    static constexpr std::uint8_t kPacketQueueCap = 16;
    std::array<Packet, kPacketQueueCap> packets_{};
    std::uint8_t packet_head_ = 0, packet_tail_ = 0, packet_count_ = 0;
    void push_packet(const Packet& p);

    bool ps2_control_ = false;
    std::uint8_t mouse_cmd_ = 0;
    bool mouse_awaiting_param_ = false;

    static constexpr std::uint8_t kReplyQueueCap = 16;
    std::array<std::uint8_t, kReplyQueueCap> replies_{};
    std::uint8_t reply_head_ = 0, reply_tail_ = 0, reply_count_ = 0;
    void push_reply(std::uint8_t code);

    void decode_control(std::uint8_t mouse_ctl);
};

}  // namespace mister::proto
