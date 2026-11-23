// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

#include "infra/affine.h"
#include "infra/indexed_array.h"
#include "proto/disk_register_count.h"

namespace mister::cores::x86 {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct CmosRegisterCount {
    std::uint8_t v = 0;
    using affine_count = CmosRegisterCount;
    friend constexpr bool operator==(CmosRegisterCount, CmosRegisterCount) = default;
    friend constexpr auto operator<=>(CmosRegisterCount, CmosRegisterCount) = default;
};

struct CmosRegister {
    std::uint8_t v = 0;
    using affine_count = CmosRegisterCount;
    using affine_index = CmosRegister;

    [[nodiscard]] constexpr proto::DiskRegisterCount offset() const noexcept { return {v}; }

    friend constexpr bool operator==(CmosRegister, CmosRegister) = default;
    friend constexpr auto operator<=>(CmosRegister, CmosRegister) = default;
};

inline constexpr std::size_t kX86CmosBytes = 128;
using X86CmosImage = infra::IndexedArray<std::byte, kX86CmosBytes, CmosRegister>;

inline constexpr CmosRegisterCount kCmosRegisters{128};
static_assert(kCmosRegisters == CmosRegisterCount{kX86CmosBytes});

namespace cmos {
inline constexpr CmosRegister kSeconds{0x00};
inline constexpr CmosRegister kMinutes{0x02};
inline constexpr CmosRegister kHours{0x04};
inline constexpr CmosRegister kAlarmHours{0x05};
inline constexpr CmosRegister kWeekday{0x06};
inline constexpr CmosRegister kMonthDay{0x07};
inline constexpr CmosRegister kMonth{0x08};
inline constexpr CmosRegister kYear{0x09};
inline constexpr CmosRegister kRegA{0x0A};
inline constexpr CmosRegister kRegB{0x0B};
inline constexpr CmosRegister kRegD{0x0D};
inline constexpr CmosRegister kFloppyTypes{0x10};
inline constexpr CmosRegister kHddTypes{0x12};
inline constexpr CmosRegister kEquipment{0x14};
inline constexpr CmosRegister kBaseMemLow{0x15};
inline constexpr CmosRegister kBaseMemHigh{0x16};
inline constexpr CmosRegister kExtMemHigh{0x18};
inline constexpr CmosRegister kHdd0Ext{0x19};
inline constexpr CmosRegister kHdd1Ext{0x1A};
inline constexpr CmosRegister kBootSequence{0x2D};
inline constexpr CmosRegister kChecksumHigh{0x2E};
inline constexpr CmosRegister kChecksumLow{0x2F};
inline constexpr CmosRegister kExtMemHigh2{0x31};
inline constexpr CmosRegister kCentury{0x32};
inline constexpr CmosRegister kHighMemLow{0x34};
inline constexpr CmosRegister kHighMemHigh{0x35};
inline constexpr CmosRegister kPs2Century{0x37};
inline constexpr CmosRegister kAtaTranslation{0x39};
inline constexpr CmosRegister kElToritoBoot{0x3D};

inline constexpr CmosRegister kSumFirst{0x10};
inline constexpr CmosRegister kSumLast{0x2D};
}

}
