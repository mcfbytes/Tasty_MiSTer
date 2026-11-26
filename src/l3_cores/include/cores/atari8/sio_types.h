// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/types.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/endian.h"
#include "proto/types.h"
#include "svc/sector_types.h"

namespace mister::cores::atari8 {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

struct SioDriveCount {
    std::uint8_t v = 0;
    using affine_count = SioDriveCount;

    static const SioDriveCount kAll;
    friend constexpr bool operator==(SioDriveCount, SioDriveCount) = default;
    friend constexpr auto operator<=>(SioDriveCount, SioDriveCount) = default;
};

inline constexpr SioDriveCount SioDriveCount::kAll{16};

struct SioUnit {
    std::uint8_t v = 0;
    using affine_count = SioDriveCount;
    using affine_index = SioUnit;
    static const SioUnit kD1;
    static const SioUnit kD2;
    static const SioUnit kD3;
    static const SioUnit kD4;
    static const SioUnit kHardDisk;

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    [[nodiscard]] static constexpr std::optional<SioUnit> of_dunit(std::byte dunit) noexcept {
        const std::uint8_t n = std::to_integer<std::uint8_t>(dunit);
        const SioUnit unit{static_cast<std::uint8_t>(n - 1)};
        if (!infra::affine::in_bounds(SioDriveCount::kAll, unit))
            return std::nullopt;
        return unit;
    }

    friend constexpr bool operator==(SioUnit, SioUnit) = default;
    friend constexpr auto operator<=>(SioUnit, SioUnit) = default;
};

inline constexpr SioUnit SioUnit::kD1{0};
inline constexpr SioUnit SioUnit::kD2{1};
inline constexpr SioUnit SioUnit::kD3{2};
inline constexpr SioUnit SioUnit::kD4{3};
inline constexpr SioUnit SioUnit::kHardDisk{15};

enum class SioSectorOrigin : std::uint8_t { FromZero, FromOne };

struct SioDevice {
    std::uint8_t v = 0;

    static constexpr hal::RegField<std::uint8_t> kPbiBit{6, 0x1};
    static constexpr hal::RegField<std::uint8_t> kLongBit{7, 0x1};
    static constexpr hal::RegField<std::uint8_t> kFamilyField{0, 0x3F};
    static constexpr hal::RegField<std::uint8_t> kUnitField{0, 0x0F};

    static const SioDevice kHardDiskFamily;
    static const SioDevice kFirstSerialDisk;
    static const SioDevice kLastSerialDisk;

    [[nodiscard]] constexpr bool pbi() const noexcept { return kPbiBit.test(v); }

    [[nodiscard]] constexpr bool long_sector() const noexcept { return kLongBit.test(v); }

    [[nodiscard]] constexpr bool hard_disk() const noexcept {
        return SioDevice{kFamilyField.get(v)} == kHardDiskFamily;
    }

    [[nodiscard]] constexpr bool serial_disk() const noexcept {
        return *this >= kFirstSerialDisk && *this <= kLastSerialDisk;
    }

    [[nodiscard]] constexpr SioUnit unit() const noexcept {
        if (hard_disk())
            return SioUnit::kHardDisk;
        return SioUnit{static_cast<std::uint8_t>(kUnitField.get(v) - 1)};
    }

    [[nodiscard]] static constexpr SioDevice of_pbi_request(
        SioDevice ddevic,
        std::byte dunit
    ) noexcept {
        const auto sum = static_cast<std::uint8_t>(
            ddevic.v + std::to_integer<std::uint8_t>(dunit) - 1
        );
        return SioDevice{static_cast<std::uint8_t>(sum | kPbiBit.word_mask())};
    }

    [[nodiscard]] constexpr SioSectorOrigin sector_origin() const noexcept {
        return hard_disk() ? SioSectorOrigin::FromZero : SioSectorOrigin::FromOne;
    }

