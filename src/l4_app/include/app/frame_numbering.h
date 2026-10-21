// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/core_frame_record.h"
#include "app/frame_stamp.h"
#include "infra/seat.h"

namespace mister::app {

class FrameNumbering {
    TASTY_SEAT_EXEMPT(component);

public:
    struct LastCtr {
        std::uint8_t ctr = 0;
        std::int64_t seen_ns = 0;
    };

    static constexpr std::size_t kAnchorVotes = 8;

    struct Gap {
        std::uint32_t n = 0;
        bool estimated = false;
    };

    struct Stamped {
        FrameStamp stamp{};
        bool rebased = false;
        bool drifted = false;
    };

    void restart() noexcept;

    [[nodiscard]] const std::optional<LastCtr>& last() const noexcept { return last_; }

    [[nodiscard]] std::uint64_t core_frame() const noexcept;

    [[nodiscard]] Gap frames_since(
        std::uint8_t ctr,
        std::int64_t now,
        const CoreFrameRecord* cell,
        std::int64_t lag,
        std::int64_t behind,
        bool woven,
        bool anchored,
        std::int64_t& period_ns
    ) noexcept;

    void advance(std::uint32_t n) noexcept { ext_ += n; }

    [[nodiscard]] bool anchor(
        const CoreFrameRecord& cell,
        std::int64_t lag,
        bool vote,
        bool anchored
    ) noexcept;

    [[nodiscard]] Stamped stamp(
        std::uint8_t ctr,
        std::int64_t now,
        const CoreFrameRecord* cell,
        const std::optional<CoreFrameRecord>& last_cell,
        bool anchored,
        std::int64_t lag
    ) noexcept;
    [[nodiscard]] std::int32_t movie_of(
        const CoreFrameRecord& cell,
        std::uint64_t core
    ) const noexcept;

private:
    void rebase_(const CoreFrameRecord& cell, std::uint64_t core, std::int64_t lag) noexcept;
    [[nodiscard]] bool cross_check_(
        const CoreFrameRecord& cell,
        std::uint64_t core,
        std::int64_t lag
    ) const noexcept;

    std::optional<LastCtr> last_{};
    std::uint64_t ext_ = 0;
    std::int64_t anchor_off_ = -1;
    std::array<std::int64_t, kAnchorVotes> votes_{};
    std::uint8_t nvotes_ = 0;
    std::uint8_t vote_next_ = 0;

    std::int64_t cell_off_ = 0;
    std::uint32_t cell_epoch_ = 0;
};

}
