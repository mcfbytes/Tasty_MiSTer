// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hal/reg_word.h"
#include "hal/types.h"

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

    static constexpr Reg data(std::uint32_t i) noexcept {
        return static_cast<Reg>(kDataBase + kWordBytes * i);
    }

    [[nodiscard]] static constexpr bool pending(RegWord w) noexcept {
        return w.any_of(RegWord{kReqPending});
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

    struct Counts {
        EventCount8 rejected{};
        EventCount8 skipped{};
        EventCount8 aborted{};
        EventCount8 reset_fills{};
    };

    enum CountsBits : std::uint32_t {
        kCountsRejectedShift = 24,
        kCountsSkippedShift = 16,
        kCountsAbortedShift = 8,
    };

    static constexpr Counts counts(std::uint32_t w) noexcept {
        return Counts{
            .rejected = EventCount8::of_lane(w, kCountsRejectedShift),
            .skipped = EventCount8::of_lane(w, kCountsSkippedShift),
            .aborted = EventCount8::of_lane(w, kCountsAbortedShift),
            .reset_fills = EventCount8::of_lane(w, 0)
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
    SectorBlock::counts(0x04030201u).rejected == EventCount8{4}
        && SectorBlock::counts(0x04030201u).skipped == EventCount8{3}
        && SectorBlock::counts(0x04030201u).aborted == EventCount8{2}
        && SectorBlock::counts(0x04030201u).reset_fills == EventCount8{1},
    "COUNTS: one event byte per field, high to low"
);

}
