// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "os/types.h"

namespace mister::os {

struct UioLineSpace {
    std::string_view node_prefix;
    std::uint32_t lines = 0;
    std::string_view sysfs_root = "/sys/class/uio";
    std::string_view dev_dir = "/dev";

    [[nodiscard]] constexpr bool contains(UioLine line) const noexcept {
        return to_ordinal(line) < lines;
    }

    [[nodiscard]] static constexpr UioLine line_at(
        std::uint32_t ordinal,
        std::uint32_t first_line
    ) noexcept {
        return pool_line_at(ordinal, first_line);
    }
};

}
