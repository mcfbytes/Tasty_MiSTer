// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "cores/x86/cmos_drive_type.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::x86 {

enum class FloppyType : std::uint8_t {
    None,
    K160,
    K180,
    K320,
    K360,
    K720,
    M1200,
    M1440,
    M1680,
    M2880,
};

struct FloppyFormat {
    FloppyType type = FloppyType::None;
    proto::BlockCount at_least{};
    svc::DiskGeometry shape{};
    CmosDriveType cmos{};
};

inline constexpr auto kFloppyFormats = std::to_array<FloppyFormat>({
    {.type = FloppyType::M2880,
     .at_least = proto::BlockCount{5760},
     .shape = {.cylinders = {80}, .heads = {2}, .sectors = {36}},
     .cmos = CmosDriveType::k2880K},
    {.type = FloppyType::M1680,
     .at_least = proto::BlockCount{3360},
     .shape = {.cylinders = {80}, .heads = {2}, .sectors = {21}},
     .cmos = CmosDriveType::k1440K},
    {.type = FloppyType::M1440,
     .at_least = proto::BlockCount{2880},
     .shape = {.cylinders = {80}, .heads = {2}, .sectors = {18}},
     .cmos = CmosDriveType::k1440K},
    {.type = FloppyType::M1200,
     .at_least = proto::BlockCount{2400},
     .shape = {.cylinders = {80}, .heads = {2}, .sectors = {15}},
     .cmos = CmosDriveType::k1200K},
    {.type = FloppyType::K720,
     .at_least = proto::BlockCount{1440},
     .shape = {.cylinders = {80}, .heads = {2}, .sectors = {9}},
     .cmos = CmosDriveType::k720K},
    {.type = FloppyType::K360,
     .at_least = proto::BlockCount{720},
     .shape = {.cylinders = {40}, .heads = {2}, .sectors = {9}},
     .cmos = CmosDriveType::k360K},
    {.type = FloppyType::K320,
     .at_least = proto::BlockCount{640},
     .shape = {.cylinders = {40}, .heads = {2}, .sectors = {8}},
     .cmos = CmosDriveType::k360K},
    {.type = FloppyType::K180,
     .at_least = proto::BlockCount{360},
     .shape = {.cylinders = {40}, .heads = {1}, .sectors = {9}},
     .cmos = CmosDriveType::k360K},
    {.type = FloppyType::K160,
     .at_least = proto::BlockCount{},
     .shape = {.cylinders = {40}, .heads = {1}, .sectors = {8}},
     .cmos = CmosDriveType::k360K},
    {.type = FloppyType::None, .shape = {}, .cmos = CmosDriveType::k360K},
});

}
