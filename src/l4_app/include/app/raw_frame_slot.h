// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "app/frame_stamp.h"
#include "app/picture_pixels.h"
#include "app/rec_control.h"
#include "app/rgb_rows.h"
#include "app/types.h"
#include "infra/loan_channel.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

struct RawFrameSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    enum class Kind : std::uint8_t { Open, Frame, Close, kCount };

    using Head = ControlGeneration;

    static constexpr std::size_t kMaxRuns = 4;

    struct Open {
        static constexpr Kind kKind = Kind::Open;
        RecOptions opt{};
        RecPath path{};
        RecMode mode = RecMode::Hash;
        std::array<std::byte, 3> pad_{};
    };

    struct Frame {
        static constexpr Kind kKind = Kind::Frame;
        RgbRows rgb{};
#if __SIZEOF_POINTER__ == 4

        std::array<std::byte, 4> pad_ilp32_{};
#endif
        FrameStamp stamp{};
        std::array<FrameStampRun, kMaxRuns> run{};
        std::uint32_t vtime = 0;
        PicturePixels width{};
        std::uint8_t runs = 0;
        std::uint8_t depth = 0;
    };

    struct Close {
        static constexpr Kind kKind = Kind::Close;
        RgbRows rgb{};
#if __SIZEOF_POINTER__ == 4

        std::array<std::byte, 4> pad_ilp32_{};
#endif
        FrameStamp stamp{};
        std::array<FrameStampRun, kMaxRuns> run{};
        PicturePixels width{};
        std::uint8_t runs = 0;
        std::array<std::byte, 5> pad_{};
    };

    using Alternatives = std::tuple<Open, Frame, Close>;
    static constexpr std::size_t kStore = sizeof(Open);

    Kind kind = Kind::Open;
    std::array<std::byte, 1> pad_{};
    Head head{};
    std::array<std::byte, 4> pad2_{};
    alignas(std::uint64_t) std::array<std::byte, kStore> store{};
};

static_assert(infra::MessageSum<RawFrameSlot> && infra::alternatives_are_total<RawFrameSlot>());

inline constexpr std::size_t kRawFrameSlots = 32;
inline constexpr std::size_t kRawStripeGranule = 2u << 20;
inline constexpr std::size_t kRawArenaBudget = kRawFrameSlots * kRawStripeGranule;
inline constexpr std::size_t kRawMinDepth = 4;

[[nodiscard]] constexpr std::size_t raw_stripe(std::size_t frame_bytes) noexcept {
    const std::size_t n = frame_bytes == 0 ? 1 : (frame_bytes - 1) / kRawStripeGranule + 1;
    return n * kRawStripeGranule;
}

[[nodiscard]] constexpr std::size_t raw_depth(std::size_t stripe_bytes) noexcept {
    if (stripe_bytes == 0)
        return kRawFrameSlots;
    return std::clamp(kRawArenaBudget / stripe_bytes, kRawMinDepth, kRawFrameSlots);
}

static_assert(raw_depth(raw_stripe(std::size_t{512} * 3 * 239)) == kRawFrameSlots);
static_assert(
    raw_stripe(std::size_t{281} * 3 * 239) == raw_stripe(std::size_t{704} * 3 * 722),
    "a PSX 240p stripe already holds the 480i switch's largest frame"
);
static_assert(raw_depth(raw_stripe(std::size_t{1920} * 3 * 1080)) == 10);
static_assert(
    raw_stripe(kRawStripeGranule) == kRawStripeGranule && raw_stripe(1) == kRawStripeGranule
);

using RawFrameChannel = xthread::
    LoanChannel<RawFrameSlot, kRawFrameSlots, SeatTag::Capture, SeatTag::Encode>;

}
