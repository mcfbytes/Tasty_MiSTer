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
#include <type_traits>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

#include "hal/reg_word.h"
#include "hal/sdram_cfg.h"
#include "infra/affine.h"
#include "infra/ascii.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "infra/json_out.h"
#include "hal/types.h"
#include "infra/seat.h"
#include "os/map_length.h"
#include "os/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct IoIndex;
struct ListIndex;
struct PollParam;

struct SlotIndex {
    std::uint8_t v = 0;

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr SlotIndex of(I i) noexcept {
        return SlotIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr IoIndex io_index() const noexcept;

    [[nodiscard]] constexpr bool in_block_slots() const noexcept;

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept;

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

    [[nodiscard]] constexpr ListIndex index_above(ListIndex base) const noexcept;

    [[nodiscard]] constexpr char record_byte() const noexcept { return static_cast<char>(v); }

    friend constexpr bool operator==(IoIndex, IoIndex) = default;
    friend constexpr auto operator<=>(IoIndex, IoIndex) = default;
};

constexpr IoIndex SlotIndex::io_index() const noexcept {
    return IoIndex{v};
}

struct ListIndex {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool at_or_above(ListIndex base) const noexcept { return v >= base.v; }

    [[nodiscard]] constexpr ListIndex since(ListIndex base) const noexcept {
        return ListIndex{v - base.v};
    }

    [[nodiscard]] constexpr bool names_slot() const noexcept {
        return v <= std::numeric_limits<std::uint8_t>::max();
    }

    [[nodiscard]] constexpr IoIndex as_slot() const noexcept {
        return IoIndex{static_cast<std::uint8_t>(v)};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(ListIndex, ListIndex) = default;
    friend constexpr auto operator<=>(ListIndex, ListIndex) = default;
};

constexpr ListIndex IoIndex::index_above(ListIndex base) const noexcept {
    return ListIndex{base.v + v};
}

constexpr std::uint32_t SlotIndex::detail() const noexcept {
    const std::uint32_t word = v;
    return word;
}

struct FileSlotDigit {
    friend constexpr auto operator<=>(FileSlotDigit, FileSlotDigit) = default;

    static constexpr std::uint32_t kMax = 15;

    [[nodiscard]] static constexpr std::optional<FileSlotDigit> from(std::uint32_t n) noexcept {
        if (n > kMax)
            return std::nullopt;
        return FileSlotDigit{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] static constexpr std::optional<FileSlotDigit> from(IoIndex io) noexcept {
        return from(io.v);
    }

    [[nodiscard]] static constexpr FileSlotDigit zero() noexcept { return FileSlotDigit{}; }

    [[nodiscard]] constexpr IoIndex io_index() const noexcept { return IoIndex{v}; }

    [[nodiscard]] static constexpr FileSlotDigit of(IoIndex index) noexcept {
        return FileSlotDigit{index.v};
    }

    [[nodiscard]] constexpr SlotIndex slot() const noexcept { return SlotIndex{v}; }

    [[nodiscard]] static constexpr FileSlotDigit or_zero(std::optional<FileSlotDigit> d) noexcept {
        return d.value_or(FileSlotDigit{});
    }

    friend constexpr bool operator==(FileSlotDigit, FileSlotDigit) = default;

    constexpr FileSlotDigit() noexcept = default;

private:
    explicit constexpr FileSlotDigit(std::uint8_t digit) noexcept : v{digit} {}

    std::uint8_t v = 0;
};

enum class FileRowKind : std::uint8_t { File, Mount };

struct FileRowId {
    FileRowKind kind;
    IoIndex index;

    [[nodiscard]] static constexpr FileRowId file(IoIndex i) noexcept {
        return FileRowId{FileRowKind::File, i};
    }

    [[nodiscard]] static constexpr FileRowId mount(IoIndex i) noexcept {
        return FileRowId{FileRowKind::Mount, i};
    }

    [[nodiscard]] static constexpr FileRowId of(FileRowKind k, IoIndex i) noexcept {
        return FileRowId{k, i};
    }

