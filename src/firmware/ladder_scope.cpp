// SPDX-License-Identifier: GPL-3.0-or-later
#include "ladder_scope.h"

#include <pthread.h>
#include <unistd.h>

#include <cstdio>

#include "hal/bridge_sequencer.h"
#include "reactor/round_timer.h"
#include "thread_assembly.h"

namespace mister::fw {

void LadderScope::relax() {
    TASTY_SEAT_BODY(LadderScope);
    assembly_.mark_transitioning(true);
    if (timer_ != nullptr) timer_->note_lifecycle(reactor::RoundLifecycle::Rung);
    if (::pthread_getschedparam(::pthread_self(), &saved_policy_, &saved_param_) != 0) {
        saved_policy_ = SCHED_OTHER;
        saved_param_ = sched_param{};
    }
    sched_param other{};
    (void)::pthread_setschedparam(::pthread_self(), SCHED_OTHER, &other);
}

void LadderScope::restore() {
    TASTY_SEAT_BODY(LadderScope);
    if (::pthread_setschedparam(::pthread_self(), saved_policy_, &saved_param_) != 0) {
        const auto n = restore_failures_.fetch_add(1, std::memory_order_relaxed) + 1;
        std::fprintf(stderr, "mister: RT restore after ladder rung FAILED (%u)\n",
                     static_cast<unsigned>(n));
    }
    assembly_.mark_transitioning(false);
}

void LadderScope::board_reset() {
    TASTY_SEAT_BODY(LadderScope);

    if (!bridges_.request_board_reset()) return;
    for (unsigned ms = 0; ms < kResetSettleMs; ms += 100) {
        ::usleep(100000);
    }
}

}  // namespace mister::fw
