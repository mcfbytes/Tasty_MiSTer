// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include "infra/affine.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"
#include "hal/types.h"
#include "infra/seat.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct SlotIndex {
    std::uint8_t v = 0;

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr SlotIndex of(I i) noexcept {
        return SlotIndex{static_cast<std::uint8_t>(i)};
    }

    template <std::size_t Count>
    [[nodiscard]] constexpr bool below() const noexcept {
        return v < Count;
    }

    friend constexpr bool operator==(SlotIndex, SlotIndex) = default;
    friend constexpr auto operator<=>(SlotIndex, SlotIndex) = default;
};

struct IoIndex {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SlotIndex block_slot() const noexcept { return SlotIndex{v}; }

    template <std::size_t Count>
    [[nodiscard]] constexpr bool below() const noexcept {
        return v < Count;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(IoIndex, IoIndex) = default;
    friend constexpr auto operator<=>(IoIndex, IoIndex) = default;
};

struct FileSlotDigit {
    std::uint8_t v = 0;
    friend constexpr auto operator<=>(FileSlotDigit, FileSlotDigit) = default;

    static constexpr std::uint32_t kMax = 15;

    [[nodiscard]] static constexpr std::optional<FileSlotDigit> from(std::uint32_t n) noexcept {
        if (n > kMax)
            return std::nullopt;
        return FileSlotDigit{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr IoIndex io_index() const noexcept { return IoIndex{v}; }

    [[nodiscard]] constexpr SlotIndex slot() const noexcept { return SlotIndex{v}; }

    [[nodiscard]] static constexpr FileSlotDigit or_zero(std::optional<FileSlotDigit> d) noexcept {
        return d.value_or(FileSlotDigit{});
    }

    friend constexpr bool operator==(FileSlotDigit, FileSlotDigit) = default;
};

struct ItemOrdinal {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ItemOrdinal, ItemOrdinal) = default;
};

struct WideIoIndex {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr IoIndex io() const noexcept {
        return IoIndex{static_cast<std::uint8_t>(v & 0x3Fu)};
    }

    [[nodiscard]] constexpr std::uint8_t sub() const noexcept {
        return static_cast<std::uint8_t>(v >> 6);
    }

    [[nodiscard]] constexpr std::uint8_t fio_byte() const noexcept {
        return static_cast<std::uint8_t>(v);
    }

    [[nodiscard]] static constexpr WideIoIndex of(IoIndex io, std::uint8_t sub) noexcept {
        return WideIoIndex{
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(sub) << 6) | io.v)
        };
    }

    friend constexpr bool operator==(WideIoIndex, WideIoIndex) = default;
};

static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).io() == IoIndex{0x2A});
static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).sub() == 3);
static_assert(WideIoIndex::of(IoIndex{2}, 2) == WideIoIndex{0x82});
static_assert(WideIoIndex::of(IoIndex{2}, 4).fio_byte() == 0x02);
static_assert(WideIoIndex{0x82}.sub() == 2);
static_assert(WideIoIndex{0x82}.io() == IoIndex{2});

struct PathId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(PathId, PathId) = default;
};

struct BlockCount;

