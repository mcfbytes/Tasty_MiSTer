// SPDX-License-Identifier: GPL-3.0-or-later
#include "topology_outcome.h"

#include <sched.h>
#include <unistd.h>

#include <cerrno>

#include "os/types.h"

namespace mister::fw {

[[nodiscard]] TopologyOutcome verify_cpu_topology(
    const hal::ThreadMap& m,
    long online_cpus,
    RtMode mode
) noexcept {
    TopologyOutcome out;
    out.online = online_cpus;
    if (online_cpus <= 0) {

        out.topology = RtSetup{false, errno != 0 ? errno : EINVAL};
        return out;
    }

    const long declared = static_cast<long>(hal::max_cpu(m).v) + 1;
    const std::size_t online = online_cpus < 0 ? std::size_t{0}
                                               : static_cast<std::size_t>(online_cpus);
    out.coverage = RtSetup{online_cpus == declared, online_cpus == declared ? 0 : ENOSPC};
    if (hal::cpus_within(m, online)) {
        out.topology = RtSetup{true, 0};
        return out;
    }
    out.topology = RtSetup{false, ERANGE};
    if (mode == RtMode::Required) {

        out.verdict = std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), ERANGE));
    }
    return out;
}

bool cpus_in_affinity_mask(const hal::ThreadMap& m) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof set, &set) != 0)
        return false;
    for (const hal::ThreadRole& r : m) {
        if (!os::cpu_set_has(set, r.cpu))
            return false;
    }
    return true;
}

[[nodiscard]] TopologyOutcome rt_topology_init(const hal::ThreadMap& m, RtMode mode) noexcept {
    errno = 0;
    TopologyOutcome out = verify_cpu_topology(m, ::sysconf(_SC_NPROCESSORS_ONLN), mode);
    if (out.verdict)
        return out;

    if (cpus_in_affinity_mask(m)) {

        out.topology = RtSetup{true, EXDEV};
        out.verdict = Ex<void>{};
        return out;
    }
    return out;
}

}
