// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/reg_field.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/endian.h"
#include "proto/disk_register_count.h"
#include "proto/types.h"
#include "svc/disk_geometry.h"

namespace mister::cores::ide {

struct IdeStatus {
    std::uint8_t v = 0;
    static const IdeStatus kBusy;
    static const IdeStatus kReady;
    static const IdeStatus kFastRead;
    static const IdeStatus kSeekComplete;
    static const IdeStatus kDataRequest;
    static const IdeStatus kIrq;
    static const IdeStatus kLastRead;
    static const IdeStatus kError;

    [[nodiscard]] constexpr bool intersects(IdeStatus bits) const noexcept {
        return (v & bits.v) != 0;
    }

    [[nodiscard]] constexpr IdeStatus without(IdeStatus bits) const noexcept {
        return IdeStatus{static_cast<std::uint8_t>(v & ~bits.v)};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }

    friend constexpr IdeStatus operator|(IdeStatus a, IdeStatus b) noexcept {
        return IdeStatus{static_cast<std::uint8_t>(a.v | b.v)};
    }

    friend constexpr bool operator==(IdeStatus, IdeStatus) = default;
};

inline constexpr IdeStatus IdeStatus::kBusy{0x80};
inline constexpr IdeStatus IdeStatus::kReady{0x40};
inline constexpr IdeStatus IdeStatus::kFastRead{0x20};
inline constexpr IdeStatus IdeStatus::kSeekComplete{0x10};
inline constexpr IdeStatus IdeStatus::kDataRequest{0x08};
inline constexpr IdeStatus IdeStatus::kIrq{0x04};
inline constexpr IdeStatus IdeStatus::kLastRead{0x02};
inline constexpr IdeStatus IdeStatus::kError{0x01};

struct IdeRegister {
    std::uint8_t v = 0;
    static const IdeRegister kTaskFile;
    static const IdeRegister kPresence;
    static const IdeRegister kData;

    [[nodiscard]] constexpr proto::DiskRegisterCount offset() const noexcept {
        return proto::DiskRegisterCount{v};
    }

    friend constexpr bool operator==(IdeRegister, IdeRegister) = default;
};

inline constexpr IdeRegister IdeRegister::kTaskFile{0};
inline constexpr IdeRegister IdeRegister::kPresence{6};
inline constexpr IdeRegister IdeRegister::kData{255};

enum class IdeDriveSelect : std::uint8_t { Master, Slave };

enum class RemountTarget : std::uint8_t { FloppyA, FloppyB, ThisDrive };

[[nodiscard]] constexpr hal::LinkWord presence_word(IdeDriveSelect drive, bool present) noexcept {
    constexpr unsigned kSlaveShift = 4;
    constexpr std::uint16_t kWriteEnable = 0x8;
    constexpr std::uint16_t kPresent = 0x1;
    const std::uint16_t nibble = present ? (kWriteEnable | kPresent) : kWriteEnable;
    const unsigned shift = drive == IdeDriveSelect::Slave ? kSlaveShift : 0;
    return hal::LinkWord{static_cast<std::uint16_t>(nibble << shift)};
}

[[nodiscard]] constexpr hal::LinkWord no_wait_word() noexcept {
    constexpr std::uint16_t kWaitWriteEnable = 0x200;
    return hal::LinkWord{kWaitWriteEnable};
}

struct IdeSectorCount {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr proto::BlockCount blocks() const noexcept {
        return proto::BlockCount{v};
    }

    [[nodiscard]] constexpr proto::BlockCount burst(proto::BlockCount most) const noexcept {
        return (v == 0 || blocks() > most) ? most : blocks();
    }

    [[nodiscard]] constexpr IdeSectorCount less(proto::BlockCount moved) const noexcept {
        return IdeSectorCount{
            static_cast<std::uint8_t>(v - std::to_integer<std::uint8_t>(moved.low_byte()))
        };
    }

    [[nodiscard]] constexpr svc::DiskGeometry::Sectors sectors() const noexcept {
        return svc::DiskGeometry::Sectors{v};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(IdeSectorCount, IdeSectorCount) = default;
};

struct InterruptReason {
    std::uint8_t v = 0;
    static const InterruptReason kDataOut;
    static const InterruptReason kCommand;
    static const InterruptReason kDataIn;
    static const InterruptReason kStatus;

    [[nodiscard]] constexpr IdeSectorCount count() const noexcept { return IdeSectorCount{v}; }

    friend constexpr bool operator==(InterruptReason, InterruptReason) = default;
};

inline constexpr InterruptReason InterruptReason::kDataOut{0};
inline constexpr InterruptReason InterruptReason::kCommand{1};
inline constexpr InterruptReason InterruptReason::kDataIn{2};
inline constexpr InterruptReason InterruptReason::kStatus{3};

struct IdeAddress {
    std::uint32_t v = 0;
    static constexpr hal::RegField<std::uint32_t> kSectorField{0, 0xFF};
    static constexpr hal::RegField<std::uint32_t> kCylinderField{8, 0xFFFF};
    static constexpr hal::RegField<std::uint32_t> kHeadField{24, 0xF};

