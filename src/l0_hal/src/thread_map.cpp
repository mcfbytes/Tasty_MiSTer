// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/thread_map.h"

#include <sched.h>

#include "infra/persist.h"

namespace mister::hal {

namespace {

thread_local const ThreadMap* t_map TASTY_PERSIST(proc, hal_placement_map) = nullptr;
thread_local SeatTag t_seat TASTY_PERSIST(proc, hal_placement_seat) = SeatTag::RT;

}  // namespace

void adopt_placement(const ThreadMap& m, SeatTag seat) noexcept {
    t_map = &m;
    t_seat = seat;
}

bool widen_self_affinity() noexcept {
    if (t_map == nullptr) return false;
    ::cpu_set_t set;
    CPU_ZERO(&set);
    const unsigned hi = static_cast<unsigned>(max_cpu(*t_map));
    for (unsigned c = 0; c <= hi; ++c)
        CPU_SET(c, &set);
    return ::sched_setaffinity(0, sizeof set, &set) == 0;
}

bool restore_self_affinity() noexcept {
    if (t_map == nullptr) return false;
    ::cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned>(seat_of(*t_map, t_seat).cpu), &set);
    return ::sched_setaffinity(0, sizeof set, &set) == 0;
}

}  // namespace mister::hal
