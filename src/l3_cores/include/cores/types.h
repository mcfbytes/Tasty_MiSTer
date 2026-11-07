// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <tuple>
#include <optional>
#include <span>

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
    friend constexpr bool operator==(AcsiTarget, AcsiTarget) = default;
    friend constexpr auto operator<=>(AcsiTarget, AcsiTarget) = default;
};

struct ConfigSlot {
    std::uint8_t v = 0;
    friend constexpr bool operator==(ConfigSlot, ConfigSlot) = default;
};

struct WordId {
    std::uint8_t v = 0;
    friend constexpr bool operator==(WordId, WordId) = default;
};

struct ChoiceCount {
    std::uint8_t v = 0;
    using affine_count = ChoiceCount;
    inline static constexpr std::uint8_t kAll = 0;

    [[nodiscard]] static constexpr ChoiceCount all() noexcept { return ChoiceCount{kAll}; }

    [[nodiscard]] constexpr bool is_all() const noexcept { return v == kAll; }

    friend constexpr bool operator==(ChoiceCount, ChoiceCount) = default;
    friend constexpr auto operator<=>(ChoiceCount, ChoiceCount) = default;
};

struct ChoiceSlot {
    std::uint8_t v = 0;
    using affine_count = ChoiceCount;

    template <class Row>
    [[nodiscard]] constexpr bool within(std::span<const Row> table) const noexcept {
        return v < table.size();
    }

    friend constexpr bool operator==(ChoiceSlot, ChoiceSlot) = default;
    friend constexpr auto operator<=>(ChoiceSlot, ChoiceSlot) = default;
};

struct JoyBit {
    std::uint8_t v = 0;
    friend constexpr bool operator==(JoyBit, JoyBit) = default;
};

struct BaudRate {
    std::uint32_t v = 0;
    friend constexpr bool operator==(BaudRate, BaudRate) = default;
    friend constexpr auto operator<=>(BaudRate, BaudRate) = default;
};

struct SaveRow {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr SaveRow next() const noexcept {
        return SaveRow{static_cast<std::uint8_t>(v + 1u)};
    }

    friend constexpr bool operator==(SaveRow, SaveRow) = default;
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

    [[nodiscard]] constexpr std::size_t bytes(std::size_t frame_bytes) const noexcept {
        return v < 0 ? 0 : static_cast<std::size_t>(v) * frame_bytes;
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

    [[nodiscard]] constexpr std::optional<proto::Lba> readable() const noexcept {
        if (v < 0)
            return std::nullopt;
        return proto::Lba{static_cast<std::uint32_t>(v)};
    }

    [[nodiscard]] constexpr svc::Msf msf() const noexcept;

    [[nodiscard]] constexpr svc::Msf stock_msf() const noexcept;

    [[nodiscard]] constexpr svc::Msf wrapped_msf() const noexcept;
    friend constexpr bool operator==(HeadLba, HeadLba) = default;
    friend constexpr auto operator<=>(HeadLba, HeadLba) = default;
};

enum class DiscType : std::uint8_t { CdRom = 0x00, CdRomXa = 0x20 };

namespace mra {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct RomLength;

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

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

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

    [[nodiscard]] constexpr std::optional<RomLength> repeats_within(
        RomOffset cap,
        std::span<const std::uint8_t> pattern,
        std::int32_t repeat
    ) const noexcept {
        const std::uint64_t len = static_cast<std::uint64_t>(repeat) * pattern.size();
        if (std::uint64_t{v} + len > cap.v)
            return std::nullopt;
        return RomLength{static_cast<std::uint32_t>(len)};
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
