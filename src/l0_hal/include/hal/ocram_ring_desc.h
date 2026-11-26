// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/ocram_types.h"
#include "os/types.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::hal::ocram {

class RingDesc {
public:
    enum class Kind : std::uint8_t { Pad = 1, Sector = 2, Osd = 3, Evt = 4, Lane = 5 };

    RingDesc() = default;

    [[nodiscard]] constexpr RingDesc placed(Ring r) const noexcept {
        RingDesc desc = *this;
        desc.ring_ = r;
        return desc;
    }

    [[nodiscard]] static constexpr std::optional<RingDesc> of(std::uint32_t word) noexcept {
        const std::uint32_t kind_bits = (word & kKindMask) >> kKindShift;
        if (kind_bits == 0u || kind_bits > kLaneKind)
            return std::nullopt;

        if ((word & kReservedMask) != 0u)
            return std::nullopt;
        const RingSizeLog2 log2 = RingSizeLog2::from_field((word & kLog2Mask) >> kLog2Shift);
        if (!log2.at_least(RingSizeLog2::from_field(RingSizeLog2::kMin))
            || !log2.at_most(RingSizeLog2::from_field(RingSizeLog2::kMax))) {
            return std::nullopt;
        }
        const std::uint16_t offset_words = static_cast<std::uint16_t>(word & kOffsetMask);
        const std::uint32_t ordinal = (word & kLineMask) >> kLineShift;
        const std::optional<os::UioLine> line = ordinal == 0u
            ? std::nullopt
            : std::optional{os::pool_line_at(ordinal, kLinePoolFirst)};
        const Kind kind = static_cast<Kind>(kind_bits);
        const RingDesc desc{kind, base_ring_(kind), log2, Words{offset_words}, line};
        if (desc.byte_offset() < kRingFirst)
            return std::nullopt;
        if (kMapEnd < desc.end())
            return std::nullopt;
        return desc;
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }

    [[nodiscard]] constexpr Ring ring() const noexcept { return ring_; }

    [[nodiscard]] constexpr RingSizeLog2 log2_words() const noexcept { return log2_; }

    [[nodiscard]] constexpr std::optional<os::UioLine> line() const noexcept { return line_; }

    [[nodiscard]] constexpr Words words() const noexcept { return log2_.words(); }

    [[nodiscard]] constexpr infra::ByteOffset byte_offset() const noexcept {
        return infra::ByteOffset::of(offset_words_.bytes());
    }

    [[nodiscard]] constexpr infra::ByteOffset offset_of(Words slot) const noexcept {
        return byte_offset() + slot.bytes();
    }

    [[nodiscard]] constexpr infra::ByteOffset end() const noexcept {
        return byte_offset() + words().bytes();
    }

    [[nodiscard]] constexpr bool fits_in(infra::ByteOffset region_end) const noexcept {
        return end() <= region_end;
    }

    friend constexpr bool operator==(RingDesc, RingDesc) = default;

private:
    static constexpr std::uint32_t kKindMask = 0xF800'0000u;
    static constexpr unsigned kKindShift = 27;
    static constexpr std::uint32_t kLaneKind = 5u;
    static constexpr std::uint32_t kLineMask = 0x07C0'0000u;
    static constexpr unsigned kLineShift = 22;
    static constexpr std::uint32_t kLinePoolFirst = 1u;
    static constexpr std::uint32_t kLog2Mask = 0x003C'0000u;
    static constexpr unsigned kLog2Shift = 18;
    static constexpr std::uint32_t kReservedMask = 0x0003'C000u;
    static constexpr std::uint32_t kOffsetMask = 0x0000'3FFFu;

    [[nodiscard]] static constexpr Ring base_ring_(Kind k) noexcept {
        switch (k) {
            case Kind::Pad:
                return Ring::Pad;
            case Kind::Sector:
                return Ring::Sector;
            case Kind::Osd:
                return Ring::Osd;
            case Kind::Evt:
                return Ring::Evt;
            case Kind::Lane:
                return Ring::Lane;
        }
        return Ring::Osd;
    }

    constexpr RingDesc(
        Kind k,
        Ring r,
        RingSizeLog2 log2,
        Words offset_words,
        std::optional<os::UioLine> line
    ) noexcept
        : kind_(k),
          ring_(r),
          log2_(log2),
          offset_words_(offset_words),
          line_(line) {}

    Kind kind_ = Kind::Osd;
    Ring ring_ = Ring::Osd;
    RingSizeLog2 log2_ = RingSizeLog2::from_field(RingSizeLog2::kMin);
    Words offset_words_{};
    std::optional<os::UioLine> line_{};
};

static_assert(sizeof(RingDesc) <= 8);
static_assert(!RingDesc::of(0u).has_value(), "an absent ring decodes as none");
static_assert(!RingDesc::of(0xFFFFFFFFu).has_value(), "no ring escapes the map");
static_assert(
    RingDesc::of((3u << 27) | (11u << 18) | 0x800u).has_value(),
    "the design's OSD descriptor {OSD, 2,048 words at 0x2000} is valid"
);
static_assert(
    !RingDesc::of((3u << 27) | (11u << 18) | (1u << 14) | 0x800u).has_value(),
    "a descriptor with a reserved bit set is refused"
);
static_assert(
    !RingDesc::of((3u << 27) | (11u << 18) | 0x800u)->line().has_value(),
    "a descriptor with line 0 declares no line"
);
static_assert(
    RingDesc::of((4u << 27) | (4u << 22) | (11u << 18) | 0x1000u)->line()
        == os::pool_line_at(4u, 1u),
    "EVT's line ordinal 4 is pool line 3, /dev/mister/doorbell/4's"
);
static_assert(
    RingDesc::of((2u << 27) | (10u << 18) | 0x1800u)->placed(Ring::Pad).ring() == Ring::Pad,
    "kind SECTOR in C_RING[0] is the mount ring, ring 0, never ring 4"
);

}