struct FileSize {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr std::uint32_t low_word() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] constexpr std::uint32_t high_word() const noexcept {
        return static_cast<std::uint32_t>(v >> std::numeric_limits<std::uint32_t>::digits);
    }

    using affine_count = FileSize;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::size_t length_of(BlockCount n) const noexcept;

    [[nodiscard]] constexpr FileSize per_step(std::uint32_t steps) const noexcept {
        const std::uint64_t each = v / steps;
        return FileSize{each == 0 ? 1u : each};
    }

    [[nodiscard]] constexpr bool within(FileSize lo, FileSize hi) const noexcept {
        return lo.v <= v && v <= hi.v;
    }

    [[nodiscard]] constexpr bool holds_offset(std::uint32_t offset) const noexcept {
        return offset < v;
    }

    [[nodiscard]] constexpr std::size_t clamp_write(
        std::uint32_t offset,
        std::size_t want
    ) const noexcept {
        if (!holds_offset(offset))
            return 0;
        const std::uint64_t room = v - offset;
        return static_cast<std::size_t>(std::min<std::uint64_t>(want, room));
    }

    [[nodiscard]] constexpr std::size_t blocks_in(std::size_t budget) const noexcept {
        return budget / static_cast<std::size_t>(v);
    }

    [[nodiscard]] constexpr FileSize after_header(FileSize header) const noexcept {
        return FileSize{v > header.v ? v - header.v : 0u};
    }

    [[nodiscard]] constexpr BlockCount units_covering(FileSize unit) const noexcept;

    [[nodiscard]] constexpr std::chrono::nanoseconds mib_share_of(
        std::chrono::nanoseconds per_mib
    ) const noexcept {
        constexpr std::uint64_t kBytesPerMiB = 1u << 20;
        return std::chrono::nanoseconds{static_cast<std::int64_t>(
            (v * static_cast<std::uint64_t>(per_mib.count())) / kBytesPerMiB
        )};
    }

    [[nodiscard]] constexpr BlockCount whole_units(FileSize unit) const noexcept;
    friend constexpr bool operator==(FileSize, FileSize) = default;
    friend constexpr auto operator<=>(FileSize, FileSize) = default;
};

struct Lba;

struct BlockCount {
    std::uint32_t v = 0;
    using affine_count = BlockCount;

    [[nodiscard]] constexpr std::byte low_byte() const noexcept {
        return static_cast<std::byte>(static_cast<std::uint8_t>(v));
    }

    [[nodiscard]] constexpr bool holds(Lba start, BlockCount count) const noexcept;

    constexpr void store_le32(std::span<std::uint8_t, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    constexpr void store_be32(std::span<std::byte, 4> dst) const noexcept {
        infra::store_be<std::uint32_t>(dst, v);
    }

    friend constexpr bool operator==(BlockCount, BlockCount) = default;
    friend constexpr auto operator<=>(BlockCount, BlockCount) = default;
};

struct Lba {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t v = 0;
    using affine_count = BlockCount;
    friend constexpr bool operator==(Lba, Lba) = default;
    friend constexpr auto operator<=>(Lba, Lba) = default;
};

constexpr BlockCount FileSize::units_covering(FileSize unit) const noexcept {
    return BlockCount{static_cast<std::uint32_t>((v + unit.v - 1) / unit.v)};
}

constexpr BlockCount FileSize::whole_units(FileSize unit) const noexcept {
    return BlockCount{static_cast<std::uint32_t>(v / unit.v)};
}

constexpr std::size_t FileSize::length_of(BlockCount n) const noexcept {
    return static_cast<std::size_t>(v * n.v);
}

constexpr bool BlockCount::holds(Lba start, BlockCount count) const noexcept {
    return static_cast<std::uint64_t>(start.v) + count.v <= v;
}

struct BlockSize;

struct SdStatusWord {
    std::uint16_t v = 0;

    enum Layout : std::uint32_t {
        kValid = 0x8000,
        kOpMask = 3,
        kWriteOp = 2,
        kSlotShift = 2,
        kSlotMask = 0xF,
        kSizeShift = 6,
        kSizeMask = 7,
        kCountShift = 9,
        kCountMask = 0x3F,
        kMinBlockBytes = 128,
    };

    [[nodiscard]] constexpr bool valid() const noexcept { return (v & kValid) != 0u; }

    [[nodiscard]] constexpr bool decoded() const noexcept { return (v & kOpMask) != 0u; }

    [[nodiscard]] constexpr bool writes() const noexcept { return (v & kOpMask) == kWriteOp; }

    [[nodiscard]] constexpr SlotIndex slot() const noexcept {
        return SlotIndex{static_cast<std::uint8_t>((v >> kSlotShift) & kSlotMask)};
    }