    friend constexpr bool operator==(SioDevice, SioDevice) = default;
    friend constexpr auto operator<=>(SioDevice, SioDevice) = default;
};

inline constexpr SioDevice SioDevice::kHardDiskFamily{0x20};
inline constexpr SioDevice SioDevice::kFirstSerialDisk{0x31};
inline constexpr SioDevice SioDevice::kLastSerialDisk{0x34};

struct SioSector {
    std::uint32_t v = 0;
    using affine_count = svc::SectorCount;

    static constexpr hal::RegField<std::uint32_t> kAux2Field{8, 0xFF};

    static const SioSector kLastBoot;

    [[nodiscard]] static constexpr SioSector load_le(std::span<const std::byte, 2> b) noexcept {
        return SioSector{infra::load_le<std::uint16_t>(b)};
    }

    [[nodiscard]] static constexpr SioSector load_le(std::span<const std::byte, 4> b) noexcept {
        return SioSector{infra::load_le<std::uint32_t>(b)};
    }

    [[nodiscard]] constexpr std::array<std::byte, 2> le_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_le(std::span<std::byte, 2>{out}, static_cast<std::uint16_t>(v));
        return out;
    }

    [[nodiscard]] constexpr bool standard_rate() const noexcept { return kAux2Field.test(v); }

    [[nodiscard]] constexpr bool past_boot() const noexcept { return *this > kLastBoot; }

    [[nodiscard]] constexpr svc::SectorCount index_from(SioSectorOrigin origin) const noexcept {
        return svc::SectorCount{v - std::to_underlying(origin)};
    }

    [[nodiscard]] constexpr std::optional<svc::SectorIndex> index_in(
        SioSectorOrigin origin,
        svc::SectorCount count
    ) const noexcept {
        const std::uint32_t from = std::to_underlying(origin);
        if (v < from)
            return std::nullopt;
        const svc::SectorIndex i{v - from};
        if (!infra::affine::in_bounds(count, i))
            return std::nullopt;
        return i;
    }

    [[nodiscard]] static constexpr SioSector at(
        svc::SectorCount index,
        SioSectorOrigin origin
    ) noexcept {
        return SioSector{static_cast<std::uint32_t>(index.v) + std::to_underlying(origin)};
    }

    friend constexpr bool operator==(SioSector, SioSector) = default;
    friend constexpr auto operator<=>(SioSector, SioSector) = default;
};

inline constexpr SioSector SioSector::kLastBoot{3};

struct SioDivisor {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr SioDivisor switched_from(hal::LinkWord getdiv) noexcept {
        return SioDivisor{static_cast<std::uint8_t>(getdiv.low_byte() - 1)};
    }

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(SioDivisor, SioDivisor) = default;
};

struct SioRate {
    std::uint8_t v = 0;
    static constexpr std::uint8_t kBias = 6;

    [[nodiscard]] constexpr SioDivisor divisor() const noexcept {
        return SioDivisor{static_cast<std::uint8_t>(((v + kBias) << 1) + 1)};
    }

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(SioRate, SioRate) = default;
};

struct SioSpeedChoice {
    std::uint8_t v = 0;
    static constexpr std::array<SioRate, 8> kRates = {
        SioRate{0x28},
        SioRate{0x06},
        SioRate{0x05},
        SioRate{0x04},
        SioRate{0x03},
        SioRate{0x02},
        SioRate{0x01},
        SioRate{0x00}
    };
    static constexpr SioRate kStandard = kRates[0];

    [[nodiscard]] constexpr SioRate rate() const noexcept { return kRates[v % kRates.size()]; }
};

struct SioWordCount {
    std::uint16_t v = 0;
    using affine_count = SioWordCount;

    [[nodiscard]] constexpr proto::ReplyCount as_replies() const noexcept {
        constexpr std::uint16_t kMost = 0xFF;
        return proto::ReplyCount{static_cast<std::uint8_t>(v < kMost ? v : kMost)};
    }

