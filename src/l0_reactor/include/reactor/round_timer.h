// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/seat.h"
#include "reactor/round_timing.h"

namespace mister::reactor {

class RoundTimer {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::int64_t kEpochNs = 1'000'000'000;
    static constexpr std::uint64_t kOverNs = 1'000'000;

    RoundTimer() = default;
    RoundTimer(const RoundTimer&) = delete;
    RoundTimer& operator=(const RoundTimer&) = delete;

    void begin(std::int64_t t_wake_ns) noexcept;
    void wake(std::int64_t latency_ns, std::uint64_t missed_ticks) noexcept;
    void mark(RoundSegment seg, std::size_t index, std::int64_t now_ns) noexcept;
    void note_lifecycle(RoundLifecycle why) noexcept;

    std::int64_t end(std::int64_t t_end_ns) noexcept;
    void flush(std::int64_t now_ns) noexcept;
    [[nodiscard]] std::int64_t last_mark_ns() const noexcept { return last_mark_ns_; }
    [[nodiscard]] const RoundTimingCell& cell() const noexcept { return cell_; }

    void preload_all_steady(std::uint32_t rounds, std::uint64_t sum_ns) noexcept {
        acc_.all_steady_rounds = rounds;
        acc_.all_steady_sum_ns = sum_ns;
    }

private:
    static std::size_t record_(RoundTiming::Dist& d, std::uint64_t ns,
                               RoundTiming::Cause c) noexcept;
    void close_epoch_(std::int64_t now_ns) noexcept;

    RoundTiming acc_{};
    std::int64_t epoch_start_ns_ = 0;
    std::int64_t round_start_ns_ = 0;
    std::int64_t last_mark_ns_ = 0;
    std::int64_t longest_ns_ = -1;
    RoundTiming::Cause longest_{};
    std::uint8_t lifecycle_ = 0;
    bool in_round_ = false;
    RoundTimingCell cell_{};
};

}  // namespace mister::reactor
