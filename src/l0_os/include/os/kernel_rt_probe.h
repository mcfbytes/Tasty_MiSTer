// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "os/kernel_rt.h"

namespace mister::os {

inline constexpr const char* kRealtimeFlagPath = "/sys/kernel/realtime";

KernelRt kernel_rt_from(std::optional<std::string_view> realtime_flag,
                        std::optional<std::string_view> uname_version) noexcept;

KernelRt read_kernel_rt(const char* sysfs_path) noexcept;

}  // namespace mister::os
