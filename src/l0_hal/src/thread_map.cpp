// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/thread_map.h"

#include <sched.h>

#include <cerrno>

#include "infra/persist.h"

namespace mister::hal {

namespace {

thread_local const ThreadMap* t_map TASTY_PERSIST(proc, hal_placement_map) = nullptr;
thread_local SeatTag t_seat TASTY_PERSIST(proc, hal_placement_seat) = SeatTag::RT;

[[nodiscard]] Ex<void> apply_self_affinity(const ::cpu_set_t& set) noexcept {
    if (::sched_setaffinity(0, sizeof set, &set) == 0)
        return {};
    return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
}

}

void adopt_placement(const ThreadMap& m, SeatTag seat) noexcept {
    t_map = &m;
    t_seat = seat;
}

[[nodiscard]] Ex<void> widen_self_affinity() noexcept {

    if (t_map == nullptr)
        return unimplemented(ERR_SITE());
    ::cpu_set_t set;
    CPU_ZERO(&set);
    const unsigned hi = max_cpu(*t_map).v;
    for (unsigned c = 0; c <= hi; ++c)
        CPU_SET(c, &set);
    return apply_self_affinity(set);
}

[[nodiscard]] Ex<void> restore_self_affinity() noexcept {
    if (t_map == nullptr)
        return unimplemented(ERR_SITE());
    const ::cpu_set_t set = os::cpu_set_of(seat_of(*t_map, t_seat).cpu);
    return apply_self_affinity(set);
}

}
