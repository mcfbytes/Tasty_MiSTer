// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace mister::svc {

struct FbParCrc {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr FbParCrc of(std::uint16_t reply) noexcept {
        return FbParCrc{static_cast<std::uint8_t>(reply)};
    }

    friend constexpr bool operator==(FbParCrc, FbParCrc) = default;
};

}