    [[nodiscard]] constexpr BlockCount block_count() const noexcept;
    [[nodiscard]] constexpr BlockSize block_size() const noexcept;
    friend constexpr bool operator==(SdStatusWord, SdStatusWord) = default;
};

struct PackedBcd {
    static constexpr std::uint8_t kMaxValue = 99;
    static constexpr unsigned kRadix = 10;
    static constexpr unsigned kNibbleBits = 4;
    static constexpr unsigned kLowNibble = 0x0F;
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<PackedBcd> of(std::uint8_t n) noexcept {
        if (n > kMaxValue)
            return std::nullopt;
        return PackedBcd{static_cast<std::uint8_t>(((n / kRadix) << kNibbleBits) | (n % kRadix))};
    }

    [[nodiscard]] static constexpr std::uint8_t unmasked_byte(std::uint32_t n) noexcept {
        const std::uint8_t low = static_cast<std::uint8_t>(n);
        return static_cast<std::uint8_t>(((low / kRadix) << kNibbleBits) | (low % kRadix));
    }

    [[nodiscard]] constexpr std::uint8_t value() const noexcept {
        return static_cast<std::uint8_t>(((v >> kNibbleBits) * kRadix) + (v & kLowNibble));
    }

    friend constexpr bool operator==(PackedBcd, PackedBcd) = default;
};

static_assert(sizeof(PackedBcd) == 1);
static_assert(PackedBcd::of(59) == PackedBcd{0x59});
static_assert(PackedBcd{0x59}.value() == 59);
static_assert(!PackedBcd::of(100).has_value());

struct MemSizeCookie {
    std::uint16_t v = 0;
    friend constexpr bool operator==(MemSizeCookie, MemSizeCookie) = default;
};

struct StatusBit {
    std::uint8_t v = 0;
    friend constexpr bool operator==(StatusBit, StatusBit) = default;
    friend constexpr auto operator<=>(StatusBit, StatusBit) = default;
};

struct MouseDelta {
    int dx = 0;
    int dy = 0;
    int wheel = 0;
    friend constexpr bool operator==(MouseDelta, MouseDelta) = default;
};

struct OsdRow;

struct OsdRowCount {

    std::uint32_t v = 0;
    using affine_count = OsdRowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr OsdRowCount of(I n) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, n))
            return OsdRowCount{kField};
        return OsdRowCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr bool covers(OsdRow row) const noexcept;

    [[nodiscard]] constexpr OsdRow end() const noexcept;
    friend constexpr bool operator==(OsdRowCount, OsdRowCount) = default;
    friend constexpr auto operator<=>(OsdRowCount, OsdRowCount) = default;
};

struct OsdRow {
    std::uint32_t v = 0;
    using affine_count = OsdRowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr OsdRow of(I i) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, i))
            return OsdRow{kField};
        return OsdRow{static_cast<std::uint32_t>(i)};
    }

    friend constexpr bool operator==(OsdRow, OsdRow) = default;
    friend constexpr auto operator<=>(OsdRow, OsdRow) = default;
};

inline constexpr OsdRowCount kOneOsdRow{1};

template <class T, std::size_t N>
using OsdRowCells = std::array<T, N>;

template <class T, std::size_t N>
[[nodiscard]] constexpr T& row_cell(OsdRowCells<T, N>& cells, OsdRow row) noexcept {
    return cells[(row - OsdRow{}).size()];
}

template <class T, std::size_t N>
[[nodiscard]] constexpr const T& row_cell(const OsdRowCells<T, N>& cells, OsdRow row) noexcept {
    return cells[(row - OsdRow{}).size()];
}

constexpr bool OsdRowCount::covers(OsdRow row) const noexcept {
    return row < end();
}

constexpr OsdRow OsdRowCount::end() const noexcept {
    return OsdRow{v};
}

struct HidUsage {
    std::uint8_t v = 0;
    friend constexpr bool operator==(HidUsage, HidUsage) = default;
};

struct KeyCode {
    std::uint32_t v = 0;

    static constexpr std::uint32_t kRelease = 0x400000u;

