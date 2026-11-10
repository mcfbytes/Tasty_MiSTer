// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/pce_subq.h"

#include <cstdint>
#include <span>

#include "cores/cdwire.h"

namespace mister::cores {

PceSubq::PceSubq() noexcept {
    bytes_[kControl] = kTimingData;
    bytes_[kIndex] = kIndexFixed;
}

void PceSubq::set_track(svc::TrackNumber number) noexcept {
    bytes_[kTrack] = cdwire::bcd(number);
}

void PceSubq::set_relative(svc::Msf msf) noexcept {
    bytes_[kRelMinute] = cdwire::bcd(msf.m);
    bytes_[kRelSecond] = cdwire::bcd(msf.s);
    bytes_[kRelFrame] = cdwire::bcd(msf.f);
}

void PceSubq::set_absolute(svc::Msf msf) noexcept {
    bytes_[kAbsMinute] = cdwire::bcd(msf.m);
    bytes_[kAbsSecond] = cdwire::bcd(msf.s);
    bytes_[kAbsFrame] = cdwire::bcd(msf.f);
}

void PceSubq::seal() noexcept {
    std::uint8_t msb = 0;
    std::uint8_t lsb = 0;
    for (std::size_t i = 0; i < kSumBytes; ++i) {
        std::uint8_t x = static_cast<std::uint8_t>(bytes_[i] ^ msb);
        x = static_cast<std::uint8_t>(x ^ (x >> kHalfShift));
        msb = static_cast<std::uint8_t>(
            lsb ^ (x >> kThreeBits) ^ static_cast<std::uint8_t>(x << kHalfShift)
        );
        lsb = static_cast<std::uint8_t>(x ^ static_cast<std::uint8_t>(x << kFiveBits));
    }
    bytes_[kCrcHigh] = static_cast<std::uint8_t>(msb ^ kByteMask);
    bytes_[kCrcLow] = static_cast<std::uint8_t>(lsb ^ kByteMask);
}

std::span<const std::uint8_t, PceSubq::kBytes> PceSubq::bytes() const noexcept {
    return bytes_;
}

}
