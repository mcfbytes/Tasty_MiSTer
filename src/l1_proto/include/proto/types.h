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

#include "hal/reg_field.h"
#include "hal/reg_word.h"
#include "hal/sdram_cfg.h"
#include "hal/spi_halves.h"
#include "infra/affine.h"
#include "infra/ascii.h"
#include "infra/byte_count.h"
#include "infra/decimal_text.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "infra/index_set.h"
#include "infra/indexed_array.h"
#include "infra/json_out.h"
#include "hal/types.h"
#include "infra/xthread/pause_gen.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/serial.h"
#include "os/map_length.h"
#include "os/types.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct DiskUnit;
struct IoIndex;
struct ListIndex;
struct PollParam;
class ILinkFrame;

struct SlotIndex {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint8_t v = 0;

    template <std::unsigned_integral I>
    [[nodiscard]] static constexpr SlotIndex truncating(I i) noexcept {
        return SlotIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr IoIndex io_index() const noexcept;

    [[nodiscard]] constexpr bool in_block_slots() const noexcept;

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept;

    [[nodiscard]] constexpr std::size_t index() const noexcept { return v; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    [[nodiscard]] constexpr bool is_primary() const noexcept { return v == 0; }

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

    [[nodiscard]] constexpr DiskUnit served_unit() const noexcept;

    template <std::size_t Count>
    [[nodiscard]] constexpr bool below() const noexcept {
        return v < Count;
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

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
    return v;
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

    [[nodiscard]] constexpr std::uint16_t keyed_ordinal() const noexcept {
        constexpr std::uint16_t kMountKindBit = 0x100;
        std::uint16_t bits = index.v;
        if (kind == FileRowKind::Mount)
            bits |= kMountKindBit;
        return bits;
    }

    friend constexpr bool operator==(FileRowId, FileRowId) = default;

private:
    constexpr FileRowId(FileRowKind k, IoIndex i) noexcept : kind(k), index(i) {}
};

static_assert(sizeof(FileRowId) == 2 && std::is_trivially_copyable_v<FileRowId>);
static_assert(!std::is_default_constructible_v<FileRowId>);
static_assert(FileRowId::file(IoIndex{1}) != FileRowId::mount(IoIndex{1}));

struct ItemCount;

struct ItemOrdinal {
    std::uint16_t v = 0;
    using affine_count = ItemCount;

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(ItemOrdinal, ItemOrdinal) = default;
};

struct ItemCount {
    std::uint16_t v = 0;
    using affine_count = ItemCount;
    friend constexpr bool operator==(ItemCount, ItemCount) = default;
};

struct WideSub {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr IoIndex slot_index() const noexcept { return IoIndex{v}; }

    friend constexpr bool operator==(WideSub, WideSub) = default;
    friend constexpr auto operator<=>(WideSub, WideSub) = default;
};

class FioIndexByte {
public:
    constexpr FioIndexByte() noexcept = default;

    explicit constexpr FioIndexByte(std::uint8_t byte) noexcept : v{byte} {}

    [[nodiscard]] constexpr IoIndex io() const noexcept {
        constexpr std::uint8_t kIoBits = 0x3F;
        return IoIndex{static_cast<std::uint8_t>(v & kIoBits)};
    }

    [[nodiscard]] constexpr WideSub sub() const noexcept {
        constexpr unsigned kSubShift = 6;
        return WideSub{static_cast<std::uint8_t>(v >> kSubShift)};
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept { return hal::SpiWord{v}; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(FioIndexByte, FioIndexByte) = default;

private:
    friend struct WideIoIndex;
    std::uint8_t v = 0;
};

struct WideIoIndex {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr IoIndex io() const noexcept {
        return IoIndex{static_cast<std::uint8_t>(v & 0x3Fu)};
    }

    [[nodiscard]] constexpr WideSub sub() const noexcept {
        return WideSub{static_cast<std::uint8_t>(v >> 6)};
    }

    [[nodiscard]] constexpr FioIndexByte fio_byte() const noexcept {
        return FioIndexByte{static_cast<std::uint8_t>(v)};
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept { return hal::SpiWord{v}; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return fio_byte().text(); }

    [[nodiscard]] static constexpr WideIoIndex of(IoIndex io, std::uint8_t sub) noexcept {
        return WideIoIndex{
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(sub) << 6) | io.v)
        };
    }

    [[nodiscard]] static constexpr WideIoIndex of(IoIndex io) noexcept { return of(io, 0); }

    [[nodiscard]] static constexpr WideIoIndex of(FioIndexByte byte) noexcept {
        return WideIoIndex{byte.v};
    }

    friend constexpr bool operator==(WideIoIndex, WideIoIndex) = default;
};

static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).io() == IoIndex{0x2A});
static_assert(WideIoIndex::of(IoIndex{0x2A}, 3).sub() == WideSub{3});
static_assert(WideIoIndex::of(IoIndex{2}, 2) == WideIoIndex{0x82});
static_assert(WideIoIndex::of(IoIndex{2}, 4).fio_byte() == FioIndexByte{2});
static_assert(FioIndexByte{0xEA}.io() == IoIndex{0x2A});
static_assert(FioIndexByte{0xEA}.sub() == WideSub{3});
static_assert(WideIoIndex::of(FioIndexByte{0x82}) == WideIoIndex{0x82});
static_assert(WideIoIndex{0x82}.sub() == WideSub{2});
static_assert(WideIoIndex{0x82}.io() == IoIndex{2});

struct PathId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(PathId, PathId) = default;
};

struct BlockCount;
struct BlockSize;
struct Lba;
struct FileOffset;

struct FileSize {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint64_t v = 0;
    static constexpr std::size_t cut_element_bytes = 1;