    friend constexpr bool operator==(FileRowId, FileRowId) = default;

private:
    constexpr FileRowId(FileRowKind k, IoIndex i) noexcept : kind(k), index(i) {}
};

static_assert(sizeof(FileRowId) == 2 && std::is_trivially_copyable_v<FileRowId>);
static_assert(!std::is_default_constructible_v<FileRowId>);
static_assert(FileRowId::file(IoIndex{1}) != FileRowId::mount(IoIndex{1}));

struct ItemOrdinal {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ItemOrdinal, ItemOrdinal) = default;
};

struct WideSub {
    std::uint8_t v = 0;
    friend constexpr bool operator==(WideSub, WideSub) = default;
    friend constexpr auto operator<=>(WideSub, WideSub) = default;
};

struct WideIoIndex {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr IoIndex io() const noexcept {
        return IoIndex{static_cast<std::uint8_t>(v & 0x3Fu)};
    }

    [[nodiscard]] constexpr WideSub sub() const noexcept {
        return WideSub{static_cast<std::uint8_t>(v >> 6)};
    }

    [[nodiscard]] constexpr std::uint8_t fio_byte() const noexcept {
        return static_cast<std::uint8_t>(v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept {
        return infra::DecimalText{fio_byte()};
    }

    [[nodiscard]] static constexpr WideIoIndex of(IoIndex io, std::uint8_t sub) noexcept {
        return WideIoIndex{
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(sub) << 6) | io.v)
        };
    }

    [[nodiscard]] static constexpr WideIoIndex of(IoIndex io) noexcept { return of(io, 0); }

    friend constexpr bool operator==(WideIoIndex, WideIoIndex) = default;
};

static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).io() == IoIndex{0x2A});
static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).sub() == WideSub{3});
static_assert(WideIoIndex::of(IoIndex{2}, 2) == WideIoIndex{0x82});
static_assert(WideIoIndex::of(IoIndex{2}, 4).fio_byte() == 0x02);
static_assert(WideIoIndex{0x82}.sub() == WideSub{2});
static_assert(WideIoIndex{0x82}.io() == IoIndex{2});

struct PathId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(PathId, PathId) = default;
};

struct BlockCount;
struct BlockSize;

struct FileSize {
    std::uint64_t v = 0;

    [[nodiscard]] static constexpr FileSize of(infra::ByteCount bytes) noexcept {
        return FileSize{static_cast<std::uint64_t>(bytes.v)};
    }

    template <class B>
        requires(sizeof(B) == sizeof(char))
    [[nodiscard]] static constexpr FileSize of(std::span<B> buf) noexcept {
        return FileSize{buf.size_bytes()};
    }

    template <class B>
        requires(sizeof(B) == sizeof(char))
    [[nodiscard]] constexpr std::span<B> skip_in(std::span<B> buf) const noexcept {
        return buf.subspan(static_cast<std::size_t>(v));
    }

    template <class B>
        requires(sizeof(B) == sizeof(char))
    [[nodiscard]] constexpr std::span<B> first_of(std::span<B> buf) const noexcept {
        return buf.first(static_cast<std::size_t>(v));
    }