    [[nodiscard]] constexpr bool is_release() const noexcept { return (v & kRelease) != 0; }

    [[nodiscard]] constexpr KeyCode code() const noexcept { return KeyCode{v & ~kRelease}; }

    [[nodiscard]] constexpr KeyCode with_release() const noexcept { return KeyCode{v | kRelease}; }

    friend constexpr bool operator==(KeyCode, KeyCode) = default;
};

static_assert(sizeof(KeyCode) == 4);

struct PlayerIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(PlayerIndex, PlayerIndex) = default;
    friend constexpr auto operator<=>(PlayerIndex, PlayerIndex) = default;
};

struct JoyMask {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool any(JoyMask bits) const noexcept { return (v & bits.v) != 0u; }

    [[nodiscard]] constexpr bool beyond_low_half() const noexcept {
        return (v >> std::numeric_limits<std::uint16_t>::digits) != 0u;
    }

    [[nodiscard]] constexpr JoyMask with(JoyMask bits) const noexcept {
        return JoyMask{v | bits.v};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0u; }

    [[nodiscard]] constexpr JoyMask without(JoyMask bits) const noexcept {
        return JoyMask{v & ~bits.v};
    }

    [[nodiscard]] constexpr JoyMask only(JoyMask bits) const noexcept {
        return JoyMask{v & bits.v};
    }

    [[nodiscard]] constexpr JoyMask differing(JoyMask other) const noexcept {
        return JoyMask{v ^ other.v};
    }

    [[nodiscard]] constexpr JoyMask lowest() const noexcept { return JoyMask{v & (~v + 1u)}; }

    friend constexpr bool operator==(JoyMask, JoyMask) = default;
};

struct OsdMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool has(OsdMask bits) const noexcept { return (v & bits.v) != 0; }

    [[nodiscard]] constexpr OsdMask only(OsdMask bits) const noexcept {
        return OsdMask{static_cast<std::uint16_t>(v & bits.v)};
    }

    [[nodiscard]] constexpr OsdMask with(OsdMask bits) const noexcept {
        return OsdMask{static_cast<std::uint16_t>(v | bits.v)};
    }

    friend constexpr bool operator==(OsdMask, OsdMask) = default;
};

inline constexpr OsdMask kSdramPresent{0x8000};
inline constexpr OsdMask kSdramSizeBits{0x7};
inline constexpr OsdMask kSdramSize32{0x1};
inline constexpr OsdMask kSdramSize64{0x3};
inline constexpr OsdMask kSdramSize128{0x7};

struct StorageSeq {
    std::uint32_t v = 0;
    friend constexpr bool operator==(StorageSeq, StorageSeq) = default;
};

struct BindGeneration {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t v = 0;

    static constexpr std::uint32_t kMaxGen = 0xFFFFu;

    [[nodiscard]] constexpr BindGeneration next() const noexcept {
        return BindGeneration{static_cast<std::uint16_t>(v >= kMaxGen ? 1u : v + 1u)};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(BindGeneration, BindGeneration) = default;
};

static_assert(sizeof(BindGeneration) == 2);
static_assert(BindGeneration{BindGeneration::kMaxGen}.next() == BindGeneration{1});

struct ArenaHalf {
    std::uint8_t v = 0;

    enum : std::uint8_t { kCount = 2 };

    [[nodiscard]] constexpr bool exists() const noexcept { return v < kCount; }

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr ArenaHalf of(I pool_index) noexcept {
        return ArenaHalf{static_cast<std::uint8_t>(pool_index % kCount)};
    }

    template <std::unsigned_integral M>
    [[nodiscard]] constexpr M mask() const noexcept {
        return static_cast<M>(M{1} << v);
    }

    [[nodiscard]] constexpr ArenaHalf other() const noexcept {
        return ArenaHalf{static_cast<std::uint8_t>(v ^ 1u)};
    }

    friend constexpr bool operator==(ArenaHalf, ArenaHalf) = default;
};

struct RxSlabId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(RxSlabId, RxSlabId) = default;
};

