// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <string_view>

#include "os/types.h"
#include "os/writeback_paths.h"
#include "os/writeback_probe.h"

namespace mister::fw {

void warn_writeback_cpumask(
    const os::WritebackProbe& wb,
    os::CpuId rt_cpu,
    const char* prog,
    std::FILE* out
) noexcept;

void tasty_warn_record_writeback(std::string_view record, os::CpuId rt_cpu) noexcept;

void tasty_warn_record_writeback(
    std::string_view record,
    os::CpuId rt_cpu,
    os::WritebackPaths paths,
    std::FILE* out
) noexcept;

}
