// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cmos_image.h"

#include <algorithm>

#include "proto/rtc_calendar.h"

namespace mister::cores {

CmosClock compose_rtc(const std::tm& local) noexcept {
    const proto::RtcCalendar cal = proto::RtcCalendar::of(local);
    CmosClock clock{};
    clock.year = cal.years_since_2000.byte();
    clock.registers[CmosClock::kSecondsAt] = cal.second.bcd().byte();
    clock.registers[CmosClock::kMinutesAt] = cal.minute.bcd().byte();
    clock.registers[CmosClock::kHoursAt] = cal.hour.bcd().byte();
    clock.registers[CmosClock::kDayYearAt] = cal.month_day.bcd_with<CmosClock::kYearBits>(
        cal.years_since_2000
    );
    clock.registers[CmosClock::kMonthWeekdayAt] = cal.month.bcd_with<CmosClock::kWeekdayBits>(
        cal.weekday
    );
    return clock;
}

bool patch_for_load(
    const CmosLayout& layout,
    const CmosClock& clock,
    std::span<std::byte> image
) noexcept {
    if (!layout.well_formed() || infra::ByteCount{image.size()} != layout.bytes)
        return false;
    std::ranges::copy(clock.registers, image.begin());
    infra::at_index(image, layout.year_at) = clock.year;
    infra::at_index(image, layout.century_at) = layout.century;
    return true;
}

}