    [[nodiscard]] static constexpr FileSize of(infra::ByteCount bytes) noexcept {
        return FileSize{static_cast<std::uint64_t>(bytes.v)};
    }

    template <class B>
        requires(sizeof(B) == sizeof(char))
    [[nodiscard]] static constexpr FileSize of(std::span<B> buf) noexcept {
        return FileSize{buf.size_bytes()};
    }

    template <class Buf>
        requires(sizeof(typename Buf::value_type) == sizeof(char))
    [[nodiscard]] Buf zeroed() const {
        return Buf(static_cast<std::size_t>(v), typename Buf::value_type{});
    }

    template <class Buf>
        requires(sizeof(typename Buf::value_type) == sizeof(char))
    void resize_in(Buf& buf) const {
        buf.resize(static_cast<std::size_t>(v));
    }

    template <class Buf>
        requires(sizeof(typename Buf::value_type) == sizeof(char))
    void reserve_in(Buf& buf) const {
        buf.reserve(static_cast<std::size_t>(v));
    }

    [[nodiscard]] constexpr os::SeekLength kernel_length() const noexcept {
        return os::SeekLength{static_cast<std::int64_t>(v)};
    }

    [[nodiscard]] constexpr std::uint32_t low_word() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] constexpr std::uint32_t high_word() const noexcept {
        return static_cast<std::uint32_t>(v >> std::numeric_limits<std::uint32_t>::digits);
    }

    constexpr void store_le32(std::span<std::byte, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, low_word());
    }

    [[nodiscard]] constexpr std::array<std::uint8_t, 3> csd_size_bytes() const noexcept {
        constexpr unsigned kCsdSizeShift0 = 9;
        constexpr unsigned kCsdSizeShift1 = 17;
        constexpr unsigned kCsdSizeShift2 = 25;
        return {
            static_cast<std::uint8_t>(v >> kCsdSizeShift0),
            static_cast<std::uint8_t>(v >> kCsdSizeShift1),
            static_cast<std::uint8_t>(v >> kCsdSizeShift2)
        };
    }

    [[nodiscard]] Ex<void> emit_words(ILinkFrame& frame, hal::Width framing) const;

    [[nodiscard]] constexpr FileSize min(FileSize other) const noexcept {
        return FileSize{std::min(v, other.v)};
    }

    [[nodiscard]] constexpr FileSize advance(FileSize by) const noexcept { return *this + by; }

    [[nodiscard]] constexpr std::uint32_t detail() const noexcept { return low_word(); }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, low_word()};
    }

    [[nodiscard]] constexpr bool within_tolerance_of(
        FileSize want,
        FileSize tolerance
    ) const noexcept {
        const std::uint64_t delta = v > want.v ? v - want.v : want.v - v;
        return delta <= tolerance.v;
    }

    template <std::integral Word>
    [[nodiscard]] constexpr Word file_word() const noexcept {
        constexpr unsigned kHalfBits = 32;
        return static_cast<Word>(static_cast<std::uint64_t>(high_word()) << kHalfBits | low_word());
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    using affine_count = FileSize;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool within(FileSize lo, FileSize hi) const noexcept {
        return lo.v <= v && v <= hi.v;
    }

    [[nodiscard]] constexpr bool holds(FileOffset at) const noexcept;

    [[nodiscard]] constexpr std::size_t clamp_write(FileOffset at, std::size_t want) const noexcept;

    [[nodiscard]] constexpr FileSize after_header(FileSize header) const noexcept {
        return infra::affine::saturating_sub(*this, header);
    }

    [[nodiscard]] constexpr BlockCount units_covering(BlockSize unit) const noexcept;

    [[nodiscard]] constexpr bool reaches(Lba lba, BlockSize bs) const noexcept;

    [[nodiscard]] constexpr BlockCount whole_units(BlockSize unit) const noexcept;

    [[nodiscard]] constexpr std::uint64_t whole_records(FileSize record) const noexcept {
        return v / record.v;
    }

    [[nodiscard]] constexpr FileSize bytes_past_whole(FileSize record) const noexcept {
        return FileSize{v % record.v};
    }

    friend constexpr bool operator==(FileSize, FileSize) = default;
    friend constexpr auto operator<=>(FileSize, FileSize) = default;
};

struct FileOffset {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint64_t v = 0;
    using affine_count = FileSize;
    static constexpr std::size_t cut_element_bytes = 1;

