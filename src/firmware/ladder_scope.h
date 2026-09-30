// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sched.h>

#include <atomic>
#include <cstdint>

#include "app/board_ops.h"
#include "hal/boards_table.h"
#include "infra/seat.h"

namespace mister::hal {
class IBridgeSequencer;
}
namespace mister::reactor {
class RoundTimer;
}

namespace mister::fw {

class ThreadAssembly;

class LadderScope final : public app::IRtPriorityScope, public app::IBoardReset {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr hal::Seat kSeat = hal::Seat::RT;

    static constexpr unsigned kResetSettleMs = 5000;

    LadderScope(ThreadAssembly& assembly, hal::IBridgeSequencer& bridges) noexcept
        : assembly_(assembly), bridges_(bridges) {}
    LadderScope(const LadderScope&) = delete;
    LadderScope& operator=(const LadderScope&) = delete;

    void relax() override;
    void restore() override;
    void board_reset() override;

    void set_round_timer(reactor::RoundTimer* timer) noexcept { timer_ = timer; }

private:
    ThreadAssembly& assembly_;
    hal::IBridgeSequencer& bridges_;
    reactor::RoundTimer* timer_ = nullptr;
    int saved_policy_ = SCHED_OTHER;
    sched_param saved_param_{};
    std::atomic<std::uint32_t> restore_failures_{0};
};

static_assert(hal::every_thread_map([](const hal::ThreadMap& m) {
                  return hal::seat_of(m, LadderScope::kSeat).policy == hal::SchedPolicy::Fifo;
              }),
              "item: this class exists to DEMOTE its seat for the duration "
              "of a blocking ladder rung (arch §2's one named exception). "
              "Point kSeat at a SCHED_OTHER row and relax()/restore() become "
              "a pair of no-ops around work that never needed bracketing.");

}  // namespace mister::fw
