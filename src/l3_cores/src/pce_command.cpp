// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_command.h"

#include <cstdint>
#include <optional>
#include <span>

#include "cd_core_detail.h"
#include "cores/cd_msf.h"
#include "cores/scsi_cdb.h"
#include "proto/types.h"
#include "svc/msf.h"
#include "svc/toc.h"
#include "svc/track.h"

namespace mister::cores {

PceCommand PceCommand::of(std::span<const std::uint8_t, kBytes> raw) noexcept {
    PceCommand out;
    Opcode op{};
    switch (raw[kOpcodeAt]) {
        case TestUnit:
            op = TestUnit;
            break;
        case RequestSense:
            op = RequestSense;
            break;
        case Read6:
            op = Read6;
            break;
        case ModeSelect6:
            op = ModeSelect6;
            break;
        case Sapsp:
            op = Sapsp;
            break;
        case Sapep:
            op = Sapep;
            break;
        case Pause:
            op = Pause;
            break;
        case ReadSubq:
            op = ReadSubq;
            break;
        case GetDirInfo:
            op = GetDirInfo;
            break;
        default:
            return out;
    }
    out.opcode_ = op;
    out.fast_seek_ = (raw[kFlagsAt] & kFastSeekFlag) != 0;
    if (op == Read6) {
        if (const auto cdb = Cdb::decode(std::as_bytes(raw))) {
            out.read_head_ = HeadLba::of(cdb->lba);
            out.read_count_ = SectorCount::of(cdb->transfer);
        }
    } else if (op == Sapsp || op == Sapep) {
        switch (raw[kAddressingAt] & kAddressingMask) {
            case MsfMode:
                out.addressing_ = MsfMode;
                break;
            case TrackMode:
                out.addressing_ = TrackMode;
                break;
            default:
                out.addressing_ = LbaMode;
                break;
        }
        out.cdda_mode_ = cdda_mode_of(raw[kModeAt]);
        out.msf_ = Msf{
            proto::PackedBcd{raw[kStartHighAt]}.value(),
            proto::PackedBcd{raw[kStartMidAt]}.value(),
            proto::PackedBcd{raw[kStartLowAt]}.value()
        };
        out.track_bcd_ = proto::PackedBcd{raw[kStartHighAt]};
        const std::uint32_t high = raw[kStartMidAt];
        const std::uint32_t mid = raw[kStartLowAt];
        const std::uint32_t low = raw[kStartLow2At];
        out.raw_head_ = HeadLba::of(proto::Lba{(high << kHighShift) | (mid << kMidShift) | low});
    } else if (op == GetDirInfo) {
        switch (raw[kModeAt]) {
            case 0:
                out.dir_info_ = DirInfo::Summary;
                break;
            case 1:
                out.dir_info_ = DirInfo::DiscEnd;
                break;
            case 2:
                out.dir_info_ = DirInfo::Track;
                break;
            default:
                break;
        }
        out.track_bcd_ = proto::PackedBcd{raw[kStartHighAt]};
    } else if (op == ModeSelect6) {
        out.data_request_ = raw[kStartLowAt] != 0;
    }
    return out;
}

std::optional<PceCommand::Opcode> PceCommand::opcode() const noexcept {
    return opcode_;
}

PceCommand::ReadOperands PceCommand::read6() const noexcept {
    return ReadOperands{read_head_, read_count_};
}

bool PceCommand::fast_seek() const noexcept {
    return fast_seek_;
}

std::optional<PceCommand::CddaMode> PceCommand::cdda_mode() const noexcept {
    return cdda_mode_;
}

std::optional<PceCommand::DirInfo> PceCommand::dir_info() const noexcept {
    return dir_info_;
}

bool PceCommand::requests_data() const noexcept {
    return data_request_;
}

HeadLba PceCommand::target(bool clamp_track, const svc::Toc& toc) const noexcept {
    switch (addressing_) {
        case MsfMode:
            return HeadLba::of(msf_) - kPregapSectors;
        case TrackMode: {

            const std::uint32_t note = track_bcd_.value();
            if (clamp_track) {
                const svc::TrackNumber number = toc.last.clamped_number(svc::CueField{note});
                return HeadLba::of(toc.at(number.index()).start);
            }
            const svc::TrackIndex idx = svc::TrackNumber::of(note == 0 ? 1 : note).index();
            if (!toc.last.contains(idx))
                return HeadLba::of(toc.end);
            return HeadLba::of(toc.at(idx).start);
        }
        case LbaMode:
        default:
            return raw_head_;
    }
}

svc::TrackNumber PceCommand::clamped_track(const svc::Toc& toc) const noexcept {
    return toc.last.clamped_number(svc::CueField{track_bcd_.value()});
}

}
