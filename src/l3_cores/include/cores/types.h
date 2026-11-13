// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <ranges>
#include <tuple>
#include <optional>
#include <span>

#include "infra/decimal_text.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/affine.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::svc {
struct Msf;
}

namespace mister::cores {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

using proto::FileSize;
using proto::IoIndex;
using proto::PathId;
using proto::WideIoIndex;

struct CoreTypeMask {
    std::uint8_t v = 0;
    friend constexpr bool operator==(CoreTypeMask, CoreTypeMask) = default;
};

inline constexpr CoreTypeMask kAnyDialect{0xFF};

[[nodiscard]] constexpr bool applies(CoreTypeMask row, CoreTypeMask dialect) noexcept {
    return (row.v & dialect.v) != 0u;
}

struct CoreWindowSlot {
    std::uint8_t v = 0;

    template <class Row>
    [[nodiscard]] constexpr infra::OptRef<const Row> in(std::span<const Row> table) const noexcept {
        if (v >= table.size())
            return {};
        return infra::OptRef<const Row>{table[v]};
    }

    friend constexpr bool operator==(CoreWindowSlot, CoreWindowSlot) = default;
};

inline constexpr std::uint8_t kPhysicalWindow = 0xFF;

struct WindowRef {
    std::uint8_t v = kPhysicalWindow;

    [[nodiscard]] static constexpr WindowRef physical() noexcept { return WindowRef{}; }

    [[nodiscard]] static constexpr WindowRef at(CoreWindowSlot slot) noexcept {
        return WindowRef{slot.v};
    }

    [[nodiscard]] constexpr bool is_physical() const noexcept { return v == kPhysicalWindow; }

    [[nodiscard]] constexpr std::optional<CoreWindowSlot> slot() const noexcept {
        if (is_physical())
            return std::nullopt;
        return CoreWindowSlot{v};
    }

    friend constexpr bool operator==(WindowRef, WindowRef) = default;
};

struct AcsiTarget {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr proto::DiskUnit unit() const noexcept { return proto::DiskUnit{v}; }

    [[nodiscard]] static constexpr AcsiTarget of(proto::DiskUnit drive) noexcept {
        return AcsiTarget{drive.v};
    }

    friend constexpr bool operator==(AcsiTarget, AcsiTarget) = default;
    friend constexpr auto operator<=>(AcsiTarget, AcsiTarget) = default;
};

struct ConfigSlot {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool within(ConfigSlot limit) const noexcept { return v < limit.v; }

    [[nodiscard]] constexpr char digit() const noexcept { return static_cast<char>('0' + v); }

    friend constexpr bool operator==(ConfigSlot, ConfigSlot) = default;
};

struct WordId {
    std::uint8_t v = 0;
    friend constexpr bool operator==(WordId, WordId) = default;
};

struct ChoiceCount {
    std::uint8_t v = 0;
    inline static constexpr std::uint8_t kAll = 0;

    [[nodiscard]] static constexpr ChoiceCount all() noexcept { return ChoiceCount{kAll}; }

    [[nodiscard]] constexpr bool is_all() const noexcept { return v == kAll; }

    [[nodiscard]] constexpr proto::FieldCount reach(proto::FieldCount declared) const noexcept {
        return is_all() ? declared : proto::FieldCount{v};
    }

    friend constexpr bool operator==(ChoiceCount, ChoiceCount) = default;
    friend constexpr auto operator<=>(ChoiceCount, ChoiceCount) = default;
};

struct OptionTag {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::uint8_t wire() const noexcept { return v; }

    friend constexpr bool operator==(OptionTag, OptionTag) = default;
    friend constexpr auto operator<=>(OptionTag, OptionTag) = default;
};

struct JoyBit {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr proto::JoyMask mask() const noexcept { return proto::JoyMask{1u << v}; }

    [[nodiscard]] constexpr bool fits() const noexcept {
        return v < std::numeric_limits<decltype(proto::JoyMask::v)>::digits;
    }

    friend constexpr bool operator==(JoyBit, JoyBit) = default;
};

struct BaudRate {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::uint16_t low_word() const noexcept {
        return static_cast<std::uint16_t>(v);
    }

    [[nodiscard]] constexpr std::uint16_t high_word() const noexcept {
        constexpr unsigned kWordBits = 16;
        return static_cast<std::uint16_t>(v >> kWordBits);
    }

    friend constexpr bool operator==(BaudRate, BaudRate) = default;
    friend constexpr auto operator<=>(BaudRate, BaudRate) = default;
};

inline constexpr BaudRate kDefaultUartBaud{115200};

struct SaveRow {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SaveRow next() const noexcept {
        return SaveRow{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr Error refused(Errc code, infra::ErrorSite site) const noexcept {
        return Error{code, site, v};
    }

    friend constexpr bool operator==(SaveRow, SaveRow) = default;
};

[[nodiscard]] constexpr std::uint32_t detail_of(SaveRow row) noexcept {
    return static_cast<std::uint32_t>(row.v);
}

struct SectorBytes {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr SectorBytes of(std::size_t bytes) noexcept {
        return SectorBytes{static_cast<std::uint32_t>(bytes)};
    }

