// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <span>

#include "cores/cmos_layout.h"
#include "hal/reg_field.h"
#include "infra/byte_offset.h"
#include "infra/indexed_array.h"

namespace mister::cores {

struct CmosClock {

    static constexpr infra::ByteOffset kSecondsAt{2};
    static constexpr infra::ByteOffset kMinutesAt{3};
    static constexpr infra::ByteOffset kHoursAt{4};
    static constexpr infra::ByteOffset kDayYearAt{5};
    static constexpr infra::ByteOffset kMonthWeekdayAt{6};

    static constexpr hal::RegField<std::uint8_t> kYearBits{6, 0x3};
    static constexpr hal::RegField<std::uint8_t> kWeekdayBits{5, 0x7};

    infra::IndexedArray<std::byte, CmosLayout::kClockBytes, infra::ByteOffset> registers{};
    std::byte year{};
};

[[nodiscard]] CmosClock compose_rtc(const std::tm& local) noexcept;

[[nodiscard]] bool patch_for_load(
    const CmosLayout& layout,
    const CmosClock& clock,
    std::span<std::byte> image
) noexcept;

}
