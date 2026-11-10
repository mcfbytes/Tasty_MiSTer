// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembly.h"
#include "infra/posix_compat.h"

#include <malloc.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <optional>

#include "os/kernel_rt_probe.h"
#include "os/kernel_tid.h"
#include "os/stack_touch.h"
#include "os/types.h"
#include "rt_setup.h"
#include "stack_extent.h"

namespace mister::fw {

[[nodiscard]] RtSetup confirm_current_cpu(os::CpuId cpu) noexcept {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof set, &set) != 0)
        return RtSetup{false, errno};
    const bool exact = os::cpu_set_has(set, cpu) && CPU_COUNT(&set) == 1;
    return RtSetup{exact, exact ? 0 : ERANGE};
}

[[nodiscard]] MemoryPolicyOutcome rt_memory_init(RtMode mode) {
    MemoryPolicyOutcome out;

    out.kernel = os::read_kernel_rt(os::kRealtimeFlagPath);
    out.kernel_rt = RtSetup{out.kernel == os::KernelRt::PreemptRt, 0};
    out.vm = os::read_vm_compaction(os::kCompactUnevictablePath, os::kCompactionProactivenessPath);

#if defined(__GLIBC__)
    out.trim_pin = RtSetup{infra::mallopt(M_TRIM_THRESHOLD, -1) == 1, 0};
    out.mmap_pin = RtSetup{infra::mallopt(M_MMAP_MAX, 0) == 1, 0};
#else
    out.trim_pin = RtSetup{false, 0};
    out.mmap_pin = RtSetup{false, 0};
    std::fprintf(stderr, "mallopt N/A on musl (mallocng); trim_pin/mmap_pin not attempted\n");
#endif

    if (::mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
        out.mem_lock = RtSetup{true, 0};
        return out;
    }
    out.mem_lock = RtSetup{false, errno};
    if (mode == RtMode::Required) {

        out.verdict = std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), out.mem_lock.err));
    }

    return out;
}

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept {
    struct Row {
        const std::optional<long>& value;
        const char* name;
        const char* effect;
        const char* path;
    };

    const Row rows[] = {
        {vm.unevictable_allowed,
         "vm.compact_unevictable_allowed",
         "the kernel may move locked memory",
         os::kCompactUnevictablePath},
        {vm.proactiveness,
         "vm.compaction_proactiveness",
         "background compaction may move locked memory",
         os::kCompactionProactivenessPath},
    };
    for (const Row& r : rows) {
        if (!r.value || *r.value == 0)
            continue;
        std::fprintf(
            out,
            "%s: warning: %s=%ld: %s and stall real-time I/O; fix: echo 0 > %s\n",
            prog,
            r.name,
            *r.value,
            r.effect,
            r.path
        );
    }
}

[[nodiscard]] StackPrefault prefault_current_stack(std::size_t requested) noexcept {
    StackPrefault out;
    rlimit rl{};
    if (::getrlimit(RLIMIT_STACK, &rl) != 0) {
        out.outcome = RtSetup{false, errno};
        return out;
    }
    out.rlimit = static_cast<std::uint64_t>(rl.rlim_cur);
    pthread_attr_t attr;
    if (const int rc = ::pthread_getattr_np(::pthread_self(), &attr); rc != 0) {
        out.outcome = RtSetup{false, rc};
        return out;
    }
    void* base = nullptr;
    std::size_t mapped = 0;
    const int grc = ::pthread_attr_getstack(&attr, &base, &mapped);
    (void)::pthread_attr_destroy(&attr);
    if (grc != 0) {
        out.outcome = RtSetup{false, grc};
        return out;
    }
    const auto top = std::bit_cast<std::uintptr_t>(base) + mapped;
    const auto sp = std::bit_cast<std::uintptr_t>(__builtin_frame_address(0));

    const std::size_t used = (sp < top && sp >= top - mapped) ? top - sp : mapped;

    const bool growable = !infra::kPosixGlibc && os::calling_task_leads_process();
    const StackExtent stack{used, mapped, out.rlimit};
    out.bytes = stack.span(requested, growable);
    os::touch_stack_pages(os::StackWalk{.span = out.bytes, .page = os::PageSize{kStackPageBytes}});
    out.outcome = RtSetup{out.bytes == requested, out.bytes == requested ? 0 : ENOMEM};
    return out;
}

[[nodiscard]] RtSetup pin_current_cpu(os::CpuId cpu) noexcept {
    const cpu_set_t set = os::cpu_set_of(cpu);
    if (::sched_setaffinity(0, sizeof set, &set) != 0)
        return RtSetup{false, errno};

    return confirm_current_cpu(cpu);
}

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
