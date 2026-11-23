// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "cores/x86/cmos_checksum.h"
#include "cores/x86/cmos_drive_type.h"
#include "cores/x86/cmos_register.h"
#include "cores/x86/x86_cmos_inputs.h"
#include "proto/rtc_calendar.h"

namespace mister::cores::x86 {

[[nodiscard]] constexpr X86CmosImage compose_x86_cmos(const X86CmosInputs& in) noexcept {

    constexpr proto::CalendarField kCmosFloorYear{117};
    constexpr proto::CalendarField kCmosFloorYy{17};
    constexpr std::byte kAlarmHour{0x12};
    constexpr std::byte kRegAValue{0x26};
    constexpr std::byte kRegBValue{0x02};
    constexpr std::byte kRegDValue{0x80};
    constexpr std::byte kEquipmentBits{0x4D};
    constexpr std::byte kBaseMem640Low{0x80};
    constexpr std::byte kBaseMem640High{0x02};
    constexpr std::byte kMem16Above1M{0x3C};
    constexpr std::byte kMem256Above1M{0xFC};
    constexpr std::byte kMem256Above16MLow{0x80};
    constexpr std::byte kMem256Above16MHigh{0x0E};
    constexpr std::byte kCentury20{0x20};
    constexpr std::byte kBootFloppyFirst{0x20};
    constexpr std::byte kElToritoFloppy{0x21};
    constexpr std::byte kElToritoDisk{0x02};

    constexpr std::byte kAllLba{0x55};

    X86CmosImage c{};
    const proto::RtcCalendar& cal = in.calendar;
    c[cmos::kSeconds] = cal.second.bcd().byte();
    c[cmos::kMinutes] = cal.minute.bcd().byte();
    c[cmos::kHours] = cal.hour.bcd().byte();
    c[cmos::kAlarmHours] = kAlarmHour;

    c[cmos::kWeekday] = cal.weekday_from_one.byte();
    c[cmos::kMonthDay] = cal.month_day.bcd().byte();
    c[cmos::kMonth] = cal.month.bcd().byte();
    c[cmos::kYear] = (cal.year < kCmosFloorYear ? kCmosFloorYy : cal.years_since_2000)
                         .bcd_unmasked();
    c[cmos::kRegA] = kRegAValue;
    c[cmos::kRegB] = kRegBValue;
    c[cmos::kRegD] = kRegDValue;
    c[cmos::
          kFloppyTypes] = CmosDrivePair{.first = in.fdd0.cmos_type(), .second = in.fdd1.cmos_type()}
                              .byte();
    c[cmos::kHddTypes] =
        CmosDrivePair{
            .first = in.hdd0_named ? CmosDriveType::kNamedHdd : CmosDriveType::kNone,
            .second = in.hdd1_named ? CmosDriveType::kNamedHdd : CmosDriveType::kNone
        }
            .byte();
    c[cmos::kEquipment] = kEquipmentBits;
    c[cmos::kBaseMemLow] = kBaseMem640Low;
    c[cmos::kBaseMemHigh] = kBaseMem640High;
    const bool small = in.ram == X86RamSize::Mb16;
    c[cmos::kExtMemHigh] = small ? kMem16Above1M : kMem256Above1M;
    c[cmos::kHdd0Ext] = (in.hdd0_named ? CmosDriveType::kExtendedHdd : CmosDriveType::kNone).byte();
    c[cmos::kHdd1Ext] = (in.hdd1_named ? CmosDriveType::kExtendedHdd : CmosDriveType::kNone).byte();
    const bool boots_floppy = in.fdd0.boots();
    c[cmos::kBootSequence] = boots_floppy ? kBootFloppyFirst : std::byte{};
    c[cmos::kExtMemHigh2] = small ? kMem16Above1M : kMem256Above1M;
    c[cmos::kCentury] = kCentury20;
    c[cmos::kHighMemLow] = small ? std::byte{} : kMem256Above16MLow;
    c[cmos::kHighMemHigh] = small ? std::byte{} : kMem256Above16MHigh;
    c[cmos::kPs2Century] = kCentury20;
    c[cmos::kAtaTranslation] = kAllLba;
    c[cmos::kElToritoBoot] = boots_floppy ? kElToritoFloppy : kElToritoDisk;

    CmosChecksum::of(c).store(c);
    return c;
}

}
