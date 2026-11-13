// SPDX-License-Identifier: GPL-3.0-or-later
#include "rt_setup.h"

#include <sched.h>

#include <cerrno>

namespace mister::fw {

[[nodiscard]] RtSetup confirm_current_cpu(os::CpuId cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof set, &set) != 0)
        return RtSetup{false, errno};
    const bool exact = os::cpu_set_has(set, cpu) && CPU_COUNT(&set) == 1;
    return RtSetup{exact, exact ? 0 : ERANGE};
}

[[nodiscard]] RtSetup pin_current_cpu(os::CpuId cpu) noexcept {
    const cpu_set_t set = os::cpu_set_of(cpu);
    if (::sched_setaffinity(0, sizeof set, &set) != 0)
        return RtSetup{false, errno};

    return confirm_current_cpu(cpu);
}

}
