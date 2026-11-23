// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/atari8/sio_partition.h"
#include "cores/atari8/sio_types.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"
#include "svc/sector_types.h"

namespace mister::cores::atari8 {

struct SioStatusFacts {
    std::byte fdc;
    svc::SectorCount count;
    svc::SectorSize size;
    bool read_only;
    bool disk;
};

struct SioStatusFrame {
    std::byte drive;
    std::byte fdc;
    std::byte timeout;
    std::byte last;

    static constexpr std::byte kMotor{0x10};
    static constexpr std::byte kLocked{0x08};
    static constexpr std::byte kMedium{0x80};
    static constexpr std::byte kWide{0x20};
    static constexpr std::byte kDisk{0x40};
    static constexpr std::byte kFloppyId{0xE0};
    static constexpr std::byte kDiskId{0x10};
    static constexpr std::byte kBlank{0x00};

    [[nodiscard]] static SioStatusFrame of(
        SioStatusFacts facts,
        std::span<const std::byte> host
    ) noexcept;
    [[nodiscard]] std::array<std::byte, 4> bytes() const noexcept;
};

struct SioPercom {
    std::byte tracks;
    std::byte step;
    svc::SectorCount per_track;
    std::byte sides;
    std::byte density;
    svc::SectorSize size;
    std::byte online;

    using Bytes = std::span<std::byte, 12>;
    static constexpr infra::ByteCount kBytes{12};
    static constexpr std::byte kStepMark{0x03};
    static constexpr std::byte kFlat{0xFF};
    static constexpr std::byte kDense{0x04};

    static constexpr infra::ByteOffset kTracksAt{0};
    static constexpr infra::ByteOffset kStepAt{1};
    static constexpr infra::ByteOffset kPerTrackAt{2};

    static constexpr infra::ByteOffset kPerTrackHighAt{2};
    static constexpr infra::ByteOffset kSidesAt{4};
    static constexpr infra::ByteOffset kDensityAt{5};
    static constexpr infra::ByteOffset kSizeAt{6};
    static constexpr infra::ByteOffset kOnlineAt{8};
    static constexpr infra::ByteOffset kReservedAt{9};

    [[nodiscard]] static constexpr bool standard(svc::SectorCount total) noexcept {
        constexpr svc::SectorCount kSingle{720};
        constexpr svc::SectorCount kMedium{1040};
        constexpr svc::SectorCount kDouble{1440};
        return total == kSingle || total == kMedium || total == kDouble;
    }

    [[nodiscard]] static SioPercom of(
        svc::SectorCount total,
        svc::SectorSize size,
        bool standard_drive
    ) noexcept;
    void encode(Bytes out) const noexcept;
};

struct HostText {
    infra::ByteOffset length_at;
    infra::ByteOffset from;
    infra::ByteOffset to;
};

inline constexpr HostText kHostName{0x0E, 0x0F, 0x10};
inline constexpr HostText kHostLabel{0x37, 0x38, 0x38};

inline constexpr infra::ByteOffset kInfoMarkAt{0};
inline constexpr infra::ByteOffset kInfoMark2At{2};
inline constexpr infra::ByteOffset kInfoIdAt{3};
inline constexpr infra::ByteOffset kInfoSizeAt{6};
inline constexpr infra::ByteOffset kInfoCountAt{8};
inline constexpr infra::ByteOffset kInfoFirstAt{0x0C};
inline constexpr infra::ByteCount kInfoFirstBytes{3};
inline constexpr infra::ByteOffset kInfoName{0x10};
inline constexpr infra::ByteOffset kInfoMetaFrom{0x10};
inline constexpr infra::ByteCount kInfoMetaBytes{40};

void encode_device_info(
    std::span<std::byte> buffer,
    infra::ByteCount length,
    std::optional<SioPartition> partition,
    svc::SectorSize size,
    svc::SectorCount count,
    bool disk
) noexcept;

void copy_host_text(
    std::span<std::byte> buffer,
    std::span<const std::byte> host,
    HostText at
) noexcept;

}
