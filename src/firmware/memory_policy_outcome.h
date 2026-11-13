// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>

#include "infra/error.h"
#include "os/kernel_rt.h"
#include "os/vm_compaction.h"
#include "rt_setup.h"

namespace mister::fw {

struct MemoryPolicyOutcome {
    os::KernelRt kernel = os::KernelRt::Unknown;
    RtSetup kernel_rt{};
    RtSetup trim_pin{};
    RtSetup mmap_pin{};
    RtSetup mem_lock{};
    os::VmCompaction vm{};
    Ex<void> verdict{};
};

[[nodiscard]] MemoryPolicyOutcome rt_memory_init(RtMode mode);

void warn_vm_compaction(const os::VmCompaction& vm, const char* prog, std::FILE* out) noexcept;

}
