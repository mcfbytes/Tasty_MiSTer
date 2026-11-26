// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_field.h"
#include "hal/types.h"
#include "proto/types.h"

namespace mister::cores::x86 {

struct FdcSectorWord {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr FdcSectorWord of(hal::LinkWord w) noexcept {
        return w.decoded([](std::uint16_t bits) { return FdcSectorWord{bits}; });
    }

    [[nodiscard]] constexpr proto::DiskUnit drive() const noexcept {
        return proto::DiskUnit{static_cast<std::uint8_t>(kDriveField.get(v))};
    }

    [[nodiscard]] constexpr proto::Lba lba() const noexcept { return proto::Lba{kLbaField.get(v)}; }

    friend constexpr bool operator==(FdcSectorWord, FdcSectorWord) = default;

private:
    static constexpr hal::RegField<std::uint16_t> kDriveField{15, 0x1};
    static constexpr hal::RegField<std::uint16_t> kLbaField{0, 0x7FFF};
};

}