    [[nodiscard]] constexpr FileSize remaining_in(FileSize whole) const noexcept;

    [[nodiscard]] constexpr std::int64_t seek_offset() const noexcept {
        return static_cast<std::int64_t>(v);
    }

    [[nodiscard]] constexpr std::optional<FileOffset> seek_by(std::int64_t by) const noexcept {
        const std::int64_t at = static_cast<std::int64_t>(v) + by;
        if (at < 0)
            return std::nullopt;
        return FileOffset{static_cast<std::uint64_t>(at)};
    }

    [[nodiscard]] constexpr os::SeekOffset kernel_offset() const noexcept {
        return os::SeekOffset{static_cast<std::int64_t>(v)};
    }

    [[nodiscard]] constexpr std::uint64_t seek_word() const noexcept { return v; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, static_cast<std::uint32_t>(v)};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(FileOffset, FileOffset) = default;
    friend constexpr auto operator<=>(FileOffset, FileOffset) = default;
};

inline constexpr FileOffset kFileStart{};
static_assert(infra::affine::Position<FileOffset>);
static_assert(kFileStart + FileSize{5} - kFileStart == FileSize{5});
static_assert(!kFileStart.seek_by(-1) && kFileStart.seek_by(7) == kFileStart + FileSize{7});

constexpr FileSize FileOffset::remaining_in(FileSize whole) const noexcept {
    return whole.after_header(*this - kFileStart);
}

constexpr bool FileSize::holds(FileOffset at) const noexcept {
    return at.v < v;
}

constexpr std::size_t FileSize::clamp_write(FileOffset at, std::size_t want) const noexcept {
    if (!holds(at))
        return 0;
    const std::uint64_t room = v - at.v;
    return static_cast<std::size_t>(std::min<std::uint64_t>(want, room));
}

static_assert((kFileStart + FileSize{5}).remaining_in(FileSize{3}) == FileSize{});

struct Lba;

struct BlockCount {
    std::uint32_t v = 0;
    using affine_count = BlockCount;

    [[nodiscard]] static constexpr BlockCount of(hal::SpiWord w) noexcept {
        return w.decoded([](std::uint16_t b) { return BlockCount{b}; });
    }

    [[nodiscard]] constexpr hal::SpiWord low_word() const noexcept {
        return hal::SpiWord{static_cast<std::uint16_t>(v)};
    }

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

    [[nodiscard]] constexpr bool store_le16(std::span<std::byte, 2> dst) const noexcept {
        if (v > std::numeric_limits<std::uint16_t>::max())
            return false;
        infra::store_le<std::uint16_t>(dst, static_cast<std::uint16_t>(v));
        return true;
    }

    constexpr void store_le16_saturated(std::span<std::byte, 2> dst) const noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint16_t>::max();
        infra::store_le<std::uint16_t>(dst, static_cast<std::uint16_t>(std::min(v, kField)));
    }

    [[nodiscard]] constexpr float whole_turns(BlockCount per_turn) const noexcept {
        return static_cast<float>(*this / per_turn);
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

    [[nodiscard]] static constexpr Lba of(hal::SpiHalves h) noexcept {
        return h.joined([](std::uint32_t d) { return Lba{d}; });
    }

    constexpr void store_le32(std::span<std::byte, 4> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    [[nodiscard]] constexpr bool store_le16(std::span<std::byte, 2> dst) const noexcept {
        if (v > std::numeric_limits<std::uint16_t>::max())
            return false;
        infra::store_le<std::uint16_t>(dst, static_cast<std::uint16_t>(v));
        return true;
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(Lba, Lba) = default;
    friend constexpr auto operator<=>(Lba, Lba) = default;
};

constexpr bool BlockCount::holds(Lba start, BlockCount count) const noexcept {
    return infra::affine::fits(start, count, *this);
}

struct BlockSize;

struct SdStatusWord {
    std::uint16_t v = 0;

    static constexpr hal::RegField<std::uint16_t> kValidField{15, 0x1};
    static constexpr hal::RegField<std::uint16_t> kOpField{0, 0x3};
    static constexpr hal::RegField<std::uint16_t> kSlotField{2, 0xF};
    static constexpr hal::RegField<std::uint16_t> kSizeField{6, 0x7};
    static constexpr hal::RegField<std::uint16_t> kCountField{9, 0x3F};

    enum Layout : std::uint32_t { kWriteOp = 2, kMinBlockBytes = 128 };

    [[nodiscard]] constexpr bool valid() const noexcept { return kValidField.test(v); }

    [[nodiscard]] static constexpr SdStatusWord of(hal::RegWord w) noexcept {
        return SdStatusWord{static_cast<std::uint16_t>(w.bits())};
    }

    [[nodiscard]] static constexpr SdStatusWord of(hal::SpiWord w) noexcept {
        return w.decoded([](std::uint16_t b) { return SdStatusWord{b}; });
    }

    [[nodiscard]] constexpr bool decoded() const noexcept { return kOpField.get(v) != 0u; }

    [[nodiscard]] constexpr bool writes() const noexcept { return kOpField.get(v) == kWriteOp; }

    [[nodiscard]] constexpr SlotIndex slot() const noexcept {
        return SlotIndex{static_cast<std::uint8_t>(kSlotField.get(v))};
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

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept { return hal::SpiWord{v}; }

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

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(PackedBcd, PackedBcd) = default;
};

static_assert(sizeof(PackedBcd) == 1);
static_assert(PackedBcd::of(59) == PackedBcd{0x59});
static_assert(PackedBcd{0x59}.value() == 59);
static_assert(PackedBcd{0x59}.byte() == std::byte{0x59});
static_assert(!PackedBcd::of(100).has_value());

struct MemSizeCookie {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr MemSizeCookie of(std::optional<hal::SdramCfg> cfg) noexcept {
        return cfg ? MemSizeCookie{cfg->cookie()} : MemSizeCookie{};
    }

    [[nodiscard]] constexpr hal::SpiWord word() const noexcept { return hal::SpiWord{v}; }

    [[nodiscard]] constexpr bool is_measured() const noexcept {
        return (v & hal::SdramCfg::kCookiePresent) != 0;
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
    [[nodiscard]] static constexpr OsdRowCount saturating(I n) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, n))
            return OsdRowCount{kField};
        return OsdRowCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept {
        return hal::SpiWord{
            static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max())
        };
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
    [[nodiscard]] static constexpr OsdRow saturating(I i) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, i))
            return OsdRow{kField};
        return OsdRow{static_cast<std::uint32_t>(i)};
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept {
        return hal::SpiWord{
            static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max())
        };
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
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

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(OsdRowBytes, OsdRowBytes) = default;
};

