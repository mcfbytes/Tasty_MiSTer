// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "infra/error.h"

namespace mister::proto {
class Link;
}

namespace mister::svc {

struct ScalerSlotMode {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kOff = 0;
    static constexpr std::uint8_t kFromFile = 1;

    [[nodiscard]] static constexpr ScalerSlotMode of(std::byte raw) noexcept {
        return ScalerSlotMode{std::to_integer<std::uint8_t>(raw)};
    }

    [[nodiscard]] static constexpr ScalerSlotMode off() noexcept { return ScalerSlotMode{kOff}; }

    [[nodiscard]] static constexpr ScalerSlotMode from_file() noexcept {
        return ScalerSlotMode{kFromFile};
    }

    [[nodiscard]] constexpr bool enabled() const noexcept { return v != kOff; }

    [[nodiscard]] constexpr bool legal() const noexcept { return v <= kFromFile; }

    [[nodiscard]] Ex<void> write_to(proto::Link& link) const;
    friend constexpr bool operator==(ScalerSlotMode, ScalerSlotMode) = default;
};

}
