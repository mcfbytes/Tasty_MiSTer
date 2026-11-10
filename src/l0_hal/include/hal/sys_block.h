// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mister::hal {

struct SysBlock {
    enum class Reg : std::uint32_t {
        Cause = 0x00,
        Stflg = 0x04,
        Status0 = 0x08,
        Status1 = 0x0C,
        Status2 = 0x10,
        Status3 = 0x14,
        Id = 0x1C,
    };
    static constexpr std::size_t kSize = 0x20;
    static constexpr std::uint32_t kId = 0x31535453u;
    static constexpr std::uint32_t kSnapshotWords = 4;

    static constexpr std::uint32_t kCauseStatus = 1u << 0;

    enum Layout : std::uint32_t { kWordBytes = 4 };

    static constexpr Reg status(std::uint32_t i) noexcept {
        return static_cast<Reg>(static_cast<std::uint32_t>(Reg::Status0) + kWordBytes * i);
    }

    enum StflgBits : std::uint32_t {
        kFlagMask = 0x0F,
        kMarkMask = 0xFFF0,
        kMark = 0xA0,
        kSeqShift = 16
    };

    struct Stflg {
        std::uint16_t seq;
        std::uint8_t flag;
        friend constexpr bool operator==(const Stflg&, const Stflg&) = default;
    };

    [[nodiscard]] static constexpr std::optional<Stflg> decode(std::uint32_t stflg) noexcept {
        if ((stflg & kMarkMask) != kMark)
            return std::nullopt;
        return Stflg{
            static_cast<std::uint16_t>(stflg >> kSeqShift),
            static_cast<std::uint8_t>(stflg & kFlagMask)
        };
    }
};

static_assert(
    static_cast<std::size_t>(SysBlock::status(SysBlock::kSnapshotWords - 1)) + 4 <= SysBlock::kSize
        && static_cast<std::size_t>(SysBlock::Reg::Id) + 4 == SysBlock::kSize,
    "lw_sys_status.sv's register map"
);
static_assert(SysBlock::decode(0x0001'00A1u) == SysBlock::Stflg{1, 1}, "STFLG {seq 1, A, flag 1}");
static_assert(!SysBlock::decode(0xDEADBEEFu), "the unmarked word decodes to nothing");

}