    [[nodiscard]] constexpr std::uint32_t low_word() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] constexpr std::uint32_t high_word() const noexcept {
        return static_cast<std::uint32_t>(v >> std::numeric_limits<std::uint32_t>::digits);
    }

    [[nodiscard]] constexpr FileSize min(FileSize other) const noexcept {
        return FileSize{std::min(v, other.v)};
    }

    [[nodiscard]] constexpr FileSize advance(FileSize by) const noexcept { return *this + by; }

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept { return low_word(); }

    template <std::integral Word>
    [[nodiscard]] constexpr Word file_word() const noexcept {
        constexpr unsigned kHalfBits = 32;
        return static_cast<Word>(static_cast<std::uint64_t>(high_word()) << kHalfBits | low_word());
    }

    template <std::integral Word>
    constexpr void stamp_into(Word& slot) const noexcept {
        slot = static_cast<Word>(v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    using affine_count = FileSize;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

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

    [[nodiscard]] constexpr std::size_t slots_in(std::size_t budget) const noexcept {
        return v == 0 ? 0 : budget / static_cast<std::size_t>(v);
    }

    [[nodiscard]] constexpr FileSize after_header(FileSize header) const noexcept {
        return FileSize{v > header.v ? v - header.v : 0u};
    }

    [[nodiscard]] constexpr BlockCount units_covering(BlockSize unit) const noexcept;

    [[nodiscard]] constexpr std::chrono::nanoseconds mib_share_of(
        std::chrono::nanoseconds per_mib
    ) const noexcept {
        constexpr std::uint64_t kBytesPerMiB = 1u << 20;
        return std::chrono::nanoseconds{static_cast<std::int64_t>(
            (v * static_cast<std::uint64_t>(per_mib.count())) / kBytesPerMiB
        )};
    }

    [[nodiscard]] constexpr BlockCount whole_units(BlockSize unit) const noexcept;
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

    [[nodiscard]] constexpr BlockCount rounded_up_to(BlockCount multiple) const noexcept;

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

    [[nodiscard]] constexpr Lba wrapping_prev() const noexcept { return Lba{v - 1u}; }

    constexpr void store_be32(std::span<std::byte, 4> dst) const noexcept {
        infra::store_be<std::uint32_t>(dst, v);
    }

    [[nodiscard]] constexpr hal::RegWord register_word() const noexcept { return hal::RegWord{v}; }

    [[nodiscard]] static constexpr Lba of(hal::RegWord w) noexcept { return Lba{w.bits()}; }

    friend constexpr bool operator==(Lba, Lba) = default;
    friend constexpr auto operator<=>(Lba, Lba) = default;
};

template <std::integral Word = std::uint32_t>
[[nodiscard]] constexpr Word detail_of(Lba lba) noexcept {
    return static_cast<Word>(lba.v & std::numeric_limits<std::uint32_t>::max());
}

namespace detail {

[[nodiscard]] constexpr std::uint64_t positional_word(FileSize at) noexcept {
    return at.v;
}
}

constexpr bool BlockCount::holds(Lba start, BlockCount count) const noexcept {
    return static_cast<std::uint64_t>(start.v) + count.v <= v;
}

constexpr BlockCount BlockCount::rounded_up_to(BlockCount multiple) const noexcept {
    return BlockCount{((v + multiple.v - 1u) / multiple.v) * multiple.v};
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

    [[nodiscard]] static constexpr SdStatusWord of(hal::RegWord w) noexcept {
        return SdStatusWord{static_cast<std::uint16_t>(w.bits())};
    }

    [[nodiscard]] constexpr bool decoded() const noexcept { return (v & kOpMask) != 0u; }

    [[nodiscard]] constexpr bool writes() const noexcept { return (v & kOpMask) == kWriteOp; }

    [[nodiscard]] constexpr SlotIndex slot() const noexcept {
        return SlotIndex{static_cast<std::uint8_t>((v >> kSlotShift) & kSlotMask)};
    }

    [[nodiscard]] constexpr BlockCount block_count() const noexcept;
    [[nodiscard]] constexpr BlockSize block_size() const noexcept;
    friend constexpr bool operator==(SdStatusWord, SdStatusWord) = default;
};

struct SdStatParameter {
    std::uint16_t v = 0;

    enum : std::uint16_t { kReadOnly = 0x80 };

    static constexpr unsigned kSlotBits = std::numeric_limits<std::uint16_t>::digits;

    [[nodiscard]] static constexpr SdStatParameter of(SlotIndex slot, bool writable) noexcept {
        const std::uint16_t bit = slot.v < kSlotBits
            ? static_cast<std::uint16_t>(std::uint16_t{1u} << slot.v)
            : std::uint16_t{0u};
        return SdStatParameter{static_cast<std::uint16_t>(
            bit | (writable ? std::uint16_t{0u} : static_cast<std::uint16_t>(kReadOnly))
        )};
    }

    [[nodiscard]] constexpr bool read_only() const noexcept { return (v & kReadOnly) != 0u; }

    friend constexpr bool operator==(SdStatParameter, SdStatParameter) = default;
};

static_assert(sizeof(SdStatParameter) == 2);

struct SdRegisterWord {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr SdStatusWord status() const noexcept {
        return SdStatusWord{static_cast<std::uint16_t>(v)};
    }

    friend constexpr bool operator==(SdRegisterWord, SdRegisterWord) = default;
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

    [[nodiscard]] static constexpr MemSizeCookie of(std::optional<hal::SdramCfg> cfg) noexcept {
        return cfg ? MemSizeCookie{cfg->cookie()} : MemSizeCookie{};
    }

    friend constexpr bool operator==(MemSizeCookie, MemSizeCookie) = default;
};

static_assert(
    MemSizeCookie::of(hal::SdramCfg{3}) == MemSizeCookie{0x8003},
    "the handoff cell's cookie word carries the measured bit"
);
static_assert(
    MemSizeCookie::of(std::nullopt) == MemSizeCookie{},
    "an absent cell reads the 'absent or zero' word"
);

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

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

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

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    friend constexpr bool operator==(OsdRow, OsdRow) = default;
    friend constexpr auto operator<=>(OsdRow, OsdRow) = default;
};

