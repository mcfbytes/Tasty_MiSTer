// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ratio>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/sector_types.h"

namespace mister::svc {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

using AtxUnits = std::chrono::duration<std::int64_t, std::ratio<8, 1'000'000>>;

inline constexpr AtxUnits kFullRotation{26042};

struct AtxLength {
    std::uint32_t v = 0;
    using affine_count = AtxLength;

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    [[nodiscard]] static constexpr AtxLength of(infra::ByteCount bytes) noexcept {
        return AtxLength{static_cast<std::uint32_t>(bytes.v)};
    }

    friend constexpr bool operator==(AtxLength, AtxLength) = default;
    friend constexpr auto operator<=>(AtxLength, AtxLength) = default;
};

struct AtxOffset {
    std::uint32_t v = 0;
    using affine_count = AtxLength;
    using affine_index = AtxOffset;

    [[nodiscard]] constexpr proto::FileOffset file_offset() const noexcept {
        return proto::FileOffset{v};
    }

    friend constexpr bool operator==(AtxOffset, AtxOffset) = default;
    friend constexpr auto operator<=>(AtxOffset, AtxOffset) = default;
};

struct AtxSectorNumber {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr AtxSectorNumber of_image_sector(
        SectorIndex index,
        SectorCount per_track
    ) noexcept {
        const auto from_zero = static_cast<std::uint16_t>(index.v);
        return AtxSectorNumber{static_cast<std::uint8_t>(from_zero % per_track.v + 1)};
    }

    friend constexpr bool operator==(AtxSectorNumber, AtxSectorNumber) = default;
    friend constexpr auto operator<=>(AtxSectorNumber, AtxSectorNumber) = default;
};

struct AtxEntryCount {
    std::uint16_t v = 0;
    using affine_count = AtxEntryCount;

    [[nodiscard]] constexpr AtxLength span(AtxLength per_record) const noexcept {
        return AtxLength{static_cast<std::uint32_t>(v) * per_record.v};
    }

    friend constexpr bool operator==(AtxEntryCount, AtxEntryCount) = default;
    friend constexpr auto operator<=>(AtxEntryCount, AtxEntryCount) = default;
};

struct AtxEntry {
    std::uint16_t v = 0;
    using affine_count = AtxEntryCount;
    using affine_index = AtxEntry;
    friend constexpr bool operator==(AtxEntry, AtxEntry) = default;
    friend constexpr auto operator<=>(AtxEntry, AtxEntry) = default;
};

struct AtxTrackCount {
    std::uint16_t v = 0;
    using affine_count = AtxTrackCount;

    [[nodiscard]] constexpr std::chrono::microseconds times(
        std::chrono::microseconds per_step
    ) const noexcept {
        return per_step * v;
    }

    friend constexpr bool operator==(AtxTrackCount, AtxTrackCount) = default;
    friend constexpr auto operator<=>(AtxTrackCount, AtxTrackCount) = default;
};

struct AtxTrack {
    std::uint16_t v = 0;
    using affine_count = AtxTrackCount;
    using affine_index = AtxTrack;

    [[nodiscard]] static constexpr AtxTrack of_image_sector(
        SectorIndex index,
        SectorCount per_track
    ) noexcept {
        const auto from_zero = static_cast<std::uint16_t>(index.v);
        return AtxTrack{static_cast<std::uint8_t>(from_zero / per_track.v)};
    }

    friend constexpr bool operator==(AtxTrack, AtxTrack) = default;
    friend constexpr auto operator<=>(AtxTrack, AtxTrack) = default;
};

struct FdcStatus {
    std::uint8_t v = 0;

    static const FdcStatus kBusy;
    static const FdcStatus kDrq;
    static const FdcStatus kLost;
    static const FdcStatus kCrc;
    static const FdcStatus kMissing;
    static const FdcStatus kRecordType;
    static const FdcStatus kExtended;
    static const FdcStatus kProtect;
    static const FdcStatus kReserved;

    [[nodiscard]] constexpr bool clear() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool has(FdcStatus bits) const noexcept { return (v & bits.v) != 0; }

    [[nodiscard]] constexpr FdcStatus with(FdcStatus bits) const noexcept {
        return FdcStatus{static_cast<std::uint8_t>(v | bits.v)};
    }

    [[nodiscard]] constexpr FdcStatus without(FdcStatus bits) const noexcept {
        return FdcStatus{static_cast<std::uint8_t>(v & ~bits.v)};
    }

    [[nodiscard]] constexpr std::byte file_byte() const noexcept { return std::byte{v}; }

    [[nodiscard]] constexpr SectorStatus wire() const noexcept {
        return SectorStatus{static_cast<std::uint8_t>(~v)};
    }
};

inline constexpr FdcStatus FdcStatus::kBusy{0x01};
inline constexpr FdcStatus FdcStatus::kDrq{0x02};
inline constexpr FdcStatus FdcStatus::kLost{0x04};
inline constexpr FdcStatus FdcStatus::kCrc{0x08};
inline constexpr FdcStatus FdcStatus::kMissing{0x10};
inline constexpr FdcStatus FdcStatus::kRecordType{0x20};
inline constexpr FdcStatus FdcStatus::kExtended{0x40};
inline constexpr FdcStatus FdcStatus::kProtect{0x40};
inline constexpr FdcStatus FdcStatus::kReserved{0x80};

struct WeakOffset {
    std::int16_t v = -1;

    [[nodiscard]] constexpr bool none() const noexcept { return v == -1; }

    [[nodiscard]] constexpr std::optional<infra::ByteOffset> random_from() const noexcept {
        if (v < 0)
            return std::nullopt;
        return infra::ByteOffset{static_cast<std::size_t>(v)};
    }

    friend constexpr bool operator==(WeakOffset, WeakOffset) = default;
};

struct AtxChunkData {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr WeakOffset weak() const noexcept {
        return WeakOffset{static_cast<std::int16_t>(v)};
    }

    [[nodiscard]] constexpr SectorSize long_size() const noexcept {
        constexpr std::uint16_t kLongBase = 128;
        return SectorSize{static_cast<std::uint16_t>(kLongBase << v)};
    }

    friend constexpr bool operator==(AtxChunkData, AtxChunkData) = default;
};

struct AtxNoise {
    TASTY_SEAT_EXEMPT(component);
    std::uint32_t v = 0;
    static const AtxNoise kSeed;

    [[nodiscard]] std::byte next() noexcept {
        constexpr unsigned kShiftA = 13;
        constexpr unsigned kShiftB = 17;
        constexpr unsigned kShiftC = 5;
        v ^= v << kShiftA;
        v ^= v >> kShiftB;
        v ^= v << kShiftC;
        return static_cast<std::byte>(v);
    }

    friend constexpr bool operator==(AtxNoise, AtxNoise) = default;
};

inline constexpr AtxNoise AtxNoise::kSeed{0x2545F491u};

}