    friend constexpr bool operator==(SectorBytes, SectorBytes) = default;
};

struct SectorCount {
    std::int32_t v = 0;
    using affine_count = SectorCount;

    [[nodiscard]] static constexpr SectorCount of(proto::BlockCount n) noexcept {
        return SectorCount{static_cast<std::int32_t>(n.v)};
    }

    [[nodiscard]] constexpr SectorCount magnitude() const noexcept {
        return SectorCount{v < 0 ? -v : v};
    }

    [[nodiscard]] constexpr proto::FileSize bytes(SectorBytes frame_bytes) const noexcept {
        if (v < 0)
            return proto::FileSize{};
        return proto::FileSize{static_cast<std::uint64_t>(v) * frame_bytes.v};
    }

    [[nodiscard]] constexpr std::uint32_t to_wire() const noexcept {
        return static_cast<std::uint32_t>(v);
    }

    [[nodiscard]] constexpr svc::Msf stock_msf() const noexcept;

    [[nodiscard]] constexpr svc::Msf wrapped_msf() const noexcept;
    friend constexpr bool operator==(SectorCount, SectorCount) = default;
    friend constexpr auto operator<=>(SectorCount, SectorCount) = default;
};

struct HeadLba {
    std::int32_t v = 0;
    using affine_count = SectorCount;

    [[nodiscard]] static constexpr HeadLba of(proto::Lba lba) noexcept {
        return HeadLba{static_cast<std::int32_t>(lba.v)};
    }

    [[nodiscard]] static constexpr HeadLba of(svc::Msf msf) noexcept;

    [[nodiscard]] constexpr std::optional<proto::Lba> readable() const noexcept {
        if (v < 0)
            return std::nullopt;
        return proto::Lba{static_cast<std::uint32_t>(v)};
    }

    template <class Row>
    [[nodiscard]] constexpr const Row& row_in(std::span<const Row> table) const noexcept {
        const std::int32_t rows = static_cast<std::int32_t>(table.size());
        const std::int32_t row = v % rows;
        return table[static_cast<std::size_t>(row < 0 ? row + rows : row)];
    }

    [[nodiscard]] constexpr svc::Msf msf() const noexcept;

    [[nodiscard]] constexpr svc::Msf stock_msf() const noexcept;

    [[nodiscard]] constexpr svc::Msf wrapped_msf() const noexcept;
    friend constexpr bool operator==(HeadLba, HeadLba) = default;
    friend constexpr auto operator<=>(HeadLba, HeadLba) = default;
};

struct HeadMove {
    HeadLba from{};
    HeadLba to{};

    [[nodiscard]] constexpr SectorCount distance() const noexcept { return to - from; }

    [[nodiscard]] constexpr bool is_backward() const noexcept { return to < from; }

    friend constexpr bool operator==(HeadMove, HeadMove) = default;
};

enum class DiscType : std::uint8_t { CdRom = 0x00, CdRomXa = 0x20 };

struct ReplayLead {
    std::int32_t v = 0;

    [[nodiscard]] constexpr std::int32_t frames() const noexcept { return v; }

    template <std::integral I>
    [[nodiscard]] constexpr bool fits_in() const noexcept {
        return v >= std::numeric_limits<I>::min() && v <= std::numeric_limits<I>::max();
    }

    [[nodiscard]] constexpr infra::DecimalText text() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr bool operator==(ReplayLead, ReplayLead) = default;
    friend constexpr auto operator<=>(ReplayLead, ReplayLead) = default;
};

struct ClockHz {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr bool valid() const noexcept { return v != 0; }

    friend constexpr bool operator==(ClockHz, ClockHz) = default;
};

struct CoreClocks {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr bool zero() const noexcept { return v == 0; }

    [[nodiscard]] constexpr std::optional<std::chrono::nanoseconds> span(
        ClockHz hz
    ) const noexcept {
        if (!hz.valid())
            return std::nullopt;
        constexpr std::uint64_t kNsPerSecond = 1'000'000'000u;
        const std::uint64_t whole = v / hz.v;
        if (whole > std::numeric_limits<std::uint64_t>::max() / kNsPerSecond)
            return std::nullopt;
        const std::uint64_t whole_ns = whole * kNsPerSecond;
        const std::uint64_t part_ns = (v % hz.v) * kNsPerSecond / hz.v;
        const auto max_ns = static_cast<std::uint64_t>(std::chrono::nanoseconds::max().count());
        if (whole_ns > max_ns - part_ns)
            return std::nullopt;
        return std::chrono::nanoseconds{static_cast<std::int64_t>(whole_ns + part_ns)};
    }

