// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "app/frame_stamp.h"
#include "app/rec_control.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

enum class RawKind : std::uint8_t { Open, Frame, Close };

struct RawFrameSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::size_t kMaxRuns = 4;

    RawKind kind = RawKind::Frame;
    std::uint8_t runs = 0;
    std::uint16_t gen = 0;
    std::array<FrameStampRun, kMaxRuns> run{};
    FrameStamp stamp{};
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t line = 0;
    std::uint32_t vtime = 0;
    RecMode mode = RecMode::Hash;
    RecOptions opt{};
    const std::byte* pixels = nullptr;
    RecPath path{};
};

inline constexpr std::size_t kRawFrameSlots = 4;
using RawFrameChannel =
    xthread::LoanChannel<RawFrameSlot, kRawFrameSlots, SeatTag::Capture, SeatTag::Encode>;

}  // namespace mister::app
