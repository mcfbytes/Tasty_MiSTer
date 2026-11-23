// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "proto/types.h"

namespace mister::svc {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct SectorCount {
    std::uint32_t v = 0;
    using affine_count = SectorCount;

    [[nodiscard]] constexpr std::array<std::byte, 4> le_bytes() const noexcept {
        std::array<std::byte, 4> out{};
        infra::store_le(std::span<std::byte, 4>{out}, v);
        return out;
    }

    [[nodiscard]] constexpr std::array<std::byte, 4> be_bytes() const noexcept {
        std::array<std::byte, 4> out{};
        infra::store_be(std::span<std::byte, 4>{out}, v);
        return out;
    }

    friend constexpr bool operator==(SectorCount, SectorCount) = default;
    friend constexpr auto operator<=>(SectorCount, SectorCount) = default;
};

struct SectorIndex {
    std::uint32_t v = 0;
    using affine_count = SectorCount;
    using affine_index = SectorIndex;

    [[nodiscard]] constexpr std::array<std::byte, 4> le_bytes() const noexcept {
        std::array<std::byte, 4> out{};
        infra::store_le(std::span<std::byte, 4>{out}, v);
        return out;
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(SectorIndex, SectorIndex) = default;
    friend constexpr auto operator<=>(SectorIndex, SectorIndex) = default;
};

struct SectorSize {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    constexpr void store_be16(std::span<std::byte, 2> dst) const noexcept {
        infra::store_be<std::uint16_t>(dst, v);
    }

    [[nodiscard]] constexpr proto::FileSize span_of(SectorCount n) const noexcept {
        return proto::FileSize{std::uint64_t{v} * n.v};
    }

    [[nodiscard]] constexpr SectorCount whole_in(proto::FileSize size) const noexcept {
        return SectorCount{static_cast<std::uint32_t>(size.v / v)};
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> blank(std::span<Byte> buffer) const noexcept {
        const std::span<Byte> front = of(buffer);
        std::ranges::fill(front, Byte{});
        return front;
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> of(std::span<Byte> buffer) const noexcept {
        return buffer.first(v);
    }

    template <class Byte>
    [[nodiscard]] constexpr std::span<Byte> sector_in(
        std::span<Byte> run,
        SectorIndex i
    ) const noexcept {
        return of(run.subspan(static_cast<std::size_t>(i.v) * v));
    }

    [[nodiscard]] constexpr SectorCount covering(proto::FileSize size) const noexcept {
        const std::uint64_t bytes = static_cast<std::uint64_t>(size.v);
        return SectorCount{static_cast<std::uint32_t>(infra::affine::div_ceil(bytes, v))};
    }

    [[nodiscard]] constexpr std::array<std::byte, 2> le_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_le(std::span<std::byte, 2>{out}, v);
        return out;
    }

    [[nodiscard]] constexpr std::array<std::byte, 2> be_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_be(std::span<std::byte, 2>{out}, v);
        return out;
    }

    template <class Byte>
    [[nodiscard]] constexpr bool fits(std::span<Byte> buffer) const noexcept {
        return buffer.size() >= v;
    }

    [[nodiscard]] constexpr Error refusal(Errc code, infra::ErrorSite at) const noexcept {
        return Error{code, at, v};
    }

    friend constexpr bool operator==(SectorSize, SectorSize) = default;
    friend constexpr auto operator<=>(SectorSize, SectorSize) = default;
};

struct SectorStatus {
    static constexpr std::uint8_t kFaultFree = 0xFF;
    std::uint8_t v = kFaultFree;

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(SectorStatus, SectorStatus) = default;
};

inline constexpr SectorStatus kSectorClear{};

}
