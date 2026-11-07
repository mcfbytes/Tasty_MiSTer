// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/core_frame_record.h"
#include "app/frame_stamp.h"
#include "app/types.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::app {

class FrameNumbering {
    TASTY_SEAT_EXEMPT(component);

public:
    struct LastCtr {
        hal::HeaderCtr ctr{};
        std::int64_t seen_ns = 0;
    };

    static constexpr std::size_t kAnchorVotes = 8;

    struct Gap {
        CoreFrameCount frames{};
        bool estimated = false;
    };

    struct Stamped {
        FrameStamp stamp{};
        bool rebased = false;
        bool drifted = false;
    };

    void restart() noexcept;

    [[nodiscard]] const std::optional<LastCtr>& last() const noexcept { return last_; }

    [[nodiscard]] WideCoreFrame core_frame() const noexcept { return current_(); }

    [[nodiscard]] Gap frames_since(
        hal::HeaderCtr ctr,
        std::int64_t now,
        const CoreFrameRecord* cell,
        CoreFrameGap lag,
        CoreFrameGap behind,
        bool woven,
        bool anchored,
        std::int64_t& period_ns
    ) noexcept;

    void advance(CoreFrameCount n) noexcept { ext_ += CoreFrameGap::of_count(n); }

    [[nodiscard]] bool anchor(
        const CoreFrameRecord& cell,
        CoreFrameGap lag,
        bool vote,
        bool anchored
    ) noexcept;

    [[nodiscard]] Stamped stamp(
        hal::HeaderCtr ctr,
        std::int64_t now,
        const CoreFrameRecord* cell,
        const std::optional<CoreFrameRecord>& last_cell,
        bool anchored,
        CoreFrameGap lag
    ) noexcept;
    [[nodiscard]] MovieFrame movie_of(
        const CoreFrameRecord& cell,
        WideCoreFrame core
    ) const noexcept;

private:
    static constexpr CoreFrameGap kBeforeFirstCopy{-1};

    [[nodiscard]] WideCoreFrame current_() const noexcept { return ext_ + anchor_off_; }

    void rebase_(const CoreFrameRecord& cell, WideCoreFrame core, CoreFrameGap lag) noexcept;
    [[nodiscard]] bool cross_check_(
        const CoreFrameRecord& cell,
        WideCoreFrame core,
        CoreFrameGap lag
    ) const noexcept;

    std::optional<LastCtr> last_{};
    WideCoreFrame ext_{};
    CoreFrameGap anchor_off_ = kBeforeFirstCopy;
    std::array<CoreFrameGap, kAnchorVotes> votes_{};
    std::uint8_t nvotes_ = 0;
    std::uint8_t vote_next_ = 0;

    CoreFrameGap cell_off_{};
    std::uint32_t cell_epoch_ = 0;
};

}
