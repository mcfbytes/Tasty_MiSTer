// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/free_space.h"

namespace mister::ui {

std::string format_free_space(proto::FileSize avail, FreeSpaceStyle style) {
    const std::uint64_t avail_bytes = avail.v;
    constexpr std::uint64_t kMiB = std::uint64_t{1} << 20;
    constexpr std::uint64_t kGiB = std::uint64_t{1} << 30;
    constexpr std::uint64_t kTenGiB = 10 * kGiB;
    const bool gb = avail_bytes >= kTenGiB;
    std::string s = std::to_string(gb ? avail_bytes / kGiB : avail_bytes / kMiB);
    if (style == FreeSpaceStyle::Hd) {
        s += gb ? " GB" : " MB";
    } else {
        s += gb ? "gb" : "mb";
    }
    return s;
}

}
