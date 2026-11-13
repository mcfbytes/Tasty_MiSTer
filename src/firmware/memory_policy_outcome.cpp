// SPDX-License-Identifier: GPL-3.0-or-later
#include "memory_policy_outcome.h"
#include "infra/posix_compat.h"

#include <malloc.h>
#include <sys/mman.h>

#include <cerrno>
#include <cstdio>
#include <optional>

#include "os/kernel_rt_probe.h"

namespace mister::fw {

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

}