inline constexpr OsdRowCount kOneOsdRow{1};

struct OsdRowBytes {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr OsdRowBytes of(std::span<const std::uint8_t> row) noexcept {
        return OsdRowBytes{static_cast<std::uint32_t>(row.size())};
    }

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(OsdRowBytes, OsdRowBytes) = default;
};

struct OsdCol;

struct OsdColCount {

    std::uint32_t v = 0;
    using affine_count = OsdColCount;

    template <std::integral I>
    [[nodiscard]] static constexpr OsdColCount of(I n) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, n))
            return OsdColCount{kField};
        return OsdColCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    [[nodiscard]] constexpr bool covers(OsdCol col) const noexcept;

    [[nodiscard]] constexpr OsdCol end() const noexcept;
    friend constexpr bool operator==(OsdColCount, OsdColCount) = default;
    friend constexpr auto operator<=>(OsdColCount, OsdColCount) = default;
};

struct OsdCol {
    std::uint32_t v = 0;
    using affine_count = OsdColCount;

    template <std::integral I>
    [[nodiscard]] static constexpr OsdCol of(I i) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, i))
            return OsdCol{kField};
        return OsdCol{static_cast<std::uint32_t>(i)};
    }

    [[nodiscard]] constexpr std::uint16_t wire_word() const noexcept {
        return static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max());
    }

    friend constexpr bool operator==(OsdCol, OsdCol) = default;
    friend constexpr auto operator<=>(OsdCol, OsdCol) = default;
};

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

constexpr bool OsdColCount::covers(OsdCol col) const noexcept {
    return col < end();
}

constexpr OsdCol OsdColCount::end() const noexcept {
    return OsdCol{v};
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

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr PlayerIndex of(I i) noexcept {
        return PlayerIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr PollParam poll_param() const noexcept;
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

    static const StorageSeq kRefused;

    [[nodiscard]] constexpr StorageSeq next() const noexcept {
        return v + 1u == 0u ? StorageSeq{1u} : StorageSeq{v + 1u};
    }

    friend constexpr bool operator==(StorageSeq, StorageSeq) = default;
};

inline constexpr StorageSeq StorageSeq::kRefused{0};
static_assert(StorageSeq{0xFFFFFFFFu}.next() == StorageSeq{1});

struct BindGeneration {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t v = 0;

    static constexpr std::uint32_t kMaxGen = 0xFFFFu;

    [[nodiscard]] constexpr BindGeneration next() const noexcept {
        return BindGeneration{static_cast<std::uint16_t>(v >= kMaxGen ? 1u : v + 1u)};
    }

    [[nodiscard]] constexpr bool ahead_of(BindGeneration earlier) const noexcept {
        return static_cast<std::int16_t>(v - earlier.v) > 0;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(BindGeneration, BindGeneration) = default;
};

static_assert(sizeof(BindGeneration) == 2);
static_assert(BindGeneration{BindGeneration::kMaxGen}.next() == BindGeneration{1});

struct StageSlot {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool within(StageSlot slots) const noexcept { return v < slots.v; }

    [[nodiscard]] constexpr std::size_t table_index() const noexcept {
        const std::size_t place = v;
        return place;
    }

    friend constexpr bool operator==(StageSlot, StageSlot) = default;
};

struct StageGen {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr StageGen next() const noexcept {
        const std::uint32_t n = v + 1u;
        return StageGen{n != 0u ? n : 1u};
    }

    [[nodiscard]] constexpr bool is_set() const noexcept { return v != 0; }

    [[nodiscard]] constexpr StageSlot slot_in(StageSlot slots) const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v % slots.v);
        return StageSlot{place};
    }

    [[nodiscard]] constexpr std::uint32_t stamp() const noexcept {
        const std::uint32_t word = v;
        return word;
    }

    friend constexpr bool operator==(StageGen, StageGen) = default;
};

