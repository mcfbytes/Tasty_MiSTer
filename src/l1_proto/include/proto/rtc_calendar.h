// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include "hal/reg_field.h"
#include "proto/types.h"

namespace mister::proto {

struct CalendarField {
    int v = 0;

    [[nodiscard]] constexpr PackedBcd bcd() const noexcept {
        return PackedBcd::of(static_cast<std::uint8_t>(v)).value_or(PackedBcd{});
    }

    [[nodiscard]] constexpr std::byte bcd_unmasked() const noexcept {
        const auto u = static_cast<std::uint32_t>(v);
        return std::byte{static_cast<std::uint8_t>(
            ((u / PackedBcd::kRadix) << PackedBcd::kNibbleBits) + (u % PackedBcd::kRadix)
        )};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept {
        return std::byte{static_cast<std::uint8_t>(v)};
    }

    template <hal::RegField<std::uint8_t> Field>
    [[nodiscard]] constexpr std::byte bcd_with(CalendarField above) const noexcept {
        const std::uint8_t at = std::to_integer<std::uint8_t>(bcd().byte());
        const std::uint8_t low = std::to_integer<std::uint8_t>(above.byte());
        return std::byte{Field.set(at, hal::RegField<std::uint8_t>::Value{low})};
    }

    friend constexpr bool operator==(CalendarField, CalendarField) = default;
    friend constexpr auto operator<=>(CalendarField, CalendarField) = default;
};

struct RtcCalendar {
    CalendarField second{};
    CalendarField minute{};
    CalendarField hour{};
    CalendarField month_day{};
    CalendarField month{};
    CalendarField weekday{};
    CalendarField weekday_from_one{};
    CalendarField year{};
    CalendarField years_since_2000{};

    [[nodiscard]] static constexpr RtcCalendar of(const std::tm& local) noexcept {
        constexpr int kTmYearOf2000 = 100;
        return RtcCalendar{
            .second = CalendarField{local.tm_sec},
            .minute = CalendarField{local.tm_min},
            .hour = CalendarField{local.tm_hour},
            .month_day = CalendarField{local.tm_mday},
            .month = CalendarField{local.tm_mon + 1},
            .weekday = CalendarField{local.tm_wday},
            .weekday_from_one = CalendarField{local.tm_wday + 1},
            .year = CalendarField{local.tm_year},
            .years_since_2000 = CalendarField{local.tm_year - kTmYearOf2000},
        };
    }

    friend constexpr bool operator==(RtcCalendar, RtcCalendar) = default;
};

}
