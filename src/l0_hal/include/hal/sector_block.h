// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::hal {

struct SectorBlock {
    enum class Reg : std::uint32_t {
        Cause = 0x00,
        Req = 0x04,
        Lba = 0x08,
        Begin = 0x0C,
        Ctrl = 0x10,
        Status = 0x14,
        Accept = 0x18,
        Drops = 0x1C,
        Counts = 0x20,
        Gen = 0x24,
        TstampReq = 0x28,
        TstampDone = 0x2C,
        Cycles = 0x30,
        Sum = 0x34,
        Id = 0x38,
    };
    static constexpr std::size_t kSize = 0x2000;
    static constexpr std::uint32_t kId = 0x31434553u;
    static constexpr std::uint32_t kIdV2 = 0x32434553u;
    static constexpr std::uint32_t kDataBase = 0x1000;

    enum Layout : std::uint32_t {
        kWordBytes = 4,
        kCardSlots = 4,
        kCtrlCardShift = 4,
        kBeginSlotBase = 0x40,
        kWackSlotBase = 0x50,
        kLbaSlotBase = 0x60,
    };

    static constexpr std::uint32_t kMaxFrameWords = (kSize - kDataBase) / kWordBytes;

    static constexpr std::uint32_t kReqPending = 1u << 31;
    static constexpr std::uint32_t kCtrlEnable = 1u << 0;
    static constexpr std::uint32_t kCtrlAbort = 1u << 1;

    static constexpr std::uint32_t ctrl_enable_slot(std::uint8_t slot) noexcept {
        return slot < kCardSlots ? 1u << (kCtrlCardShift + slot) : 0u;
    }

    static constexpr Reg begin_slot(std::uint8_t s) noexcept {
        return static_cast<Reg>(kBeginSlotBase + kWordBytes * (s & (kCardSlots - 1u)));
    }

    static constexpr Reg wack_slot(std::uint8_t s) noexcept {
        return static_cast<Reg>(kWackSlotBase + kWordBytes * (s & (kCardSlots - 1u)));
    }

    static constexpr Reg lba_slot(std::uint8_t s) noexcept {
        return static_cast<Reg>(kLbaSlotBase + kWordBytes * (s & (kCardSlots - 1u)));
    }

    struct SdStat {
        bool valid = false;
        std::uint8_t count = 0;
        std::uint8_t blksz = 0;
        std::uint8_t slot = 0;
        bool wr = false;
        bool rd = false;
    };

    enum SdBits : std::uint32_t {
        kSdValid = 0x8000,
        kSdCountShift = 9,
        kSdCountMask = 0x3F,
        kSdBlkszShift = 6,
        kSdBlkszMask = 7,
        kSdSlotShift = 2,
        kSdSlotMask = 0xF,
        kSdOpMask = 3,
        kSdWr = 2,
        kSdRd = 1,
    };

    static constexpr SdStat sdstat(std::uint32_t w) noexcept {
        return SdStat{
            .valid = (w & kSdValid) != 0u,
            .count = static_cast<std::uint8_t>((w >> kSdCountShift) & kSdCountMask),
            .blksz = static_cast<std::uint8_t>((w >> kSdBlkszShift) & kSdBlkszMask),
            .slot = static_cast<std::uint8_t>((w >> kSdSlotShift) & kSdSlotMask),
            .wr = (w & kSdWr) != 0u,
            .rd = (w & kSdRd) != 0u
        };
    }

    static constexpr bool sdstat_reads(std::uint32_t w, std::uint8_t slot) noexcept {
        return (w & kSdValid) != 0u
            && ((w >> kSdSlotShift) & kSdSlotMask) == (slot & kSdSlotMask)
            && (w & kSdOpMask) == kSdRd
            && ((w >> kSdCountShift) & kSdCountMask) == 0u;
    }

    static constexpr Reg data(std::uint32_t i) noexcept {
        return static_cast<Reg>(kDataBase + kWordBytes * i);
    }

    struct Accept {
        bool ok = false;
        std::uint16_t drops = 0;
        std::uint16_t first_drop = 0;
    };

    enum AcceptBits : std::uint32_t {
        kAcceptOkShift = 31,
        kAcceptDropsShift = 16,
        kAcceptDropMask = 0x3FF
    };

    static constexpr Accept accept(std::uint32_t w) noexcept {
        return Accept{
            .ok = (w >> kAcceptOkShift) != 0u,
            .drops = static_cast<std::uint16_t>((w >> kAcceptDropsShift) & kAcceptDropMask),
            .first_drop = static_cast<std::uint16_t>(w & kAcceptDropMask)
        };
    }
};

static_assert(
    static_cast<std::size_t>(SectorBlock::data(SectorBlock::kMaxFrameWords - 1))
        == SectorBlock::kSize - 4,
    "every DATA index lands inside the block"
);
static_assert(SectorBlock::kMaxFrameWords == 1024, "the 10-bit index the FIFO entry carries");
static_assert(
    static_cast<std::uint32_t>(SectorBlock::begin_slot(2)) == 0x48
        && static_cast<std::uint32_t>(SectorBlock::wack_slot(3)) == 0x5C
        && static_cast<std::uint32_t>(SectorBlock::lba_slot(2)) == 0x68
        && SectorBlock::ctrl_enable_slot(2) == 0x40u
        && SectorBlock::ctrl_enable_slot(3) == 0x80u,
    "lw_sector.sv's SEC2 offsets and per-slot enables"
);
static_assert(
    SectorBlock::sdstat(0x80CAu).slot == 2
        && SectorBlock::sdstat(0x80CAu).wr
        && !SectorBlock::sdstat(0x80CAu).rd
        && SectorBlock::sdstat(0x80CAu).blksz == 3,
    "PSX card 1's one-block write: slot 2, 1 KB"
);
static_assert(
    SectorBlock::sdstat_reads(0x80C5u, 1)
        && !SectorBlock::sdstat_reads(0x80C5u, 0)
        && !SectorBlock::sdstat_reads(0x80C6u, 1)
        && !SectorBlock::sdstat_reads(0x82C5u, 1),
    "PSX slot 1's one-block read, and nothing else"
);

}
