// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/free_space.h"

namespace mister::ui {

std::string format_free_space(std::uint64_t avail_bytes, FreeSpaceStyle style) {
    constexpr std::uint64_t kTenGiB = 10ull * 1024 * 1024 * 1024;
    const bool gb = avail_bytes >= kTenGiB;
    std::string s = std::to_string(
        gb ? avail_bytes / (1024ull * 1024 * 1024) : avail_bytes / (1024ull * 1024)
    );
    if (style == FreeSpaceStyle::Hd) {
        s += gb ? " GB" : " MB";
    } else {
        s += gb ? "gb" : "mb";
    }
    return s;
}

}
