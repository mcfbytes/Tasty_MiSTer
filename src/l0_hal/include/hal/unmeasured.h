// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::hal {

struct Unmeasured {
    explicit constexpr Unmeasured() = default;
};
inline constexpr Unmeasured kUnmeasured{};

}  // namespace mister::hal
