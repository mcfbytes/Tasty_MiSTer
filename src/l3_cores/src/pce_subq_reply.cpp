// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_subq_reply.h"

#include <span>

#include "cores/cdwire.h"

namespace mister::cores {

PceSubqReply::PceSubqReply() noexcept {
    bytes_[kLength] = kLengthValue;
    bytes_[kControl] = kControlValue;
    bytes_[kIndex] = kIndexValue;
}

void PceSubqReply::set_play_state(PlayState state) noexcept {
    bytes_[kStatus] = static_cast<std::uint8_t>(state);
}

void PceSubqReply::set_track(svc::TrackNumber number) noexcept {
    bytes_[kTrack] = cdwire::bcd(number);
}

void PceSubqReply::set_relative(svc::Msf msf) noexcept {
    bytes_[kRelMinute] = cdwire::bcd(msf.m);
    bytes_[kRelSecond] = cdwire::bcd(msf.s);
    bytes_[kRelFrame] = cdwire::bcd(msf.f);
}

void PceSubqReply::set_absolute(svc::Msf msf) noexcept {
    bytes_[kAbsMinute] = cdwire::bcd(msf.m);
    bytes_[kAbsSecond] = cdwire::bcd(msf.s);
    bytes_[kAbsFrame] = cdwire::bcd(msf.f);
}

std::span<const std::uint8_t, PceSubqReply::kBytes> PceSubqReply::bytes() const noexcept {
    return bytes_;
}

}
