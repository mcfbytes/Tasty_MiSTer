// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_writeback.h"

#include <cstdio>

#include "app/path_text.h"
#include "tasty_record.h"

namespace mister::fw {

void warn_writeback_cpumask(
    const os::WritebackProbe& wb,
    os::CpuId rt_cpu,
    const char* prog,
    std::FILE* out
) noexcept {
    if (!wb.fs_magic || !os::is_network_fs(*wb.fs_magic))
        return;
    const auto cpus = os::effective_writeback_cpus(wb);
    if (!cpus)
        return;
    const os::CpuSet rt_only = rt_cpu.as_set();
    if (cpus->intersect(rt_only).empty())
        return;
    os::CpuSet fix = wb.cpumask->without(rt_only);
    if (fix.empty())
        fix = rt_cpu.housekeeping_set();
    std::fprintf(
        out,
        "%s: warning: writeback cpumask=%s: flushing to a network share may run on "
        "real-time CPU %s and stall real-time I/O; fix: echo %s > %s\n",
        prog,
        wb.cpumask->text().c_str(),
        rt_cpu.text().c_str(),
        fix.text().c_str(),
        os::kWritebackCpumaskPath
    );
}

void tasty_warn_record_writeback(
    std::string_view record,
    os::CpuId rt_cpu,
    os::WritebackPaths paths,
    std::FILE* out
) noexcept {
    app::PathText dir{};
    if (!dir.assign(tasty_record_dir(record)))
        return;
    paths.dir = dir.c_str();
    warn_writeback_cpumask(os::read_writeback_probe(paths), rt_cpu, "tasty", out);
}

void tasty_warn_record_writeback(std::string_view record, os::CpuId rt_cpu) noexcept {
    tasty_warn_record_writeback(record, rt_cpu, os::WritebackPaths{}, stderr);
}

}