struct OsdCol;

struct OsdColCount {

    std::uint32_t v = 0;
    using affine_count = OsdColCount;

    template <std::integral I>
    [[nodiscard]] static constexpr OsdColCount saturating(I n) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, n))
            return OsdColCount{kField};
        return OsdColCount{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept {
        return hal::SpiWord{
            static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max())
        };
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
    [[nodiscard]] static constexpr OsdCol saturating(I i) noexcept {
        constexpr std::uint32_t kField = std::numeric_limits<std::uint8_t>::max();
        if (std::cmp_less(kField, i))
            return OsdCol{kField};
        return OsdCol{static_cast<std::uint32_t>(i)};
    }

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept {
        return hal::SpiWord{
            static_cast<std::uint16_t>(v & std::numeric_limits<std::uint16_t>::max())
        };
    }

    friend constexpr bool operator==(OsdCol, OsdCol) = default;
    friend constexpr auto operator<=>(OsdCol, OsdCol) = default;
};

template <class T, std::size_t N>
using OsdRowCells = infra::IndexedArray<T, N, OsdRow>;

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
    [[nodiscard]] static constexpr PlayerIndex truncating(I i) noexcept {
        return PlayerIndex{static_cast<std::uint8_t>(i)};
    }

    [[nodiscard]] constexpr PollParam poll_param() const noexcept;

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    static constexpr unsigned kWireShift = 4;
    static constexpr std::uint8_t kPaddleLow = 0x0F;
    static constexpr std::uint8_t kSpinnerLow = 0x8F;

    [[nodiscard]] constexpr std::uint8_t stick_wire_index() const noexcept { return v; }

    [[nodiscard]] constexpr std::uint8_t paddle_wire_index() const noexcept {
        return static_cast<std::uint8_t>((static_cast<unsigned>(v) << kWireShift) | kPaddleLow);
    }

    [[nodiscard]] constexpr std::uint8_t spinner_wire_index() const noexcept {
        return static_cast<std::uint8_t>((static_cast<unsigned>(v) << kWireShift) | kSpinnerLow);
    }

    [[nodiscard]] constexpr PlayerIndex joyswapped() const noexcept {
        constexpr std::uint8_t kSwappablePorts = 2;
        return PlayerIndex{static_cast<std::uint8_t>(v < kSwappablePorts ? v ^ 1u : v)};
    }

    friend constexpr bool operator==(PlayerIndex, PlayerIndex) = default;
    friend constexpr auto operator<=>(PlayerIndex, PlayerIndex) = default;
};

struct JoyMask {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool any(JoyMask bits) const noexcept { return (v & bits.v) != 0u; }

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

    [[nodiscard]] constexpr hal::SpiHalves halves() const noexcept { return hal::SpiHalves::of(v); }

    friend constexpr bool operator==(JoyMask, JoyMask) = default;
};

struct OsdMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool intersects(OsdMask bits) const noexcept {
        return (v & bits.v) != 0;
    }

    [[nodiscard]] constexpr OsdMask only(OsdMask bits) const noexcept {
        return OsdMask{static_cast<std::uint16_t>(v & bits.v)};
    }

    [[nodiscard]] constexpr OsdMask with(OsdMask bits) const noexcept {
        return OsdMask{static_cast<std::uint16_t>(v | bits.v)};
    }

    [[nodiscard]] static constexpr OsdMask of(hal::SpiWord w) noexcept {
        return w.decoded([](std::uint16_t b) { return OsdMask{b}; });
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
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;

    static const StorageSeq kRefused;

    [[nodiscard]] constexpr StorageSeq next() const noexcept { return infra::serial::next(*this); }

    friend constexpr bool operator==(StorageSeq, StorageSeq) = default;
};

