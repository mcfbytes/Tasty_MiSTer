// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::cores {

struct PcmHandovers {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr PcmHandovers next() const noexcept { return {v + 1u}; }

    friend constexpr bool operator==(PcmHandovers, PcmHandovers) = default;
};

}