static_assert(sizeof(StageGen) == 4);
static_assert(StageGen{0xFFFFFFFFu}.next() == StageGen{1});

struct MountEpoch {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr MountEpoch next() const noexcept { return MountEpoch{v + 1u}; }

    friend constexpr bool operator==(MountEpoch, MountEpoch) = default;
};

struct ReplyMask {
    std::uint16_t v = 0;
    friend constexpr bool operator==(ReplyMask, ReplyMask) = default;
};

struct ReplyWord {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool nonzero() const noexcept { return v != 0; }

    [[nodiscard]] constexpr bool any(ReplyMask m) const noexcept { return (v & m.v) != 0; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(ReplyWord, ReplyWord) = default;
};

static_assert(sizeof(ReplyWord) == 2);

struct CompanionBindGen {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr CompanionBindGen next() const noexcept {
        return CompanionBindGen{
            static_cast<std::uint16_t>(v >= std::numeric_limits<std::uint16_t>::max() ? 1u : v + 1u)
        };
    }

    [[nodiscard]] constexpr bool at_or_past(CompanionBindGen key) const noexcept {
        return static_cast<std::int16_t>(v - key.v) >= 0;
    }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    [[nodiscard]] static constexpr CompanionBindGen none() noexcept { return CompanionBindGen{}; }

    friend constexpr bool operator==(CompanionBindGen, CompanionBindGen) = default;
};

static_assert(sizeof(CompanionBindGen) == sizeof(std::uint16_t));
static_assert(
    CompanionBindGen{std::numeric_limits<std::uint16_t>::max()}.next() == CompanionBindGen{1}
);
static_assert(CompanionBindGen{}.is_none());

struct RebootSeq {
    std::uint16_t v = 0;

    static constexpr std::uint16_t kMax = 0xFFFFu;

    [[nodiscard]] constexpr RebootSeq next() const noexcept {
        return RebootSeq{static_cast<std::uint16_t>(v >= kMax ? 0u : v + 1u)};
    }

    friend constexpr bool operator==(RebootSeq, RebootSeq) = default;
};

static_assert(sizeof(RebootSeq) == 2);
static_assert(RebootSeq{RebootSeq::kMax}.next() == RebootSeq{0});

struct RxSlabId {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint16_t v = 0;
    static const RxSlabId kNone;

    template <std::uint16_t Usable>
    [[nodiscard]] constexpr std::optional<SlotIndex> slot_in() const noexcept {
        if (*this == kNone || v > Usable)
            return std::nullopt;
        return SlotIndex::of(v - 1u);
    }

    [[nodiscard]] static constexpr RxSlabId of_slot(SlotIndex i) noexcept {
        return RxSlabId{static_cast<std::uint16_t>(i.v + 1)};
    }

    friend constexpr bool operator==(RxSlabId, RxSlabId) = default;
};

inline constexpr RxSlabId RxSlabId::kNone{0};

struct TxSlabId {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint16_t v = 0;
    static const TxSlabId kNone;

    template <std::uint16_t Usable>
    [[nodiscard]] constexpr std::optional<SlotIndex> slot_in() const noexcept {
        if (*this == kNone || v > Usable)
            return std::nullopt;
        return SlotIndex::of(v - 1u);
    }

    [[nodiscard]] static constexpr TxSlabId of_slot(SlotIndex i) noexcept {
        return TxSlabId{static_cast<std::uint16_t>(i.v + 1)};
    }

    friend constexpr bool operator==(TxSlabId, TxSlabId) = default;
};

inline constexpr TxSlabId TxSlabId::kNone{0};

struct FileId {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr FileId of_slot(SlotIndex i) noexcept {
        return FileId{static_cast<std::uint16_t>(i.v + 1)};
    }

    [[nodiscard]] constexpr PathId path() const noexcept { return PathId{v}; }

    friend constexpr bool operator==(FileId, FileId) = default;
};

struct IdentityStamp {
    std::uint16_t v = 0;