struct TxSlabId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(TxSlabId, TxSlabId) = default;
};

struct FileId {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr PathId path() const noexcept { return PathId{v}; }

    friend constexpr bool operator==(FileId, FileId) = default;
};

struct IdentityStamp {
    std::uint16_t v = 0;
    friend constexpr bool operator==(IdentityStamp, IdentityStamp) = default;
};

struct CorrelationTag {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CorrelationTag, CorrelationTag) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }
};

inline constexpr CorrelationTag kUncaused{0};

struct ActKey;

struct ActKey16 {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ActKey16, ActKey16) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }

    [[nodiscard]] constexpr ActKey widen() const noexcept;
};

struct ActKey {
    std::uint32_t v = 0;
    friend constexpr bool operator==(ActKey, ActKey) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }

    [[nodiscard]] constexpr ActKey next() const noexcept {
        return v + 1u == 0u ? ActKey{1u} : ActKey{v + 1u};
    }

    [[nodiscard]] constexpr bool at_or_past(ActKey key) const noexcept {
        return static_cast<std::int32_t>(v - key.v) >= 0;
    }

    [[nodiscard]] constexpr ActKey16 narrow() const noexcept {
        return ActKey16{static_cast<std::uint16_t>(v)};
    }
};

constexpr ActKey ActKey16::widen() const noexcept {
    return ActKey{v};
}

struct SubmenuNo {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SubmenuNo next() const noexcept {
        return SubmenuNo{
            v == std::numeric_limits<std::uint8_t>::max() ? v : static_cast<std::uint8_t>(v + 1u)
        };
    }

    friend constexpr bool operator==(SubmenuNo, SubmenuNo) = default;
};

inline constexpr SubmenuNo kRootSubmenu{0};

template <class Page, class F>
constexpr void for_each_page(std::span<Page> pages, F&& f) {
    SubmenuNo no = kRootSubmenu;
    for (Page& page : pages) {
        f(no, page);
        no = no.next();
    }
}

struct BlockSlotMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool has(SlotIndex slot) const noexcept {
        return (v & (1u << slot.v)) != 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr BlockSlotMask with(SlotIndex slot) const noexcept {
        return BlockSlotMask{static_cast<std::uint16_t>(v | (1u << slot.v))};
    }

    [[nodiscard]] constexpr BlockSlotMask with_or_all(SlotIndex slot) const noexcept {
        constexpr unsigned kBits = 16;
        constexpr std::uint16_t kEveryBit = 0xFFFF;
        return slot.v >= kBits ? BlockSlotMask{kEveryBit} : with(slot);
    }

    [[nodiscard]] constexpr BlockSlotMask without(SlotIndex slot) const noexcept {
        return BlockSlotMask{static_cast<std::uint16_t>(v & ~(1u << slot.v))};
    }

    [[nodiscard]] constexpr BlockSlotMask united(BlockSlotMask o) const noexcept {
        return BlockSlotMask{static_cast<std::uint16_t>(v | o.v)};
    }

    [[nodiscard]] constexpr BlockSlotMask common(BlockSlotMask o) const noexcept {
        return BlockSlotMask{static_cast<std::uint16_t>(v & o.v)};
    }

    [[nodiscard]] constexpr BlockSlotMask minus(BlockSlotMask o) const noexcept {
        return BlockSlotMask{static_cast<std::uint16_t>(v & ~o.v)};
    }

    friend constexpr bool operator==(BlockSlotMask, BlockSlotMask) = default;
};

struct BlockSize {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr BlockSize span_of(BlockCount n) const noexcept {
        return BlockSize{n.v * v};
    }

    [[nodiscard]] constexpr std::uint64_t position_of(Lba lba) const noexcept {
        return static_cast<std::uint64_t>(lba.v) * v;
    }

    [[nodiscard]] constexpr std::uint32_t offset_of(BlockCount n) const noexcept { return n.v * v; }

