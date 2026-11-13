// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "proto/types.h"

namespace mister::proto {

[[nodiscard]] std::uint8_t ext_subindex(
    std::string_view filename,
    std::string_view ext_list
) noexcept;

struct ConfStrFileRow {
    std::string_view ext;
    IoIndex ioctl_index{};
    std::uint32_t load_addr = 0;
    bool opensave = false;
    std::string_view addon{};
    bool addon_after = false;

    [[nodiscard]] WideIoIndex wire_index(std::string_view filename) const noexcept {
        return WideIoIndex::of(ioctl_index, ext_subindex(filename, ext));
    }
};

}
