// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/atx_drive.h"

#include <algorithm>
#include <array>

namespace mister::svc {

namespace {

constexpr std::array<AtxAttempt, 4>
    kAttempts810{AtxAttempt::Plain, AtxAttempt::Reposition, AtxAttempt::Plain, AtxAttempt::Plain};

constexpr std::array<AtxAttempt, 2> kAttempts1050{AtxAttempt::Reposition, AtxAttempt::Reposition};

class Atx810Model final : public IAtxDriveModel {
public:
    [[nodiscard]] AtxTrackCount steps(TrackMove move) const noexcept override {
        return move.distance();
    }

    [[nodiscard]] SectorSize after_long(
        SectorSize act,
        AtxChunkData long_chunk
    ) const noexcept override {
        return std::min(act, long_chunk.long_size());
    }

    [[nodiscard]] std::span<const AtxAttempt> attempts() const noexcept override {
        return kAttempts810;
    }

    [[nodiscard]] std::chrono::microseconds reposition(AtxTrack track) const noexcept override {
        constexpr AtxTrackCount kReturnTracks{43};
        return (kReturnTracks + (track - AtxTrack{})).times(drive().step) + drive().settle;
    }

    [[nodiscard]] FdcStatus read_status(FdcStatus status) const noexcept override {
        if (status.has(FdcStatus::kLost))
            status = status.without(FdcStatus::kLost.with(FdcStatus::kCrc)).with(FdcStatus::kBusy);
        if (status.has(FdcStatus::kRecordType))
            status = status.with(FdcStatus::kProtect);
        return status;
    }

    [[nodiscard]] const AtxDrive& drive() const noexcept override { return k810; }
};

class Atx1050Model final : public IAtxDriveModel {
public:
    [[nodiscard]] AtxTrackCount steps(TrackMove move) const noexcept override {
        const AtxTrackCount distance = move.distance();
        return move.inward() ? distance + AtxTrackCount{1} : distance;
    }

    [[nodiscard]] SectorSize after_long(
        SectorSize act,
        AtxChunkData long_chunk
    ) const noexcept override {
        return std::max(act, long_chunk.long_size());
    }

    [[nodiscard]] std::span<const AtxAttempt> attempts() const noexcept override {
        return kAttempts1050;
    }

    [[nodiscard]] std::chrono::microseconds reposition(AtxTrack track) const noexcept override {
        if (track == AtxTrack{})
            return std::chrono::microseconds{};
        const AtxTrackCount d = track - AtxTrack{};
        return (d + d + AtxTrackCount{1}).times(drive().step) + drive().settle;
    }

    [[nodiscard]] FdcStatus read_status(FdcStatus status) const noexcept override {
        if (!status.has(FdcStatus::kLost))
            return status;
        return status.with(FdcStatus::kDrq);
    }

    [[nodiscard]] const AtxDrive& drive() const noexcept override { return k1050; }
};

constinit const Atx810Model kAtx810Model{};
constinit const Atx1050Model kAtx1050Model{};

}

const IAtxDriveModel& atx_drive_model(SectorDrive drive) noexcept {
    switch (drive) {
        case SectorDrive::Model810:
            return kAtx810Model;
        case SectorDrive::Model1050:
            return kAtx1050Model;
    }
    return kAtx810Model;
}

}
