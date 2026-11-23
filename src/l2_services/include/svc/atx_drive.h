// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "svc/atx_types.h"
#include "svc/sector_ask.h"

namespace mister::svc {

enum class AtxAttempt : std::uint8_t { Plain, Reposition };

struct AtxDrive {
    std::chrono::microseconds checksum;
    std::chrono::microseconds step;
    std::chrono::microseconds settle;
    std::chrono::microseconds lost;
};

inline constexpr AtxDrive k810{
    std::chrono::microseconds{5136},
    std::chrono::microseconds{5300},
    std::chrono::microseconds{10000},
    std::chrono::microseconds{1566000}
};
inline constexpr AtxDrive k1050{
    std::chrono::microseconds{270},
    std::chrono::microseconds{20120},
    std::chrono::microseconds{20000},
    std::chrono::microseconds{942000}
};

struct TrackMove {
    AtxTrack from{};
    AtxTrack to{};

    [[nodiscard]] constexpr AtxTrackCount distance() const noexcept {
        return infra::affine::distance(from, to);
    }

    [[nodiscard]] constexpr bool inward() const noexcept { return to > from; }
};

class IAtxDriveModel {
public:
    [[nodiscard]] virtual AtxTrackCount steps(TrackMove move) const noexcept = 0;

    [[nodiscard]] virtual SectorSize after_long(
        SectorSize act,
        AtxChunkData long_chunk
    ) const noexcept = 0;

    [[nodiscard]] virtual std::span<const AtxAttempt> attempts() const noexcept = 0;

    [[nodiscard]] virtual std::chrono::microseconds reposition(AtxTrack track) const noexcept = 0;

    [[nodiscard]] virtual FdcStatus read_status(FdcStatus status) const noexcept = 0;

    [[nodiscard]] virtual const AtxDrive& drive() const noexcept = 0;

protected:
    IAtxDriveModel() = default;
    IAtxDriveModel(const IAtxDriveModel&) = default;
    IAtxDriveModel& operator=(const IAtxDriveModel&) = default;
    ~IAtxDriveModel() = default;
};

[[nodiscard]] const IAtxDriveModel& atx_drive_model(SectorDrive drive) noexcept;

}
