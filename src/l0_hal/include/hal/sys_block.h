// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/reg_access.h"
#include "hal/reg_field.h"

namespace mister::hal {

struct SysBlock {
    enum class Reg : std::uint32_t {
        Cause = 0x00,
        Stflg = 0x04,
        Status0 = 0x08,
        Status1 = 0x0C,
        Status2 = 0x10,
        Status3 = 0x14,
        Pend = 0x18,
        Id = 0x1C,
    };
    static constexpr std::size_t kSize = 0x20;

    static constexpr RegAccess kAccess = RegAccess::ReadWrite;
    static constexpr std::uint32_t kId = 0x32545645u;
    static constexpr std::uint32_t kSnapshotWords = 4;

    static constexpr std::uint32_t kCauseStatus = 1u << 0;

    enum Layout : std::uint32_t { kWordBytes = 4 };

    static constexpr Reg status(std::uint32_t i) noexcept {
        return static_cast<Reg>(static_cast<std::uint32_t>(Reg::Status0) + kWordBytes * i);
    }

    static constexpr RegField<std::uint32_t> kFlagField{0, 0x0F};
    static constexpr RegField<std::uint32_t> kMarkField{4, 0xFFF};
    static constexpr RegField<std::uint32_t> kSeqField{16, 0xFFFF};

    enum Mark : std::uint32_t { kMark = 0x0A };

    struct Stflg {
        std::uint16_t seq;
        std::uint8_t flag;

        [[nodiscard]] constexpr Stflg next() const noexcept {
            return Stflg{
                static_cast<std::uint16_t>((seq + 1u) & kSeqField.mask),
                static_cast<std::uint8_t>((flag + 1u) & kFlagField.mask)
            };
        }

        friend constexpr bool operator==(const Stflg&, const Stflg&) = default;
    };

    [[nodiscard]] static constexpr std::optional<Stflg> decode(std::uint32_t stflg) noexcept {
        if (kMarkField.get(stflg) != kMark)
            return std::nullopt;
        return Stflg{
            static_cast<std::uint16_t>(kSeqField.get(stflg)),
            static_cast<std::uint8_t>(kFlagField.get(stflg))
        };
    }
};

static_assert(
    static_cast<std::size_t>(SysBlock::status(SysBlock::kSnapshotWords - 1)) + 4 <= SysBlock::kSize
        && static_cast<std::size_t>(SysBlock::Reg::Id) + 4 == SysBlock::kSize,
    "lw_evt.sv's register map"
);
static_assert(SysBlock::decode(0x0001'00A1u) == SysBlock::Stflg{1, 1}, "STFLG {seq 1, A, flag 1}");
static_assert(!SysBlock::decode(0xDEADBEEFu), "the unmarked word decodes to nothing");
static_assert(
    SysBlock::Stflg{0xFFFF, 0x0F}.next() == SysBlock::Stflg{0, 0},
    "seq and flag wrap in their own fields"
);

}
