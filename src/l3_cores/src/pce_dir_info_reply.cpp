// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_dir_info_reply.h"

#include <cstdint>
#include <span>

#include "cores/cdwire.h"
#include "svc/track.h"

namespace mister::cores {

PceDirInfoReply::PceDirInfoReply() noexcept {
    bytes_[kLength] = kLengthValue;
    bytes_[kControl] = kControlValue;
}

void PceDirInfoReply::set_disc_end(svc::Msf at) noexcept {
    set_position(at);
}

void PceDirInfoReply::set_track(svc::Msf at, svc::TrackType type) noexcept {
    set_position(at);
    switch (type) {
        case svc::TrackType::Cdda:
            break;
        case svc::TrackType::Mode1:
            bytes_[kType] = kMode1Type;
            break;
        case svc::TrackType::Mode2:
            bytes_[kType] = kMode2Type;
            break;
    }
}

void PceDirInfoReply::set_summary(svc::TrackCount last) noexcept {
    bytes_[kMinute] = kFirstTrack;
    bytes_[kSecond] = last.bcd_byte();
}

void PceDirInfoReply::set_position(svc::Msf at) noexcept {
    bytes_[kMinute] = cdwire::bcd(at.m);
    bytes_[kSecond] = cdwire::bcd(at.s);
    bytes_[kFrame] = cdwire::bcd(at.f);
}

std::span<const std::uint8_t, PceDirInfoReply::kBytes> PceDirInfoReply::bytes() const noexcept {
    return bytes_;
}

}