    friend constexpr bool operator==(CoreClocks, CoreClocks) = default;
};

struct LateFrames {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::uint8_t count() const noexcept { return v; }

    [[nodiscard]] constexpr bool any() const noexcept { return v != 0; }

    [[nodiscard]] constexpr auto slips() const noexcept {
        return std::views::iota(1u, static_cast<unsigned>(v) + 1u) | std::views::reverse;
    }

    friend constexpr bool operator==(LateFrames, LateFrames) = default;
};

namespace mra {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct RomLength;

struct RepeatCount {
    std::uint32_t v = 0;
    using affine_count = RepeatCount;

    [[nodiscard]] static constexpr std::optional<RepeatCount> of_attribute(
        std::int32_t repeat
    ) noexcept {
        if (repeat < 0)
            return std::nullopt;
        return RepeatCount{static_cast<std::uint32_t>(repeat)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RomLength length_of(
        std::span<const std::uint8_t> pattern
    ) const noexcept;
    friend constexpr bool operator==(RepeatCount, RepeatCount) = default;
    friend constexpr auto operator<=>(RepeatCount, RepeatCount) = default;
};

struct UnitCount {
    std::uint32_t v = 0;
    using affine_count = UnitCount;

    [[nodiscard]] constexpr RomLength times(RomLength stride) const noexcept;
    friend constexpr bool operator==(UnitCount, UnitCount) = default;
    friend constexpr auto operator<=>(UnitCount, UnitCount) = default;
};

struct RomLength {
    std::uint32_t v = 0;
    using affine_count = RomLength;

    [[nodiscard]] static constexpr RomLength of(std::span<const std::uint8_t> bytes) noexcept {
        return RomLength{static_cast<std::uint32_t>(bytes.size())};
    }

    [[nodiscard]] static constexpr RomLength of(proto::FileSize size) noexcept {
        return RomLength{static_cast<std::uint32_t>(size.v)};
    }

    [[nodiscard]] static constexpr std::optional<RomLength> of_attribute(
        std::int32_t length
    ) noexcept {
        if (length <= 0)
            return std::nullopt;
        return RomLength{static_cast<std::uint32_t>(length)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RomLength min(RomLength other) const noexcept {
        return RomLength{v < other.v ? v : other.v};
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    template <class T>
    [[nodiscard]] constexpr std::span<T> first_of(std::span<T> s) const noexcept {
        return s.first(v < s.size() ? v : s.size());
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> phase_in(std::span<T> period) const noexcept {
        return period.subspan(v % period.size());
    }

    [[nodiscard]] constexpr UnitCount units_covering(RomLength unit) const noexcept {
        return UnitCount{(v + unit.v - 1u) / unit.v};
    }

    [[nodiscard]] constexpr proto::FileSize file_size() const noexcept {
        return proto::FileSize{v};
    }

    friend constexpr bool operator==(RomLength, RomLength) = default;
    friend constexpr auto operator<=>(RomLength, RomLength) = default;
};

constexpr RomLength UnitCount::times(RomLength stride) const noexcept {
    return stride * v;
}

constexpr RomLength RepeatCount::length_of(std::span<const std::uint8_t> pattern) const noexcept {
    return RomLength{static_cast<std::uint32_t>(static_cast<std::uint64_t>(v) * pattern.size())};
}

struct RomOffset {
    std::uint32_t v = 0;
    using affine_count = RomLength;

    [[nodiscard]] static constexpr std::optional<RomOffset> of_attribute(
        std::int32_t offset
    ) noexcept {
        if (offset < 0)
            return std::nullopt;
        return RomOffset{static_cast<std::uint32_t>(offset)};
    }

    [[nodiscard]] constexpr std::optional<RomOffset> read_from(std::uint64_t off) const noexcept {
        if (off >= v)
            return std::nullopt;
        return RomOffset{static_cast<std::uint32_t>(off)};
    }

    [[nodiscard]] constexpr RomLength span_to(RomOffset cap) const noexcept {
        return v >= cap.v ? RomLength{} : RomLength{cap.v - v};
    }

    [[nodiscard]] constexpr RomLength distance_from_origin() const noexcept { return RomLength{v}; }

    [[nodiscard]] constexpr std::optional<RomLength> repeats_within(
        RomOffset cap,
        std::span<const std::uint8_t> pattern,
        RepeatCount repeat
    ) const noexcept {
        const RomLength len = repeat.length_of(pattern);
        if (len > span_to(cap))
            return std::nullopt;
        return len;
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> before(std::span<T> s) const noexcept {
        return RomLength{v}.first_of(s);
    }

    template <class T>
    [[nodiscard]] constexpr std::span<T> onward(std::span<T> s) const noexcept {
        return s.subspan(v < s.size() ? v : s.size());
    }

    friend constexpr bool operator==(RomOffset, RomOffset) = default;
    friend constexpr auto operator<=>(RomOffset, RomOffset) = default;
};

struct DipWord {
    using Bytes = std::array<std::uint8_t, sizeof(std::uint64_t)>;
    std::uint64_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr DipWord ones(I width) noexcept {
        constexpr int kBits = std::numeric_limits<std::uint64_t>::digits;
        return DipWord{width <= 0 ? 0 : ~std::uint64_t{0} >> (kBits - width)};
    }

    template <std::integral I>
    [[nodiscard]] constexpr DipWord shifted(I by) const noexcept {
        return DipWord{v << by};
    }

    template <std::integral K, std::integral B>
    [[nodiscard]] constexpr DipWord with_byte(K k, B b) const noexcept {
        constexpr unsigned kByteBits = 8;
        return DipWord{
            v | (static_cast<std::uint64_t>(b) << (kByteBits * static_cast<unsigned>(k)))
        };
    }

    [[nodiscard]] constexpr DipWord with(DipWord mask, DipWord val) const noexcept {
        return DipWord{(v & ~mask.v) | val.v};
    }

    [[nodiscard]] constexpr DipWord masked(DipWord mask) const noexcept {
        return DipWord{v & mask.v};
    }

    [[nodiscard]] constexpr Bytes le_bytes() const noexcept {
        constexpr std::size_t kByteBits = 8;
        Bytes out{};
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<std::uint8_t>(v >> (kByteBits * i));
        return out;
    }

    [[nodiscard]] static constexpr DipWord from_le_bytes(
        std::span<const std::uint8_t> raw
    ) noexcept {
        constexpr std::size_t kBytes = std::tuple_size_v<Bytes>;
        DipWord out{};
        for (std::size_t i = 0; i < raw.size() && i < kBytes; ++i)
            out = out.with_byte(i, raw[i]);
        return out;
    }

    friend constexpr bool operator==(DipWord, DipWord) = default;
};
}

namespace snes {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct HeaderLen {
    std::uint32_t v = 0;
    using affine_count = HeaderLen;

    [[nodiscard]] static constexpr HeaderLen of(std::span<const std::uint8_t> bytes) noexcept {
        return HeaderLen{static_cast<std::uint32_t>(bytes.size())};
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> first_of(
        std::span<const std::uint8_t> bytes
    ) const noexcept {
        return bytes.first(v < bytes.size() ? v : bytes.size());
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> past(
        std::span<const std::uint8_t> bytes
    ) const noexcept {
        return bytes.subspan(v < bytes.size() ? v : bytes.size());
    }

    friend constexpr bool operator==(HeaderLen, HeaderLen) = default;
    friend constexpr auto operator<=>(HeaderLen, HeaderLen) = default;
};

struct ResetVector {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr HeaderLen bank_offset() const noexcept {
        constexpr std::uint32_t kBankOffset = 0x7fff;
        return HeaderLen{v & kBankOffset};
    }

    friend constexpr bool operator==(ResetVector, ResetVector) = default;
    friend constexpr auto operator<=>(ResetVector, ResetVector) = default;
};

struct HeaderAddr {
    std::uint32_t v = 0;
    using affine_count = HeaderLen;

    [[nodiscard]] constexpr std::span<const std::uint8_t> slice(
        std::span<const std::uint8_t> window,
        HeaderAddr base,
        HeaderLen len
    ) const noexcept {
        if (v < base.v || v - base.v >= window.size())
            return {};
        return len.first_of(window.subspan(v - base.v));
    }

    [[nodiscard]] constexpr HeaderAddr bank_start() const noexcept {
        constexpr std::uint32_t kBankOffset = 0x7fff;
        return HeaderAddr{v & ~kBankOffset};
    }

    [[nodiscard]] constexpr HeaderAddr reset_op(ResetVector vector) const noexcept {
        return bank_start() + vector.bank_offset();
    }

    constexpr void store_le32(std::span<std::uint8_t, sizeof(std::uint32_t)> dst) const noexcept {
        infra::store_le<std::uint32_t>(dst, v);
    }

    friend constexpr bool operator==(HeaderAddr, HeaderAddr) = default;
    friend constexpr auto operator<=>(HeaderAddr, HeaderAddr) = default;
};

struct HeaderScore {
    std::int32_t v = 0;
    using affine_count = HeaderScore;

    [[nodiscard]] constexpr HeaderScore clamped() const noexcept {
        return v < 0 ? HeaderScore{} : *this;
    }

    [[nodiscard]] constexpr bool scored() const noexcept { return v != 0; }

    friend constexpr bool operator==(HeaderScore, HeaderScore) = default;
    friend constexpr auto operator<=>(HeaderScore, HeaderScore) = default;
};

}

}
