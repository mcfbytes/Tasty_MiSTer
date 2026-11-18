// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "hal/types.h"
#include "os/types.h"

namespace mister::hal {

struct PhysRegion {
    os::PhysAddr phys;
    std::size_t len;
    const char* name;

    [[nodiscard]] constexpr os::PhysAddr end() const noexcept { return phys.advanced(len); }
};

template <class U>
constexpr bool span_fits(U off, U n, U len) noexcept {
    static_assert(std::is_unsigned_v<U>, "span_fits is modular-arithmetic reasoning");
    return n <= len && off <= len - n;
}

}
