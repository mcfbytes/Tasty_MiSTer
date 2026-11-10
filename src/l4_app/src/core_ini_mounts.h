// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace mister::app::detail {

inline constexpr std::size_t kProcMountsTextMax = 64u * 1024;

[[nodiscard]] bool mount_present(std::string_view needle, std::string_view path = "/proc/mounts");

}
