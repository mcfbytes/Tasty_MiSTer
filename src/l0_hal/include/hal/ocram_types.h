// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/ocram_slot_binds.h"
#include "hal/reg_field.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"

namespace mister::hal::ocram {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

inline constexpr unsigned kHalfBits = 16;
inline constexpr std::uint32_t kHalfMask = 0x0000'FFFFu;

enum class Ring : std::uint8_t { Pad = 0, Lane = 1, Osd = 2, Evt = 3, Sector = 4, kCount = 5 };

struct Words {
    std::uint16_t v = 0;
    using affine_count = Words;

    static constexpr std::uint16_t kWordBytes = 4;
    static constexpr std::uint16_t kWordHalves = 2;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept {
        return infra::ByteCount{static_cast<std::size_t>(v) * kWordBytes};
    }

    template <class Range>
    [[nodiscard]] constexpr bool matches(const Range& r) const noexcept {
        return r.size() == v;
    }

    [[nodiscard]] constexpr std::uint16_t word() const noexcept { return v; }

    [[nodiscard]] static constexpr Words of(std::size_t n) noexcept {
        return Words{static_cast<std::uint16_t>(n)};
    }

    [[nodiscard]] constexpr Words half() const noexcept {
        return Words{static_cast<std::uint16_t>(v / 2u)};
    }

    [[nodiscard]] constexpr Words quarter() const noexcept { return half().half(); }

    [[nodiscard]] static constexpr Words carrying_halves(std::size_t n) noexcept {
        return of((n + kWordHalves - 1u) / kWordHalves);
    }

    [[nodiscard]] constexpr std::size_t halves() const noexcept {
        return std::size_t{v} * kWordHalves;
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> past_in(std::span<T> run) const noexcept {
        return run.subspan(std::min<std::size_t>(v, run.size()));
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> prefix_of(std::span<T> run) const noexcept {
        return run.first(std::min<std::size_t>(v, run.size()));
    }

    friend constexpr bool operator==(Words, Words) = default;
    friend constexpr auto operator<=>(Words, Words) = default;
};

struct RingIndex {
    std::uint16_t v = 0;
    using affine_count = Words;
    using affine_index = RingIndex;

    [[nodiscard]] constexpr Words slot_in(Words size) const noexcept {
        return Words{static_cast<std::uint16_t>(v & static_cast<std::uint16_t>(size.word() - 1u))};
    }

    [[nodiscard]] static constexpr RingIndex from_word(std::uint32_t word) noexcept {
        return RingIndex{static_cast<std::uint16_t>(word)};
    }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    friend constexpr bool operator==(RingIndex, RingIndex) = default;
    friend constexpr auto operator<=>(RingIndex, RingIndex) = default;
};

struct ImageRows {
    using affine_count = ImageRows;

    constexpr ImageRows() noexcept = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0u; }

    [[nodiscard]] static constexpr ImageRows from_high_half(std::uint32_t word) noexcept {
        return ImageRows{static_cast<std::uint16_t>(word >> kHalfBits)};
    }

    friend constexpr bool operator==(ImageRows, ImageRows) = default;
    friend constexpr auto operator<=>(ImageRows, ImageRows) = default;

    std::uint16_t v = 0;

private:
    constexpr explicit ImageRows(std::uint16_t count) noexcept : v(count) {}
};

struct ImageRowBytes {
    using affine_count = ImageRowBytes;

    constexpr ImageRowBytes() noexcept = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] static constexpr ImageRowBytes of_words(Words words) noexcept {
        return ImageRowBytes{static_cast<std::uint16_t>(words.size() * Words::kWordBytes)};
    }

    [[nodiscard]] static constexpr ImageRowBytes from_low_half(std::uint32_t word) noexcept {
        return ImageRowBytes{static_cast<std::uint16_t>(word & kHalfMask)};
    }

    friend constexpr bool operator==(ImageRowBytes, ImageRowBytes) = default;
    friend constexpr auto operator<=>(ImageRowBytes, ImageRowBytes) = default;

    std::uint16_t v = 0;

private:
    constexpr explicit ImageRowBytes(std::uint16_t bytes) noexcept : v(bytes) {}
};

struct RingSizeLog2 {
    using affine_count = RingSizeLog2;

    static constexpr std::uint8_t kMin = 2;
    static constexpr std::uint8_t kMax = 13;

    constexpr RingSizeLog2() noexcept = default;

    [[nodiscard]] static constexpr RingSizeLog2 from_field(std::uint32_t field) noexcept {
        return RingSizeLog2{static_cast<std::uint8_t>(field)};
    }

