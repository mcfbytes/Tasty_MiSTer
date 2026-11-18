// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "os/types.h"

namespace mister::os {

struct UioLineSpace {
    std::string_view node_prefix;
    F2hLine pool_first{};
    F2hLine first_node{1};
    F2hLine last_node{};
    std::string_view sysfs_root = "/sys/class/uio";
    std::string_view dev_dir = "/dev";

    [[nodiscard]] constexpr F2hLine f2h_of(UioLine line) const noexcept {
        return F2hLine::of_pool(line, pool_first);
    }

    [[nodiscard]] constexpr bool contains(UioLine line) const noexcept {
        const F2hLine f2h = f2h_of(line);
        return first_node <= f2h && f2h <= last_node;
    }

    [[nodiscard]] static constexpr UioLine line_at(
        std::uint32_t ordinal,
        std::uint32_t first_line
    ) noexcept {
        return pool_line_at(ordinal, first_line);
    }
};

}
