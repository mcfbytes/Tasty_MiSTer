// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct MountSerial {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr MountSerial next() const noexcept { return MountSerial{v + 1u}; }

    friend constexpr bool operator==(MountSerial, MountSerial) = default;
};

static_assert(MountSerial{}.next() != MountSerial{});

}
