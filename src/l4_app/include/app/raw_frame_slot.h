// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
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
    std::uint8_t depth = 0;
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

inline constexpr std::size_t kRawFrameSlots = 16;
inline constexpr std::size_t kRawArenaBudget = 8u << 20;
inline constexpr std::size_t kRawMinDepth = 4;

[[nodiscard]] constexpr std::size_t raw_depth(std::size_t stripe_bytes) noexcept {
    if (stripe_bytes == 0) return kRawFrameSlots;
    return std::clamp(kRawArenaBudget / stripe_bytes, kRawMinDepth, kRawFrameSlots);
}
static_assert(raw_depth(std::size_t{512} * 3 * 239) == kRawFrameSlots, "SNES keeps every slot");
static_assert(raw_depth(std::size_t{640} * 3 * 480) == 9);
static_assert(raw_depth(std::size_t{1920} * 3 * 1080) == kRawMinDepth);

using RawFrameChannel =
    xthread::LoanChannel<RawFrameSlot, kRawFrameSlots, SeatTag::Capture, SeatTag::Encode>;

}  // namespace mister::app