    [[nodiscard]] constexpr bool at_least(RingSizeLog2 lo) const noexcept { return lo.v <= v; }

    [[nodiscard]] constexpr bool at_most(RingSizeLog2 hi) const noexcept { return v <= hi.v; }

    [[nodiscard]] constexpr Words words() const noexcept {
        return Words{static_cast<std::uint16_t>(1u << v)};
    }

    friend constexpr bool operator==(RingSizeLog2, RingSizeLog2) = default;

    std::uint8_t v = 0;

private:
    constexpr explicit RingSizeLog2(std::uint8_t field) noexcept : v(field) {}
};

struct LayoutVersion {
    std::uint32_t v = 0;
    using affine_count = LayoutVersion;

    [[nodiscard]] static constexpr LayoutVersion of(std::uint32_t word) noexcept {
        return LayoutVersion{word};
    }

    [[nodiscard]] constexpr bool layout_1() const noexcept { return v == kLayout1; }

    friend constexpr bool operator==(LayoutVersion, LayoutVersion) = default;

    static constexpr std::uint32_t kLayout1 = 0x0001'0000u;
};

struct OcramCaps {
    std::uint32_t v = 0;
    using affine_count = OcramCaps;

    [[nodiscard]] static constexpr OcramCaps of(std::uint32_t word) noexcept {
        return OcramCaps{word};
    }

    [[nodiscard]] constexpr bool ring(Ring r) const noexcept {
        const std::uint32_t bit = ring_bit(r);
        return bit != 0u && (v & bit) != 0u;
    }

    [[nodiscard]] constexpr bool bell() const noexcept { return kBell.test(v); }

    [[nodiscard]] constexpr bool conf_str_record() const noexcept { return kConfStr.test(v); }

    [[nodiscard]] constexpr bool bind_record() const noexcept { return kBindRecord.test(v); }

    [[nodiscard]] constexpr bool events() const noexcept { return kEvents.test(v); }

    [[nodiscard]] constexpr bool captures() const noexcept { return kCaptures.test(v); }

    [[nodiscard]] constexpr bool spi_blk_removed() const noexcept { return kSpiBlkOff.test(v); }

    [[nodiscard]] constexpr bool spi_osd_removed() const noexcept { return kSpiOsdOff.test(v); }

    [[nodiscard]] constexpr bool spi_evt_removed() const noexcept { return kSpiEvtOff.test(v); }

    friend constexpr bool operator==(OcramCaps, OcramCaps) = default;

    static constexpr RegField<std::uint32_t> kBell{8, 1};
    static constexpr RegField<std::uint32_t> kConfStr{9, 1};
    static constexpr RegField<std::uint32_t> kBindRecord{10, 1};
    static constexpr RegField<std::uint32_t> kEvents{11, 1};
    static constexpr RegField<std::uint32_t> kCaptures{12, 1};
    static constexpr RegField<std::uint32_t> kSpiBlkOff{16, 1};
    static constexpr RegField<std::uint32_t> kSpiOsdOff{17, 1};
    static constexpr RegField<std::uint32_t> kSpiEvtOff{18, 1};

private:
    friend class LineSet;

    struct Bits {
        static constexpr std::uint32_t kPad = 1u << 0;
        static constexpr std::uint32_t kLane = 1u << 1;
        static constexpr std::uint32_t kOsd = 1u << 2;
        static constexpr std::uint32_t kEvt = 1u << 3;
        static constexpr std::uint32_t kSector = 1u << 4;
    };

    [[nodiscard]] static constexpr std::uint32_t ring_bit(Ring r) noexcept {
        switch (r) {
            case Ring::Pad:
                return Bits::kPad;
            case Ring::Lane:
                return Bits::kLane;
            case Ring::Osd:
                return Bits::kOsd;
            case Ring::Evt:
                return Bits::kEvt;
            case Ring::Sector:
                return Bits::kSector;
            case Ring::kCount:
                break;
        }
        return 0u;
    }
};

struct BindSet {
    std::uint32_t v = 0;
    using affine_count = BindSet;

    [[nodiscard]] static constexpr BindSet of(std::uint32_t word) noexcept { return BindSet{word}; }

    [[nodiscard]] constexpr bool ring(Ring r) const noexcept {
        const std::uint32_t bit = ring_bit(r);
        return bit != 0u && (v & bit) != 0u;
    }

    [[nodiscard]] constexpr BindSet with(Ring r) const noexcept { return BindSet{v | ring_bit(r)}; }

