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

#include "infra/seat.h"
#include "infra/affine.h"
#include "proto/types.h"

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
    friend constexpr bool operator==(SectorCount, SectorCount) = default;
    friend constexpr auto operator<=>(SectorCount, SectorCount) = default;
};

struct HeadLba {
    std::int32_t v = 0;
    using affine_count = SectorCount;

    [[nodiscard]] constexpr std::optional<proto::Lba> readable() const noexcept {
        if (v < 0)
            return std::nullopt;
        return proto::Lba{static_cast<std::uint32_t>(v)};
    }

    friend constexpr bool operator==(HeadLba, HeadLba) = default;
    friend constexpr auto operator<=>(HeadLba, HeadLba) = default;
};

enum class DiscType : std::uint8_t { CdRom = 0x00, CdRomXa = 0x20 };

namespace mra {

struct DipWord {
    TASTY_SEAT_EXEMPT(component);
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

}