    [[nodiscard]] constexpr std::size_t length_of(BlockCount n) const noexcept {
        return static_cast<std::size_t>(n.v * v);
    }

    [[nodiscard]] constexpr FileSize extent(BlockCount n) const noexcept {
        return FileSize{static_cast<std::uint64_t>(n.v) * v};
    }

    [[nodiscard]] constexpr BlockCount blocks_in(BlockSize total) const noexcept {
        return BlockCount{v == 0 ? 0u : total.v / v};
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> block_of(std::span<Byte> buf) const noexcept {
        return buf.first(static_cast<std::size_t>(v));
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr bool operator==(BlockSize, BlockSize) = default;
    friend constexpr auto operator<=>(BlockSize, BlockSize) = default;
};

constexpr BlockCount SdStatusWord::block_count() const noexcept {
    return BlockCount{((static_cast<std::uint32_t>(v) >> kCountShift) & kCountMask) + 1u};
}

constexpr BlockSize SdStatusWord::block_size() const noexcept {
    return BlockSize{kMinBlockBytes << ((static_cast<std::uint32_t>(v) >> kSizeShift) & kSizeMask)};
}

[[nodiscard]] constexpr FileSize byte_offset(Lba lba, BlockSize bs) noexcept {
    return FileSize{static_cast<std::uint64_t>(lba.v) * bs.v};
}

[[nodiscard]] constexpr Lba end_lba(FileSize size, BlockSize bs) noexcept {
    return Lba{static_cast<std::uint32_t>(size.v / bs.v)};
}

struct FieldCount;

struct FieldValue {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool within(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue cycled_forward(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue cycled_back(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue wrapped_back(FieldCount count) const noexcept;
    friend constexpr bool operator==(FieldValue, FieldValue) = default;
    friend constexpr auto operator<=>(FieldValue, FieldValue) = default;
};

struct FieldCount {
    std::uint32_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr FieldCount of(I n) noexcept {
        constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
        if (std::cmp_less(kMax, n))
            return FieldCount{kMax};
        return FieldCount{static_cast<std::uint32_t>(n)};
    }

    friend constexpr bool operator==(FieldCount, FieldCount) = default;
};

constexpr bool FieldValue::within(FieldCount count) const noexcept {
    return v < count.v;
}

constexpr FieldValue FieldValue::cycled_forward(FieldCount count) const noexcept {
    return v + 1u >= count.v ? FieldValue{} : FieldValue{v + 1u};
}

constexpr FieldValue FieldValue::cycled_back(FieldCount count) const noexcept {
    return v == 0 ? FieldValue{count.v - 1u} : FieldValue{v - 1u};
}

constexpr FieldValue FieldValue::wrapped_back(FieldCount count) const noexcept {
    return FieldValue{(v + count.v - 1u) % count.v};
}

struct RegionCode {
    std::uint8_t v = 0;
    static const RegionCode kNone;
    friend constexpr bool operator==(RegionCode, RegionCode) = default;
};

inline constexpr RegionCode RegionCode::kNone{0xFF};

struct OptionRowIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(OptionRowIndex, OptionRowIndex) = default;
    friend constexpr auto operator<=>(OptionRowIndex, OptionRowIndex) = default;
};

struct CoreBaySlot {
    std::uint8_t v = 0;

    template <std::size_t N>
    [[nodiscard]] constexpr bool within() const noexcept {
        return v < N;
    }

    friend constexpr bool operator==(CoreBaySlot, CoreBaySlot) = default;
};

inline constexpr CoreBaySlot kNoBaySlot{0xFF};

struct DiskUnit {
    std::uint8_t v = 0;
    friend constexpr bool operator==(DiskUnit, DiskUnit) = default;
};

struct SectorAck {
    std::uint16_t v = 0;
    static constexpr unsigned kShift = 8;

    [[nodiscard]] static constexpr SectorAck of(SlotIndex slot) noexcept {
        return SectorAck{static_cast<std::uint16_t>(slot.v << kShift)};
    }

    [[nodiscard]] static constexpr SectorAck stock(DiskUnit unit) noexcept {
        return SectorAck{static_cast<std::uint16_t>((unit.v + 1u) << kShift)};
    }

    [[nodiscard]] constexpr hal::SpiWord with_opcode(UioOp op) const noexcept {
        return hal::SpiWord{static_cast<std::uint16_t>(std::to_underlying(op) | v)};
    }

    friend constexpr bool operator==(SectorAck, SectorAck) = default;
};

static_assert(sizeof(SectorAck) == 2);
static_assert(SectorAck::of(SlotIndex{3}) == SectorAck{0x0300});
static_assert(SectorAck::stock(DiskUnit{0}) == SectorAck{0x0100});

struct MouseButtons {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool has_any(MouseButtons bits) const noexcept {
        return (v & bits.v) != 0;
    }

    [[nodiscard]] static constexpr MouseButtons nth(unsigned n) noexcept {
        return MouseButtons{static_cast<std::uint8_t>(1u << n)};
    }

    [[nodiscard]] constexpr MouseButtons with(MouseButtons bits) const noexcept {
        return MouseButtons{static_cast<std::uint8_t>(v | bits.v)};
    }

    [[nodiscard]] constexpr MouseButtons without(MouseButtons bits) const noexcept {
        return MouseButtons{static_cast<std::uint8_t>(v & ~bits.v)};
    }

    friend constexpr bool operator==(MouseButtons, MouseButtons) = default;
};

struct ImageTag {
    std::uint8_t v = 0;
    friend constexpr bool operator==(ImageTag, ImageTag) = default;
};

struct DipRowIndex {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr DipRowIndex of(I n) noexcept {
        return DipRowIndex{static_cast<std::uint8_t>(n)};
    }

    template <class C>
    [[nodiscard]] constexpr bool indexes(const C& c) const noexcept {
        return v < std::size(c);
    }

    template <class C>
    [[nodiscard]] constexpr auto& pick(C& c) const noexcept {
        return c[v];
    }

    friend constexpr bool operator==(DipRowIndex, DipRowIndex) = default;
    friend constexpr auto operator<=>(DipRowIndex, DipRowIndex) = default;
};

struct DipChoiceCount {
    std::uint8_t v = 0;
    using affine_count = DipChoiceCount;

    template <std::integral I>
    [[nodiscard]] static constexpr DipChoiceCount of(I n) noexcept {
        return DipChoiceCount{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    template <class C>
    [[nodiscard]] constexpr DipChoiceCount capped_to(const C& c) const noexcept {
        return v < std::size(c) ? *this : of(std::size(c));
    }

    template <class F>
    constexpr void each(F&& f) const;
    friend constexpr bool operator==(DipChoiceCount, DipChoiceCount) = default;
    friend constexpr auto operator<=>(DipChoiceCount, DipChoiceCount) = default;
};

struct DipChoice {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr DipChoice of(I n) noexcept {
        return DipChoice{static_cast<std::uint8_t>(n)};
    }

    template <class C>
    [[nodiscard]] constexpr bool indexes(const C& c) const noexcept {
        return v < std::size(c);
    }

    template <class C>
    [[nodiscard]] constexpr auto& pick(C& c) const noexcept {
        return c[v];
    }

    [[nodiscard]] constexpr bool within(DipChoiceCount choices) const noexcept {
        return v < choices.v;
    }

    [[nodiscard]] constexpr DipChoice after() const noexcept {
        return DipChoice{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr DipChoice next(DipChoiceCount choices) const noexcept {
        return DipChoice{static_cast<std::uint8_t>(v + 1u < choices.v ? v + 1u : 0u)};
    }

    friend constexpr bool operator==(DipChoice, DipChoice) = default;
};

template <class F>
constexpr void DipChoiceCount::each(F&& f) const {
    for (std::uint8_t i = 0; i < v; ++i)
        f(DipChoice{i});
}

}
