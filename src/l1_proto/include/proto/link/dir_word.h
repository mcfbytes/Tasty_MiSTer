// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "hal/reg_word.h"
#include "hal/types.h"
#include "proto/op_bitmap.h"
#include "proto/link/osd_fifo_depth.h"
#include "proto/link/osd_shape.h"
#include "proto/link/pad_shape.h"
#include "proto/types.h"

namespace mister::proto::link {

struct DirWord {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr DirWord of(hal::RegWord w) noexcept { return DirWord{w.bits()}; }

    [[nodiscard]] static constexpr OpBitmap op_bitmap(
        std::span<const DirWord, OpBitmap::kWordCount> words
    ) noexcept {
        OpBitmap set{};
        for (std::size_t k = 0; k < words.size(); ++k)
            set.v[k] = words[k].v;
        return set;
    }

    [[nodiscard]] constexpr std::uint16_t low16() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    [[nodiscard]] constexpr std::uint16_t high16() const noexcept {
        return static_cast<std::uint16_t>(v >> std::numeric_limits<std::uint16_t>::digits);
    }

    [[nodiscard]] constexpr std::uint8_t top_byte() const noexcept {
        return static_cast<std::uint8_t>(
            v
            >> (std::numeric_limits<std::uint32_t>::digits
                - std::numeric_limits<std::uint8_t>::digits)
        );
    }

    [[nodiscard]] constexpr std::uint8_t flags_byte() const noexcept {
        return static_cast<std::uint8_t>(
            (v >> std::numeric_limits<std::uint16_t>::digits)
            & std::numeric_limits<std::uint8_t>::max()
        );
    }

    [[nodiscard]] constexpr bool flags_bit(std::uint8_t bit) const noexcept {
        return (flags_byte() & bit) != 0;
    }

    [[nodiscard]] constexpr hal::LwOffset lw_offset() const noexcept { return hal::LwOffset{v}; }

    [[nodiscard]] constexpr SlotIndex hps_slot() const noexcept {
        return SlotIndex::truncating((v >> kHpsSlotShift) & kHpsSlotMask);
    }

    enum HpsSlotBits : std::uint32_t { kHpsSlotShift = 16, kHpsSlotMask = 0xF };

    [[nodiscard]] constexpr bool pad_link_arm_absent() const noexcept {
        return (v & kPadLinkArmAbsent) != 0u;
    }

    enum PadFlagBits : std::uint32_t { kPadLinkArmAbsent = 1 };

    [[nodiscard]] constexpr LaneCaps caps() const noexcept { return LaneCaps{v}; }

    [[nodiscard]] constexpr OsdShape osd_shape() const noexcept { return OsdShape{v}; }

    [[nodiscard]] constexpr OsdFifoDepth osd_fifo() const noexcept {
        return OsdFifoDepth{high16()};
    }

    [[nodiscard]] constexpr PadShape pad_shape() const noexcept { return PadShape{v}; }

    [[nodiscard]] constexpr os::PhysAddr phys_addr() const noexcept { return os::PhysAddr{v}; }

    [[nodiscard]] constexpr os::MapLength map_length() const noexcept { return os::MapLength{v}; }

    [[nodiscard]] constexpr IrqOrdinal irq_ordinal() const noexcept { return IrqOrdinal{v}; }

    [[nodiscard]] constexpr CauseBits cause_bits() const noexcept { return CauseBits{v}; }
};

}
