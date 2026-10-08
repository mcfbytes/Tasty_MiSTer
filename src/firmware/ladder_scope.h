// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sched.h>

#include <atomic>
#include <cstdint>

#include "app/board_ops.h"
#include "hal/boards_table.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::hal {
class IBridgeSequencer;
}
namespace mister::reactor {
class RoundTimer;
}

namespace mister::fw {

class LadderScope final : public app::IRtPriorityScope, public app::IBoardReset {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr SeatTag kSeat = SeatTag::RT;

    static constexpr unsigned kResetSettleMs = 5000;

    LadderScope(std::atomic<bool>& transitioning, hal::IBridgeSequencer& bridges,
                infra::OptRef<reactor::RoundTimer> timer = {}) noexcept
        : transitioning_(transitioning), bridges_(bridges), timer_(timer) {}
    LadderScope(const LadderScope&) = delete;
    LadderScope& operator=(const LadderScope&) = delete;

    void relax() override;
    void restore() override;
    void board_reset() override;

private:
    std::atomic<bool>& transitioning_;
    hal::IBridgeSequencer& bridges_;
    infra::OptRef<reactor::RoundTimer> timer_{};
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