    [[nodiscard]] static constexpr IdeAddress of(
        std::byte sector,
        std::span<const std::byte, 2> cylinder,
        std::byte head
    ) noexcept {
        std::uint32_t packed = kSectorField.set(0, {std::to_integer<std::uint32_t>(sector)});
        packed = kCylinderField.set(packed, {infra::load_le<std::uint16_t>(cylinder)});
        return IdeAddress{kHeadField.set(packed, {std::to_integer<std::uint32_t>(head)})};
    }

    [[nodiscard]] static constexpr IdeAddress of(
        std::byte sector,
        hal::LinkWord cylinder,
        std::byte head
    ) noexcept {
        std::array<std::byte, sizeof(hal::LinkWord)> wire{};
        cylinder.store_le(std::span{wire});
        return of(sector, std::span{wire}, head);
    }

    [[nodiscard]] constexpr std::byte sector_byte() const noexcept {
        return static_cast<std::byte>(kSectorField.get(v));
    }

    [[nodiscard]] constexpr hal::LinkWord cylinder_word() const noexcept {
        return hal::LinkWord{static_cast<std::uint16_t>(kCylinderField.get(v))};
    }

    [[nodiscard]] constexpr std::byte head_byte() const noexcept {
        return static_cast<std::byte>(kHeadField.get(v));
    }

    [[nodiscard]] constexpr RemountTarget remount_target() const noexcept {
        constexpr std::uint32_t kFloppyAHead = 1;
        constexpr std::uint32_t kFloppyBHead = 2;
        const std::uint32_t head = kHeadField.get(v);
        if (head == kFloppyAHead)
            return RemountTarget::FloppyA;
        if (head == kFloppyBHead)
            return RemountTarget::FloppyB;
        return RemountTarget::ThisDrive;
    }

    [[nodiscard]] constexpr proto::Lba lba() const noexcept { return proto::Lba{v}; }

    [[nodiscard]] static constexpr IdeAddress of_lba(proto::Lba block) noexcept {
        constexpr std::uint32_t kAddressMask = 0x0FFF'FFFF;
        return IdeAddress{block.v & kAddressMask};
    }

    [[nodiscard]] constexpr proto::Lba chs_lba(const svc::DiskGeometry& shape) const noexcept {
        const svc::DiskGeometry::Cylinders cylinder{kCylinderField.get(v)};
        const svc::DiskGeometry::Heads head{kHeadField.get(v)};
        const svc::DiskGeometry::Sectors sector{kSectorField.get(v)};
        return proto::Lba{}
        + cylinder.blocks(shape.cylinder_blocks())
            + head.cylinder_blocks(shape.sectors)
            + sector.track_blocks()
            - proto::BlockCount{1};
    }

    [[nodiscard]] static constexpr IdeAddress of_chs(
        proto::Lba block,
        const svc::DiskGeometry& shape
    ) noexcept {
        const proto::BlockCount offset = block - proto::Lba{};
        const svc::DiskGeometry::Cylinders cylinder{offset / shape.cylinder_blocks()};
        const proto::BlockCount in_cylinder = offset - cylinder.blocks(shape.cylinder_blocks());
        const svc::DiskGeometry::Heads head{in_cylinder / shape.sectors.track_blocks()};
        const proto::BlockCount in_track = in_cylinder - head.cylinder_blocks(shape.sectors);
        return of(
            (in_track + proto::BlockCount{1}).low_byte(),
            cylinder.word(),
            std::byte{head.word().low_byte()}
        );
    }

    [[nodiscard]] constexpr infra::ByteCount byte_count() const noexcept {
        return infra::ByteCount{kCylinderField.get(v)};
    }

    [[nodiscard]] constexpr IdeAddress with_byte_count(infra::ByteCount bytes) const noexcept {
        return IdeAddress{kCylinderField.set(v, {static_cast<std::uint32_t>(bytes.v)})};
    }

    [[nodiscard]] constexpr IdeAddress recalibrated() const noexcept {
        return IdeAddress{kCylinderField.set(v, {0})};
    }

    [[nodiscard]] static constexpr IdeAddress reset_signature(bool present) noexcept {
        return of(std::byte{1}, present ? hal::LinkWord{} : hal::LinkWord::all_ones(), std::byte{});
    }

    [[nodiscard]] static constexpr IdeAddress packet_signature() noexcept {
        constexpr std::uint16_t kAtapiSignature = 0xEB14;
        return of(std::byte{1}, hal::LinkWord{kAtapiSignature}, std::byte{});
    }

    [[nodiscard]] constexpr svc::DiskGeometry::Heads head_count() const noexcept {
        return svc::DiskGeometry::Heads{kHeadField.get(v) + 1u};
    }

    friend constexpr bool operator==(IdeAddress, IdeAddress) = default;
};

}
