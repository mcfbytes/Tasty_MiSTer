// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::reactor {

struct FrameRecord {
    std::uint32_t seq = 0;
};
using FrameCell = xthread::Telemetry<FrameRecord, SeatTag::Frame>;

class FrameClock {
    TASTY_SEAT_RESIDENT(Frame);

public:
    static Ex<std::unique_ptr<FrameClock>> open(const char* fbdev_path);

    hal::FrameSeq seq() const noexcept {
        return hal::FrameSeq{seq_.load(std::memory_order_acquire)};
    }
    int eventfd() const noexcept { return frame_wake_.fd(); }

    [[nodiscard]] const FrameCell& frame_cell() const noexcept { return cell_; }

    void wait_vsync() noexcept;

private:
    FrameClock() = default;
    std::atomic<std::uint32_t> seq_{0};
    FrameCell cell_{};
    UniqueFd fb_fd_;
    xthread::WakeFlag frame_wake_{};
};

}  // namespace mister::reactor
