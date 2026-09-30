// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"

namespace mister::proto {

class Ps2Keyboard {
    TASTY_SEAT_RESIDENT(Input);

public:
    Ex<void> key_event(HidUsage hid_usage, bool pressed);

    [[nodiscard]] bool next_frame(Ps2Frame& out) noexcept;

    Ex<void> accept_control(std::uint16_t word0);

    void end_control() noexcept;

    bool control_owned() const noexcept { return ps2_control_; }

    std::size_t queued() const noexcept { return frame_count_; }
    static constexpr std::size_t capacity() noexcept { return kFrameQueueCap; }

    std::uint32_t frames_dropped_overflow() const noexcept { return drop_overflow_; }

    unsigned release_all();

private:
    std::array<bool, 256> down_{};
    std::uint8_t scan_set_ = 2;

    static constexpr std::uint8_t kMaxFrameBytes = kPs2FrameBytes;
    static constexpr std::uint8_t kFrameQueueCap = 32;
    struct Frame {
        std::array<std::uint8_t, kMaxFrameBytes> bytes{};
        std::uint8_t len = 0;
    };
    std::array<Frame, kFrameQueueCap> frames_{};
    std::uint8_t frame_head_ = 0, frame_tail_ = 0, frame_count_ = 0;
    std::uint32_t drop_overflow_ = 0;
    void push_frame(const std::uint8_t* bytes, std::uint8_t len);

    bool ps2_control_ = false;
    std::uint8_t kbd_cmd_ = 0;
    bool kbd_awaiting_param_ = false;

    static constexpr std::uint8_t kReplyQueueCap = 16;
    std::array<std::uint8_t, kReplyQueueCap> replies_{};
    std::uint8_t reply_head_ = 0, reply_tail_ = 0, reply_count_ = 0;
    void push_reply(std::uint8_t code);

    void decode_control(std::uint8_t kbd_ctl);
};

}  // namespace mister::proto