    [[nodiscard]] static constexpr SioWordCount of(proto::ReplyCount replies) noexcept {
        return SioWordCount{static_cast<std::uint16_t>(replies.v)};
    }

    [[nodiscard]] static constexpr SioWordCount at_most(infra::ByteCount bytes) noexcept {
        constexpr std::size_t kMost = 0xFFFF;
        return SioWordCount{static_cast<std::uint16_t>(bytes.v < kMost ? bytes.v : kMost)};
    }

    friend constexpr bool operator==(SioWordCount, SioWordCount) = default;
    friend constexpr auto operator<=>(SioWordCount, SioWordCount) = default;
};

struct SioFramePlace {
    std::uint16_t v = 0;
    using affine_count = SioWordCount;
    using affine_index = SioFramePlace;
    friend constexpr bool operator==(SioFramePlace, SioFramePlace) = default;
    friend constexpr auto operator<=>(SioFramePlace, SioFramePlace) = default;
};

struct SioPartitionId {
    std::uint16_t v = 0;

    [[nodiscard]] constexpr std::array<std::byte, 2> wire_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_le(std::span<std::byte, 2>{out}, v);
        return out;
    }

    friend constexpr bool operator==(SioPartitionId, SioPartitionId) = default;
};

struct HostAddress {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr HostAddress load_le(std::span<const std::byte, 2> b) noexcept {
        return HostAddress{infra::load_le<std::uint16_t>(b)};
    }

    [[nodiscard]] constexpr std::array<std::byte, 2> wire_bytes() const noexcept {
        std::array<std::byte, 2> out{};
        infra::store_le(std::span<std::byte, 2>{out}, v);
        return out;
    }

    [[nodiscard]] constexpr HostAddress after(infra::ByteCount n) const noexcept {
        return HostAddress{static_cast<std::uint16_t>(v + n.v)};
    }

    [[nodiscard]] constexpr HostAddress at(infra::ByteOffset field) const noexcept {
        return HostAddress{static_cast<std::uint16_t>(v + field.v)};
    }

    [[nodiscard]] constexpr infra::ByteCount from_zero() const noexcept {
        return infra::ByteCount{v};
    }

    [[nodiscard]] constexpr std::optional<infra::ByteCount> through(
        HostAddress last
    ) const noexcept {
        if (last.v < v)
            return std::nullopt;
        return infra::ByteCount{std::size_t{last.v} + 1 - v};
    }

    friend constexpr bool operator==(HostAddress, HostAddress) = default;
};

struct SioRxWord {
    hal::LinkWord v{};
    static constexpr hal::RegField<std::uint8_t> kPlaceField{0, 0x7F};

    [[nodiscard]] constexpr std::byte byte() const noexcept { return v.low_lane(); }

    [[nodiscard]] constexpr std::optional<SioFramePlace> place() const noexcept {
        const std::uint8_t tag = kPlaceField.get(v.high_byte());
        if (tag == 0)
            return std::nullopt;
        return SioFramePlace{static_cast<std::uint16_t>(tag - 1)};
    }
};

struct SioRxStatus {
    hal::LinkWord v{};
    static constexpr hal::LinkWord kFull{0x0200};
    static constexpr hal::LinkWord kEmpty{0x0100};
    static constexpr SioWordCount kDepth{256};

    [[nodiscard]] constexpr SioWordCount held() const noexcept {
        if (v.intersects(kFull))
            return kDepth;
        if (v.intersects(kEmpty))
            return SioWordCount{};
        return SioWordCount{v.low_byte()};
    }
};

struct SioError {
    hal::LinkWord v{};

    [[nodiscard]] constexpr bool any() const noexcept { return v != hal::LinkWord{}; }

    [[nodiscard]] constexpr SioError with(SioError more) const noexcept {
        return SioError{v.with(more.v)};
    }
};

}