    [[nodiscard]] constexpr std::uint32_t word() const noexcept { return v; }

    [[nodiscard]] constexpr bool sector_mux() const noexcept { return (v & kSectorMux) != 0u; }

    [[nodiscard]] constexpr SlotBinds slots() const noexcept { return SlotBinds{v}; }

    friend constexpr bool operator==(BindSet, BindSet) = default;

    static constexpr std::uint32_t kSectorMux = 1u << 0;

    static constexpr std::uint32_t kEvtBit = 1u << 8;
    static constexpr std::uint32_t kLaneBit = 1u << 9;
    static constexpr std::uint32_t kPadBit = 1u << 10;
    static constexpr std::uint32_t kOsdBit = 1u << 11;

private:
    [[nodiscard]] static constexpr std::uint32_t ring_bit(Ring r) noexcept {
        switch (r) {
            case Ring::Evt:
                return kEvtBit;
            case Ring::Lane:
                return kLaneBit;
            case Ring::Pad:
                return kPadBit;
            case Ring::Osd:
                return kOsdBit;
            case Ring::Sector:
            case Ring::kCount:
                break;
        }
        return 0u;
    }
};

namespace control {
inline constexpr infra::ByteOffset kBase{0};
inline constexpr infra::ByteCount kSpan{0x0100};
inline constexpr infra::ByteOffset kSpanEnd{0x0100};
inline constexpr infra::ByteOffset kHHello{0x00};
inline constexpr infra::ByteOffset kHBind{0x04};
inline constexpr infra::ByteOffset kHLine{0x08};
inline constexpr infra::ByteOffset kHSector{0x0C};
inline constexpr infra::ByteOffset kHIdx{0x10};
inline constexpr infra::ByteOffset kHMark{0x30};
inline constexpr infra::ByteOffset kCHello{0x80};
inline constexpr infra::ByteOffset kCVersion{0x84};
inline constexpr infra::ByteOffset kCCaps{0x88};
inline constexpr infra::ByteOffset kCState{0x8C};
inline constexpr infra::ByteOffset kCIdx{0x90};
inline constexpr infra::ByteOffset kCBind{0xA4};
inline constexpr infra::ByteOffset kCRing{0xB0};
inline constexpr infra::ByteOffset kCK{0xD0};
inline constexpr infra::ByteCount kOsdKWord{8 * Words::kWordBytes};
inline constexpr infra::ByteCount kSectorK2Word{2 * Words::kWordBytes};
inline constexpr infra::ByteCount kSectorK4Word{4 * Words::kWordBytes};
inline constexpr infra::ByteCount kLaneK9Word{9 * Words::kWordBytes};
inline constexpr infra::ByteCount kMount0KWord{10 * Words::kWordBytes};
inline constexpr infra::ByteCount kMount1KWord{11 * Words::kWordBytes};
inline constexpr infra::ByteCount kStoreBytes{8};
}

namespace ring_word_offset {
inline constexpr infra::ByteCount kPad{0};
inline constexpr infra::ByteCount kLane{4};
inline constexpr infra::ByteCount kOsd{8};
inline constexpr infra::ByteCount kEvt{12};
inline constexpr infra::ByteCount kSector{16};
}

class PerRingWord {
public:
    [[nodiscard]] static constexpr std::optional<PerRingWord> of(Ring r) noexcept {
        switch (r) {
            case Ring::Pad:
                return PerRingWord{ring_word_offset::kPad};
            case Ring::Lane:
                return PerRingWord{ring_word_offset::kLane};
            case Ring::Osd:
                return PerRingWord{ring_word_offset::kOsd};
            case Ring::Evt:
                return PerRingWord{ring_word_offset::kEvt};
            case Ring::Sector:
                return PerRingWord{ring_word_offset::kSector};
            case Ring::kCount:
                break;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr infra::ByteOffset at(infra::ByteOffset base) const noexcept {
        return base + offset_;
    }

private:
    explicit constexpr PerRingWord(infra::ByteCount offset) noexcept : offset_(offset) {}

    infra::ByteCount offset_{};
};

inline constexpr infra::ByteCount kMapSpan{0xF000};
inline constexpr infra::ByteOffset kMapEnd{0xF000};
inline constexpr infra::ByteOffset kRingFirst{0x0100};

static_assert(sizeof(Words) == 2 && sizeof(RingIndex) == 2);
static_assert(infra::affine::Position<RingIndex> && infra::affine::Index<RingIndex>);
static_assert(infra::affine::Count<Words>);

}
