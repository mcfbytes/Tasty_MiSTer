// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/reg_field.h"
#include "infra/endian.h"

namespace mister::cores::x86 {

struct DosStamp {
    using Field = hal::RegField<std::uint32_t>;

    static constexpr Field kSeconds{0, 0x1F};
    static constexpr Field kMinutes{5, 0x3F};
    static constexpr Field kHours{11, 0x1F};
    static constexpr Field kDay{16, 0x1F};
    static constexpr Field kMonth{21, 0x0F};
    static constexpr Field kYear{25, 0x7F};

    static constexpr std::uint32_t kSecondsStep = 2;
    static constexpr int kTmYearBase = 80;

    static_assert(
        kHours.get(0x576E'6CB5u) == 13
        && kMinutes.get(0x576E'6CB5u) == 37
        && kSeconds.get(0x576E'6CB5u) == 21
        && kDay.get(0x576E'6CB5u) == 14
        && kMonth.get(0x576E'6CB5u) == 11
        && kYear.get(0x576E'6CB5u) == 43
    );

    std::uint32_t v = 0;

    [[nodiscard]] static DosStamp of(std::chrono::sys_seconds when) noexcept;

    constexpr void store(std::span<std::byte, 4> out) const noexcept {
        infra::store_le<std::uint32_t>(out, v);
    }

    friend constexpr bool operator==(DosStamp, DosStamp) = default;
};

}
