// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/process_name.h"

#include <sys/prctl.h>

#include <array>
#include <cstdio>
#include <span>

#include "os/small_file.h"

namespace mister::os {

void ProcessName::adopt() const noexcept {
    (void)::prctl(PR_SET_NAME, text_.c_str());
}

bool ProcessName::names(ProcessId pid) const noexcept {
    std::array<char, 32> path{};
    std::snprintf(path.data(), path.size(), "/proc/%d/comm", pid.printed());
    std::array<char, 32> comm{};
    const Ex<std::span<char>> got = read_small_file(path.data(), comm);
    if (!got || got->empty())
        return false;
    std::string_view name{got->data(), got->size()};
    if (name.back() == '\n')
        name.remove_suffix(1);
    return name == view();
}

}
