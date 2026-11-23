// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "cores/x86/bus_register_write.h"
#include "cores/x86/floppy_format.h"
#include "hal/types.h"
#include "proto/disk_address.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::x86 {

struct FloppyMedia {

    static constexpr proto::BlockCount kTooLarge{8000};
    static constexpr std::size_t kRegisterWrites = 8;
    using Registers = std::array<BusRegisterWrite, kRegisterWrites>;

    FloppyType type = FloppyType::M1440;
    bool present = false;
    bool writable = false;

    [[nodiscard]] static constexpr FloppyMedia of(bool opened, proto::BlockCount blocks) noexcept {
        if (!opened || blocks == proto::BlockCount{} || blocks >= kTooLarge)
            return FloppyMedia{};
        return FloppyMedia{.type = type_of_(blocks), .present = true};
    }

    [[nodiscard]] constexpr bool boots() const noexcept { return present; }

    [[nodiscard]] constexpr svc::DiskGeometry::Cylinders cylinders() const noexcept {
        return format_().shape.cylinders;
    }

    [[nodiscard]] constexpr svc::DiskGeometry::Sectors sectors_per_track() const noexcept {
        return format_().shape.sectors;
    }

    [[nodiscard]] constexpr svc::DiskGeometry::Heads heads() const noexcept {
        return format_().shape.heads;
    }

    [[nodiscard]] constexpr proto::BlockCount total_sectors() const noexcept {
        return format_().shape.blocks();
    }

    [[nodiscard]] constexpr CmosDriveType cmos_type() const noexcept { return format_().cmos; }

    [[nodiscard]] constexpr Registers insert(proto::DiskAddress base) const noexcept {
        return Registers{{
            {base + kPresentReg, present ? kSet : kClear},
            {base + kWriteProtectReg, present && writable ? kClear : kSet},
            {base + kCylindersReg, cylinders().word()},
            {base + kSectorsReg, sectors_per_track().word()},
            {base + kTotalReg, total_sectors().low_word()},
            {base + kHeadsReg, heads().word()},
            {base + kBaseLbaReg, kClear},
            {base + kMediaTypeReg, kClear},
        }};
    }

    [[nodiscard]] static constexpr BusRegisterWrite eject(proto::DiskAddress base) noexcept {
        return {base + kPresentReg, kClear};
    }

    friend constexpr bool operator==(const FloppyMedia&, const FloppyMedia&) = default;

private:
    static constexpr proto::DiskRegisterCount kPresentReg{0x0};
    static constexpr proto::DiskRegisterCount kWriteProtectReg{0x1};
    static constexpr proto::DiskRegisterCount kCylindersReg{0x2};
    static constexpr proto::DiskRegisterCount kSectorsReg{0x3};
    static constexpr proto::DiskRegisterCount kTotalReg{0x4};
    static constexpr proto::DiskRegisterCount kHeadsReg{0x5};
    static constexpr proto::DiskRegisterCount kBaseLbaReg{0x6};
    static constexpr proto::DiskRegisterCount kMediaTypeReg{0xC};
    static constexpr hal::SpiWord kSet{1};
    static constexpr hal::SpiWord kClear{0};

    [[nodiscard]] constexpr const FloppyFormat& format_() const noexcept {
        for (const FloppyFormat& format : kFloppyFormats)
            if (format.type == type)
                return format;
        return kFloppyFormats.back();
    }

    [[nodiscard]] static constexpr FloppyType type_of_(proto::BlockCount blocks) noexcept {
        for (const FloppyFormat& format : kFloppyFormats)
            if (format.type != FloppyType::None && blocks >= format.at_least)
                return format.type;
        return FloppyType::K160;
    }
};

static_assert(FloppyMedia::of(true, proto::BlockCount{2880}).type == FloppyType::M1440);
static_assert(
    FloppyMedia::of(true, proto::BlockCount{2880}).total_sectors() == proto::BlockCount{2880}
);
static_assert(
    !FloppyMedia::of(true, proto::BlockCount{8000}).present
    && FloppyMedia::of(true, proto::BlockCount{7999}).type == FloppyType::M2880
);
static_assert(FloppyMedia::of(true, proto::BlockCount{359}).type == FloppyType::K160);
static_assert(
    !FloppyMedia::of(true, proto::BlockCount{}).present,
    "an empty file is no media (x86.cpp:414-417)"
);
static_assert(
    FloppyMedia{}.cmos_type() == CmosDriveType::k1440K,
    "an empty drive reports 1.44 MB (x86.cpp:389)"
);

}