inline constexpr StorageSeq StorageSeq::kRefused{0};
static_assert(StorageSeq{0xFFFFFFFFu}.next() == StorageSeq{1});

struct BindGeneration {
    TASTY_SEAT_EXEMPT(const_shared);
    std::uint16_t v = 0;

    static constexpr std::uint32_t kMaxGen = 0xFFFFu;
    using serial_rule = infra::serial::SkipZero<std::uint16_t, kMaxGen>;
    using serial_order = infra::serial::Order;

    [[nodiscard]] constexpr BindGeneration next() const noexcept {
        return infra::serial::next(*this);
    }

    [[nodiscard]] constexpr bool ahead_of(BindGeneration earlier) const noexcept {
        return infra::serial::ahead(*this, earlier.v);
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    [[nodiscard]] constexpr bool is_unminted() const noexcept { return v == 0; }

    [[nodiscard]] constexpr xthread::PauseGen pause_gen() const noexcept {
        return xthread::PauseGen{v};
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(BindGeneration, BindGeneration) = default;
};

static_assert(sizeof(BindGeneration) == 2);
static_assert(BindGeneration{BindGeneration::kMaxGen}.next() == BindGeneration{1});

struct StageSlot {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool within(StageSlot slots) const noexcept { return v < slots.v; }

    [[nodiscard]] constexpr std::size_t index() const noexcept {
        const std::size_t place = v;
        return place;
    }

    friend constexpr bool operator==(StageSlot, StageSlot) = default;
};

struct StageGen {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;

    [[nodiscard]] constexpr StageGen next() const noexcept { return infra::serial::next(*this); }

    [[nodiscard]] constexpr bool is_set() const noexcept { return v != 0; }

    [[nodiscard]] constexpr StageSlot slot_in(StageSlot slots) const noexcept {
        const std::uint8_t place = static_cast<std::uint8_t>(v % slots.v);
        return StageSlot{place};
    }

    [[nodiscard]] constexpr std::uint32_t stamp() const noexcept { return v; }

    friend constexpr bool operator==(StageGen, StageGen) = default;
};

static_assert(sizeof(StageGen) == 4);
static_assert(StageGen{0xFFFFFFFFu}.next() == StageGen{1});

struct MountEpoch {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::ThroughZero<std::uint32_t>;

    [[nodiscard]] constexpr MountEpoch next() const noexcept { return infra::serial::next(*this); }

    friend constexpr bool operator==(MountEpoch, MountEpoch) = default;
};

struct ReplyMask {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr hal::SpiWord wire_word() const noexcept { return hal::SpiWord{v}; }

    [[nodiscard]] constexpr bool any() const noexcept { return v != 0; }

    [[nodiscard]] constexpr bool within(ReplyMask lane) const noexcept {
        return (v & ~lane.v) == 0;
    }

    friend constexpr bool operator==(ReplyMask, ReplyMask) = default;
};

struct ReplyWord {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr bool nonzero() const noexcept { return v != 0; }

    [[nodiscard]] constexpr bool intersects(ReplyMask m) const noexcept { return (v & m.v) != 0; }

    [[nodiscard]] constexpr ReplyWord masked(ReplyMask m) const noexcept {
        return ReplyWord{static_cast<std::uint16_t>(v & m.v)};
    }

    [[nodiscard]] constexpr std::size_t nibble() const noexcept {
        constexpr std::uint16_t kHeadNibble = 0x000F;
        return v & kHeadNibble;
    }

    [[nodiscard]] static constexpr ReplyWord of(hal::SpiWord w) noexcept {
        return w.decoded([](std::uint16_t b) { return ReplyWord{b}; });
    }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(ReplyWord, ReplyWord) = default;
};

static_assert(sizeof(ReplyWord) == 2);

struct CompanionBindGen {
    std::uint16_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint16_t>;
    using serial_order = infra::serial::Order;

    [[nodiscard]] constexpr CompanionBindGen next() const noexcept {
        return infra::serial::next(*this);
    }

    [[nodiscard]] constexpr bool at_or_past(CompanionBindGen key) const noexcept {
        return infra::serial::at_or_past(*this, key.v);
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

struct RxDir {};

struct TxDir {};

struct FilePoolDir {};

template <class Dir>
struct SlabId {
    TASTY_SEAT_MEDIATOR(Any, Any);
    std::uint16_t v = 0;
    static const SlabId kNone;

    template <std::uint16_t Usable>
    [[nodiscard]] constexpr std::optional<SlotIndex> slot_in() const noexcept {
        if (*this == kNone || v > Usable)
            return std::nullopt;
        return SlotIndex::truncating(v - 1u);
    }

    [[nodiscard]] static constexpr SlabId of_slot(SlotIndex i) noexcept {
        return SlabId{static_cast<std::uint16_t>(i.v + 1)};
    }

    [[nodiscard]] constexpr std::uint16_t number() const noexcept { return v; }

    friend constexpr bool operator==(SlabId, SlabId) = default;
};
template <class Dir>
inline constexpr SlabId<Dir> SlabId<Dir>::kNone{0};

using RxSlabId = SlabId<RxDir>;
using TxSlabId = SlabId<TxDir>;

struct FileId {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr SlabId<FilePoolDir> slab() const noexcept {
        return SlabId<FilePoolDir>{v};
    }

    [[nodiscard]] static constexpr FileId of_slot(SlotIndex i) noexcept {
        return FileId{SlabId<FilePoolDir>::of_slot(i).number()};
    }

    [[nodiscard]] constexpr PathId path() const noexcept { return PathId{v}; }

    template <std::uint16_t Usable>
    [[nodiscard]] constexpr std::optional<SlotIndex> pooled_slot() const noexcept {
        return slab().slot_in<Usable>();
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(FileId, FileId) = default;
};

struct IdentityStamp {
    std::uint16_t v = 0;

    static constexpr std::uint32_t kMax = 0xFFFFu;
    using serial_rule = infra::serial::SkipZero<std::uint16_t, kMax>;

    [[nodiscard]] constexpr IdentityStamp next() const noexcept {
        return infra::serial::next(*this);
    }

    [[nodiscard]] constexpr bool is_none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(IdentityStamp, IdentityStamp) = default;
};

static_assert(IdentityStamp{IdentityStamp::kMax}.next() == IdentityStamp{1});
static_assert(IdentityStamp{}.is_none());

struct CorrelationTag {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;
    using serial_order = infra::serial::Order;

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{v}; }

    friend constexpr bool operator==(CorrelationTag, CorrelationTag) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }

    [[nodiscard]] constexpr CorrelationTag next() const noexcept {
        return infra::serial::next(*this);
    }

    [[nodiscard]] constexpr bool minted_before(CorrelationTag later) const noexcept {
        return infra::serial::ahead(later, v);
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
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;
    using serial_order = infra::serial::Order;
    friend constexpr bool operator==(ActKey, ActKey) = default;

    explicit constexpr operator bool() const noexcept { return v != 0; }

    [[nodiscard]] constexpr ActKey next() const noexcept { return infra::serial::next(*this); }

    [[nodiscard]] constexpr bool at_or_past(ActKey key) const noexcept {
        return infra::serial::at_or_past(*this, key.v);
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

    static constexpr std::uint8_t kDigitFirst = 1;
    static constexpr std::uint8_t kDigitLast = 9;

    [[nodiscard]] constexpr SubmenuNo digit_page_or_root() const noexcept;

    [[nodiscard]] constexpr SubmenuNo next() const noexcept {
        return SubmenuNo{
            v == std::numeric_limits<std::uint8_t>::max() ? v : static_cast<std::uint8_t>(v + 1u)
        };
    }

    friend constexpr bool operator==(SubmenuNo, SubmenuNo) = default;
};

inline constexpr SubmenuNo kRootSubmenu{0};

constexpr SubmenuNo SubmenuNo::digit_page_or_root() const noexcept {
    return v >= kDigitFirst && v <= kDigitLast ? *this : kRootSubmenu;
}

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

    static constexpr std::size_t kBits = 16;

    [[nodiscard]] constexpr bool contains(SlotIndex slot) const noexcept {
        return bits().contains(slot);
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits().empty(); }

    [[nodiscard]] constexpr BlockSlotMask with(SlotIndex slot) const noexcept {
        return BlockSlotMask{bits().with(slot).v};
    }

    [[nodiscard]] constexpr BlockSlotMask with_or_all(SlotIndex slot) const noexcept {
        return BlockSlotMask{bits().with_or_all(slot).v};
    }

    [[nodiscard]] constexpr BlockSlotMask without(SlotIndex slot) const noexcept {
        return BlockSlotMask{bits().without(slot).v};
    }

    [[nodiscard]] constexpr BlockSlotMask united(BlockSlotMask o) const noexcept {
        return BlockSlotMask{bits().with(Bits{o.v}).v};
    }

    [[nodiscard]] constexpr BlockSlotMask common(BlockSlotMask o) const noexcept {
        return BlockSlotMask{bits().only(Bits{o.v}).v};
    }

    [[nodiscard]] constexpr BlockSlotMask minus(BlockSlotMask o) const noexcept {
        return BlockSlotMask{bits().without(Bits{o.v}).v};
    }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        BlockSlotMask m
    ) noexcept {
        o.field(key, m.v);
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(BlockSlotMask, BlockSlotMask) = default;

private:
    using Bits = infra::IndexSet<SlotIndex, kBits, std::uint16_t>;

    [[nodiscard]] constexpr Bits bits() const noexcept { return Bits{v}; }
};

struct BlockSize {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr FileOffset position_of(Lba lba) const noexcept {
        return FileOffset{static_cast<std::uint64_t>(lba.v) * v};
    }

    [[nodiscard]] constexpr FileSize image_bytes(Lba end) const noexcept {
        return position_of(end) - kFileStart;
    }

    [[nodiscard]] constexpr std::uint32_t block_bytes() const noexcept { return v; }

    [[nodiscard]] constexpr std::uint32_t window_bytes(BlockCount n) const noexcept {
        return n.v * v;
    }

    [[nodiscard]] constexpr infra::ByteCount window_count(BlockCount n) const noexcept {
        return infra::ByteCount{window_bytes(n)};
    }

    [[nodiscard]] constexpr FileSize bytes_of(BlockCount n) const noexcept {
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
        return buf.first(block_bytes());
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
    return BlockCount{static_cast<std::uint32_t>(infra::affine::div_ceil(v, unit.v))};
}

constexpr BlockCount FileSize::whole_units(BlockSize unit) const noexcept {
    return unit.blocks_in(*this);
}

constexpr bool FileSize::reaches(Lba lba, BlockSize bs) const noexcept {

    return bs.v == 0 ? lba.v == 0 : static_cast<std::uint64_t>(lba.v) <= v / bs.v;
}

constexpr BlockCount SdStatusWord::block_count() const noexcept {
    return BlockCount{static_cast<std::uint32_t>(kCountField.get(v)) + 1u};
}

constexpr BlockSize SdStatusWord::block_size() const noexcept {
    return BlockSize{kMinBlockBytes << kSizeField.get(v)};
}

[[nodiscard]] constexpr Lba end_lba(FileSize size, BlockSize bs) noexcept {
    return Lba{bs.blocks_in(size).v};
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

    [[nodiscard]] constexpr bool has_bit(unsigned i) const noexcept {
        return ((v >> i) & 1u) != 0u;
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
    [[nodiscard]] static constexpr FieldCount saturating(I n) noexcept {
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

    [[nodiscard]] constexpr bool contains(FieldValue value) const noexcept;
    friend constexpr bool operator==(ValueSet, ValueSet) = default;

private:
    using Bits = infra::
        IndexSet<FieldValue, std::numeric_limits<std::uint16_t>::digits, std::uint16_t>;

    [[nodiscard]] constexpr Bits bits() const noexcept { return Bits{v}; }
};

constexpr bool ValueSet::contains(FieldValue value) const noexcept {
    return bits().contains(value);
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

    [[nodiscard]] constexpr bool is_clear() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RegionCode with(RegionCode other) const noexcept {
        return RegionCode{static_cast<std::uint8_t>(v | other.v)};
    }

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

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(OptionRowIndex, OptionRowIndex) = default;
    friend constexpr auto operator<=>(OptionRowIndex, OptionRowIndex) = default;
};

struct CoreBaySlot {
    std::uint8_t v = 0;

    template <std::size_t N>
    [[nodiscard]] constexpr bool within() const noexcept {
        return v < N;
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(CoreBaySlot, CoreBaySlot) = default;
};

inline constexpr CoreBaySlot kNoBaySlot{0xFF};

struct DiskUnit {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SlotIndex slot() const noexcept { return SlotIndex{v}; }

    [[nodiscard]] static constexpr DiskUnit of(SlotIndex slot) noexcept { return DiskUnit{slot.v}; }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(DiskUnit, DiskUnit) = default;
};

constexpr DiskUnit IoIndex::served_unit() const noexcept {
    return DiskUnit{v};
}

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

    struct Ps2PacketBits {
        std::uint8_t flags;
        std::uint16_t word1;
        std::uint16_t word2;
        friend constexpr bool operator==(Ps2PacketBits, Ps2PacketBits) = default;
    };

    static constexpr std::uint8_t kButtonMask = 0x07;
    static constexpr std::uint16_t kMidButtonBits = 0x0F00;
    static constexpr unsigned kMidButtonShift = 5;
    static constexpr std::uint16_t kRightButtonBits = 0x0100;
    static constexpr unsigned kRightButtonShift = 1;

    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool intersects(MouseButtons bits) const noexcept {
        return (v & bits.v) != 0;
    }

    [[nodiscard]] constexpr Ps2PacketBits ps2_packet_bits() const noexcept {
        return Ps2PacketBits{
            static_cast<std::uint8_t>(v & kButtonMask),
            static_cast<std::uint16_t>(
                (static_cast<unsigned>(v) << kMidButtonShift) & kMidButtonBits
            ),
            static_cast<std::uint16_t>(
                (static_cast<unsigned>(v) << kRightButtonShift) & kRightButtonBits
            )
        };
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
    [[nodiscard]] static constexpr DipRowIndex truncating(I n) noexcept {
        return DipRowIndex{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(DipRowIndex, DipRowIndex) = default;
    friend constexpr auto operator<=>(DipRowIndex, DipRowIndex) = default;
};

struct DipRowCount {
    std::uint8_t v = 0;
    using affine_count = DipRowCount;

    template <std::integral I>
    [[nodiscard]] static constexpr DipRowCount truncating(I n) noexcept {
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
    [[nodiscard]] static constexpr DipChoiceCount truncating(I n) noexcept {
        return DipChoiceCount{static_cast<std::uint8_t>(n)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    template <class C>
    [[nodiscard]] constexpr DipChoiceCount capped_to(const C& c) const noexcept {
        return v < std::size(c) ? *this : truncating(std::size(c));
    }

    template <class F>
    constexpr void each(F&& f) const;
    friend constexpr bool operator==(DipChoiceCount, DipChoiceCount) = default;
    friend constexpr auto operator<=>(DipChoiceCount, DipChoiceCount) = default;
};

struct DipChoice {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr DipChoice truncating(I n) noexcept {
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

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
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

using ParamSet = infra::IndexSet<PollParam, kMaxPollParams, std::uint8_t>;

constexpr PollParam PlayerIndex::poll_param() const noexcept {
    return PollParam::of(v);
}

struct ReplyCount;

struct FioWordCount {
    std::uint32_t v = 0;
    using affine_count = FioWordCount;

    [[nodiscard]] constexpr FioWordCount times(ReplyCount n) const noexcept;

    [[nodiscard]] constexpr std::size_t bytes_at(hal::Width w) const noexcept {
        return std::size_t{v} * hal::word_bytes(w);
    }

    [[nodiscard]] constexpr infra::ByteCount byte_count_at(hal::Width w) const noexcept {
        return infra::ByteCount{bytes_at(w)};
    }

    [[nodiscard]] static constexpr FioWordCount whole_in(
        infra::ByteCount bytes,
        hal::Width w
    ) noexcept {
        constexpr std::size_t kMax = std::numeric_limits<std::uint32_t>::max();
        return FioWordCount{
            static_cast<std::uint32_t>(std::min(bytes.v / hal::word_bytes(w), kMax))
        };
    }

    [[nodiscard]] static constexpr FioWordCount carrying(FileSize bytes, hal::Width w) noexcept {
        return FioWordCount{
            static_cast<std::uint32_t>(hal::words(w, static_cast<std::size_t>(bytes.v)))
        };
    }

    friend constexpr bool operator==(FioWordCount, FioWordCount) = default;
    friend constexpr auto operator<=>(FioWordCount, FioWordCount) = default;
};

struct ReplyCount {
    std::uint8_t v = 0;
    using affine_count = ReplyCount;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    [[nodiscard]] constexpr FioWordCount link_words() const noexcept { return FioWordCount{v}; }

    friend constexpr bool operator==(ReplyCount, ReplyCount) = default;
    friend constexpr auto operator<=>(ReplyCount, ReplyCount) = default;
};

constexpr FioWordCount FioWordCount::times(ReplyCount n) const noexcept {
    return FioWordCount{v * n.link_words().v};
}

static_assert(FioWordCount{3}.times(ReplyCount{4}) == FioWordCount{12});

struct ReplyIndex {
    std::uint8_t v = 0;
    using affine_count = ReplyCount;
    using affine_index = ReplyIndex;
    friend constexpr bool operator==(ReplyIndex, ReplyIndex) = default;
    friend constexpr auto operator<=>(ReplyIndex, ReplyIndex) = default;
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

struct IrqOrdinal {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr IrqOrdinal of(hal::RegWord w) noexcept {
        return IrqOrdinal{w.bits()};
    }

    [[nodiscard]] constexpr os::UioLine line_after(IrqOrdinal pool_first) const noexcept {
        return os::pool_line_at(v, pool_first.v);
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(IrqOrdinal, IrqOrdinal) = default;
    friend constexpr auto operator<=>(IrqOrdinal, IrqOrdinal) = default;
};

struct EntryTag {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr EntryTag of(hal::RegWord w) noexcept {
        return EntryTag{w.bits()};
    }

    [[nodiscard]] constexpr bool framework() const noexcept { return v == 0; }

    friend constexpr bool operator==(EntryTag, EntryTag) = default;
};

struct CauseBits {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr CauseBits of(hal::RegWord w) noexcept {
        return CauseBits{w.bits()};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool holds(CauseBits bits) const noexcept {
        return (v & bits.v) == bits.v;
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(CauseBits, CauseBits) = default;
};

enum class IrqClass : std::uint8_t { Blk, Fifo, Ring, CoreTick, kCount };

enum class CoreType : std::uint8_t {
    Unknown = 0x55,
    EightBit = 0xA4,
    SharpMz = 0xA7,
};

inline constexpr hal::CoreTypeByte kEightBitWire{0xA4};
inline constexpr hal::CoreTypeByte kPrivateDialectWire{0xA7};

[[nodiscard]] constexpr CoreType fold_core_type(hal::CoreTypeByte type_byte) noexcept {
    if (type_byte == kEightBitWire || type_byte.dual_sdram())
        return CoreType::EightBit;
    if (type_byte == kPrivateDialectWire)
        return CoreType::SharpMz;
    return CoreType::Unknown;
}

}