    static constexpr std::uint32_t kMax = 0xFFFFu;

    [[nodiscard]] constexpr IdentityStamp next() const noexcept {
        return IdentityStamp{static_cast<std::uint16_t>(v >= kMax ? 1u : v + 1u)};
    }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    friend constexpr bool operator==(IdentityStamp, IdentityStamp) = default;
};

static_assert(IdentityStamp{IdentityStamp::kMax}.next() == IdentityStamp{1});
static_assert(IdentityStamp{}.is_none());

struct CorrelationTag {
    std::uint32_t v = 0;
    friend constexpr bool operator==(CorrelationTag, CorrelationTag) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }

    [[nodiscard]] constexpr CorrelationTag next() const noexcept {
        return v + 1u == 0u ? CorrelationTag{1u} : CorrelationTag{v + 1u};
    }

    [[nodiscard]] constexpr bool minted_before(CorrelationTag later) const noexcept {
        return static_cast<std::int32_t>(later.v - v) > 0;
    }
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

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        BlockSlotMask m
    ) noexcept {
        o.field(key, m.v);
    }

    friend constexpr bool operator==(BlockSlotMask, BlockSlotMask) = default;
};

struct BlockSize {
    std::uint32_t v = 0;

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

    [[nodiscard]] constexpr BlockCount blocks_in(FileSize total) const noexcept {
        if (v == 0)
            return BlockCount{};

        if (total.high_word() == 0)
            return BlockCount{total.low_word() / v};
        return BlockCount{static_cast<std::uint32_t>(total.v / v)};
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> block_of(std::span<Byte> buf) const noexcept {
        return buf.first(static_cast<std::size_t>(v));
    }

    [[nodiscard]] constexpr os::MapLength map_length() const noexcept { return os::MapLength{v}; }

    [[nodiscard]] constexpr bool power_of_two() const noexcept {
        return v != 0 && (v & (v - 1u)) == 0u;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    friend constexpr bool operator==(BlockSize, BlockSize) = default;
    friend constexpr auto operator<=>(BlockSize, BlockSize) = default;
};

constexpr BlockCount FileSize::units_covering(BlockSize unit) const noexcept {
    return BlockCount{static_cast<std::uint32_t>((v + unit.v - 1) / unit.v)};
}

constexpr BlockCount FileSize::whole_units(BlockSize unit) const noexcept {
    return BlockCount{static_cast<std::uint32_t>(v / unit.v)};
}

constexpr BlockCount SdStatusWord::block_count() const noexcept {
    return BlockCount{((static_cast<std::uint32_t>(v) >> kCountShift) & kCountMask) + 1u};
}

constexpr BlockSize SdStatusWord::block_size() const noexcept {
    return BlockSize{kMinBlockBytes << ((static_cast<std::uint32_t>(v) >> kSizeShift) & kSizeMask)};
}

[[nodiscard]] constexpr FileSize byte_offset(Lba lba, BlockSize bs) noexcept {
    return FileSize{bs.position_of(lba)};
}

[[nodiscard]] constexpr Lba end_lba(FileSize size, BlockSize bs) noexcept {
    return Lba{static_cast<std::uint32_t>(size.v / bs.v)};
}

struct WindowCount {
    std::size_t v = 0;

    [[nodiscard]] constexpr bool fits_within(WindowCount used, WindowCount cap) const noexcept {
        return used.v + v <= cap.v;
    }

    friend constexpr bool operator==(WindowCount, WindowCount) = default;
};

struct BracketCount {
    std::size_t v = 0;

    [[nodiscard]] constexpr bool within(BracketCount cap) const noexcept { return v <= cap.v; }

    friend constexpr bool operator==(BracketCount, BracketCount) = default;
};

struct FieldCount;
struct ValueSet;

struct FieldValue {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool within(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue cycled_forward(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue cycled_back(FieldCount count) const noexcept;

    [[nodiscard]] constexpr FieldValue wrapped_back(FieldCount count) const noexcept;

    [[nodiscard]] constexpr ValueSet bit() const noexcept;

    [[nodiscard]] static constexpr FieldValue flag(bool on) noexcept {
        return FieldValue{on ? 1u : 0u};
    }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        FieldValue f
    ) noexcept {
        o.field(key, f.v);
    }

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

    [[nodiscard]] constexpr bool holds(FieldValue i) const noexcept { return i.within(*this); }

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

struct ValueSet {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool holds(FieldValue value) const noexcept;
    friend constexpr bool operator==(ValueSet, ValueSet) = default;
};

constexpr bool ValueSet::holds(FieldValue value) const noexcept {
    return value.within(FieldCount::of(std::numeric_limits<std::uint16_t>::digits))
        && ((v >> value.v) & 1u) != 0;
}

constexpr ValueSet FieldValue::bit() const noexcept {
    return ValueSet{
        v < std::numeric_limits<std::uint16_t>::digits ? static_cast<std::uint16_t>(1u << v)
                                                       : std::uint16_t{0}
    };
}

enum FieldRep : std::uint32_t {};

[[nodiscard]] inline FieldRep printed_field(FieldValue value) noexcept {
    return static_cast<FieldRep>(value.v);
}

struct RegionCode {
    std::uint8_t v = 0;
    static const RegionCode kNone;
    friend constexpr bool operator==(RegionCode, RegionCode) = default;
};

inline constexpr RegionCode RegionCode::kNone{0xFF};

struct OptionRowCount {
    std::uint8_t v = 0;
    using affine_count = OptionRowCount;
    friend constexpr bool operator==(OptionRowCount, OptionRowCount) = default;
};

struct OptionRowIndex {
    std::uint8_t v = 0;
    using affine_count = OptionRowCount;
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

struct DipRowCount {
    std::uint8_t v = 0;
    using affine_count = DipRowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr DipRowCount of(I n) noexcept {
        return DipRowCount{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool holds(DipRowIndex row) const noexcept { return row.v < v; }

    [[nodiscard]] constexpr DipRowIndex end() const noexcept { return DipRowIndex{v}; }

    friend constexpr bool operator==(DipRowCount, DipRowCount) = default;
    friend constexpr auto operator<=>(DipRowCount, DipRowCount) = default;
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

inline constexpr std::uint8_t kMaxPollParams = 6;

struct PollParam {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr PollParam of(I p) noexcept {
        constexpr std::uint8_t kLast = kMaxPollParams - 1;
        if (std::cmp_greater(p, kLast))
            return PollParam{kLast};
        return PollParam{static_cast<std::uint8_t>(p)};
    }

    friend constexpr bool operator==(PollParam, PollParam) = default;
    friend constexpr auto operator<=>(PollParam, PollParam) = default;
};

struct ParamSet {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool has(PollParam p) const noexcept { return ((v >> p.v) & 1u) != 0u; }

    friend constexpr bool operator==(ParamSet, ParamSet) = default;
};

constexpr PollParam PlayerIndex::poll_param() const noexcept {
    return PollParam::of(v);
}

struct FioWordCount {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FioWordCount saturating_sub(FioWordCount spent) const noexcept {
        return v > spent.v ? FioWordCount{v - spent.v} : FioWordCount{};
    }

    friend constexpr bool operator==(FioWordCount, FioWordCount) = default;
};

struct Attenuation {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kMax = 0x07;

    [[nodiscard]] static constexpr Attenuation of(int step) noexcept {
        return Attenuation{static_cast<std::uint8_t>(static_cast<unsigned>(step) & kMax)};
    }

    friend constexpr bool operator==(Attenuation, Attenuation) = default;
};

enum class VolumeStep : std::uint8_t { Quieter, Louder };

struct LaneCaps {
    std::uint32_t v = 0;
    friend constexpr bool operator==(LaneCaps, LaneCaps) = default;
};

struct OsdGeometry {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(OsdGeometry, OsdGeometry) = default;
};

struct IrqOrdinal {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr os::UioLine line_after(IrqOrdinal pool_first) const noexcept {
        return os::pool_line_at(v, pool_first.v);
    }

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(IrqOrdinal, IrqOrdinal) = default;
    friend constexpr auto operator<=>(IrqOrdinal, IrqOrdinal) = default;
};

struct CauseTag {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(CauseTag, CauseTag) = default;
};

struct CauseBits {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool holds(CauseBits bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    [[nodiscard]] constexpr Error refused(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(CauseBits, CauseBits) = default;
};

}
