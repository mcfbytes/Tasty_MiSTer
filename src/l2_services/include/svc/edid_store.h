// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/adv7513_io.h"
#include "svc/types.h"

namespace mister::svc {

inline constexpr std::size_t kEdidSegmentBytes = 256;
inline constexpr std::size_t kEdidBlocksPerSegment = 2;

inline constexpr std::size_t kMaxEdidSegments = 8;

using EdidSegment = std::array<std::byte, kEdidSegmentBytes>;

struct EdidSegmentCount {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr EdidSegmentCount announced_by(const EdidSegment& base) noexcept {
        constexpr std::size_t kExtCountOffset = 126;
        const std::size_t blocks = 1u + std::to_integer<std::uint8_t>(base[kExtCountOffset]);
        const std::size_t segments = (blocks + kEdidBlocksPerSegment - 1u) / kEdidBlocksPerSegment;
        return EdidSegmentCount{
            static_cast<std::uint8_t>(segments > kMaxEdidSegments ? kMaxEdidSegments : segments)
        };
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::span<const EdidSegment> prefix_of(
        std::span<const EdidSegment> all
    ) const noexcept {
        return all.first(v);
    }

    friend constexpr bool operator==(EdidSegmentCount, EdidSegmentCount) = default;
};

static_assert(kMaxEdidSegments <= std::size_t{255}, "a segment count is one byte");

class EdidStore {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] Ex<void> refresh(const adv7513::Io& io);

    [[nodiscard]] std::optional<std::span<const std::byte>> current() const noexcept;

    Generation generation() const noexcept { return gen_; }

    static constexpr std::chrono::milliseconds kReadyTimeout{500};
    static constexpr std::chrono::microseconds kReadyPoll{10'000};
    static constexpr std::chrono::microseconds kTriggerSettle{1'000};
    static constexpr std::chrono::milliseconds kRetryDelay{100};
    static constexpr int kMaxRetries = 20;

    std::uint32_t ready_timeouts() const noexcept { return ready_timeouts_; }

    std::span<const std::byte> full() const noexcept;

private:
    [[nodiscard]] Ex<bool> read_segment(
        const adv7513::Io& io,
        std::uint8_t segment,
        std::span<std::byte> out
    );

    Generation gen_{};
    std::array<EdidSegment, kMaxEdidSegments> segments_{};
    EdidSegmentCount segments_live_{};
    std::uint32_t ready_timeouts_ = 0;
};

}
