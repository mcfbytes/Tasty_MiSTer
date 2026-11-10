// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "os/map_length.h"

namespace mister::os {

struct StackWalk;

struct PageSize {
    std::size_t v = 0;

    [[nodiscard]] constexpr bool usable() const noexcept { return v != 0; }

    [[nodiscard]] constexpr MapLength map_length() const noexcept { return MapLength{v}; }

    friend constexpr bool operator==(PageSize, PageSize) = default;

private:
    friend void touch_stack_pages(StackWalk walk) noexcept;

    [[nodiscard]] constexpr bool is_last(std::size_t offset) const noexcept { return offset < v; }

    [[nodiscard]] constexpr std::size_t stepped_from(std::size_t offset) const noexcept {
        return offset - v;
    }
};

struct StackWalk {
    std::size_t span = 0;
    PageSize page{};
};

void touch_stack_pages(StackWalk walk) noexcept;

}
