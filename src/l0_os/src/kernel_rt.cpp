// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/kernel_rt_probe.h"

#include <sys/utsname.h>

#include <array>
#include <cstddef>
#include <span>

#include "os/small_file.h"

namespace mister::os {
namespace {

bool version_says_rt(std::string_view v) noexcept {
    return v.find("PREEMPT_RT") != std::string_view::npos ||
           v.find("PREEMPT RT") != std::string_view::npos;
}

std::string_view trim_tail(std::string_view f) noexcept {
    while (!f.empty() && (f.back() == '\n' || f.back() == '\r' || f.back() == ' ')) {
        f.remove_suffix(1);
    }
    return f;
}

}  // namespace

KernelRt kernel_rt_from(std::optional<std::string_view> realtime_flag,
                        std::optional<std::string_view> uname_version) noexcept {
    if (realtime_flag) {

        const std::string_view f = trim_tail(*realtime_flag);
        if (f == "1") return KernelRt::PreemptRt;
        if (f == "0") return KernelRt::NotRt;
    }
    if (uname_version) {
        return version_says_rt(*uname_version) ? KernelRt::PreemptRt : KernelRt::NotRt;
    }
    return KernelRt::Unknown;
}

KernelRt read_kernel_rt(const char* sysfs_path) noexcept {
    std::array<char, 16> buf{};
    std::optional<std::string_view> flag;
    if (const auto got = read_small_file(sysfs_path, std::span<char>(buf)); got) {
        flag = std::string_view(buf.data(), *got);
    }

    utsname u{};
    std::optional<std::string_view> version;
    if (::uname(&u) == 0) version = std::string_view(u.version);

    return kernel_rt_from(flag, version);
}

}  // namespace mister::os
