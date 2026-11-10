// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/fixed_ring.h"
#include "infra/seat.h"
#include "proto/frame_drops.h"
#include "proto/ps2_command_port.h"
#include "proto/ps2_frame.h"
#include "proto/types.h"

namespace mister::proto {

enum class ScanSet : std::uint8_t {
    kSet1 = 1,
    kSet2 = 2,
    kSet3 = 3,
};

[[nodiscard]] constexpr Ps2Reply scan_set_reply(ScanSet set) noexcept {
    return reply_of(static_cast<std::uint8_t>(set));
}

[[nodiscard]] constexpr ScanSet scan_set_of(std::uint8_t n) noexcept {
    return n == 1 ? ScanSet::kSet1 : n == 3 ? ScanSet::kSet3 : ScanSet::kSet2;
}

enum class Press : std::uint8_t { kRelease = 0, kMake = 1, kRepeat = 2 };

class Ps2Keyboard {
    TASTY_SEAT_RESIDENT(Input);

public:
    [[nodiscard]] Ex<void> key_event(HidUsage hid_usage, bool pressed);

    [[nodiscard]] std::optional<Ps2Frame> next_frame() noexcept;

    void accept_control(std::uint16_t word0);

    void end_control() noexcept;

    bool control_owned() const noexcept { return ps2_control_; }

    std::size_t queued() const noexcept { return frames_.size(); }

    static constexpr std::size_t capacity() noexcept { return kFrameQueueCap; }

    FrameDrops overflow_drops() const noexcept { return drops_; }

    unsigned release_all();

private:
    std::array<bool, 256> down_{};
    ScanSet scan_set_ = ScanSet::kSet2;

    static constexpr std::uint8_t kMaxFrameBytes = kPs2FrameBytes;
    static constexpr std::uint8_t kFrameQueueCap = 32;
    infra::FixedRing<Ps2Frame, kFrameQueueCap> frames_{};
    FrameDrops drops_{};
    void push_frame(std::span<const std::uint8_t> bytes);

    bool ps2_control_ = false;
    Ps2CommandPort port_;

    void decode_control(std::uint8_t kbd_ctl);
};

}
