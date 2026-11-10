// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/msf.h"
#include "svc/track.h"

namespace mister::cores {

struct PceSubq {
    enum : std::size_t {
        kControl = 0,
        kTrack = 1,
        kIndex = 2,
        kRelMinute = 3,
        kRelSecond = 4,
        kRelFrame = 5,
        kZero = 6,
        kAbsMinute = 7,
        kAbsSecond = 8,
        kAbsFrame = 9,
        kCrcHigh = 10,
        kCrcLow = 11,
        kBytes = 12,
        kSumBytes = 10,
    };

    enum : std::uint8_t { kTimingData = 1, kIndexFixed = 1, kByteMask = 0xFF };

    enum : unsigned { kHalfShift = 4, kThreeBits = 3, kFiveBits = 5 };

    PceSubq() noexcept;
    void set_track(svc::TrackNumber number) noexcept;
    void set_relative(svc::Msf msf) noexcept;
    void set_absolute(svc::Msf msf) noexcept;

    void seal() noexcept;
    [[nodiscard]] std::span<const std::uint8_t, kBytes> bytes() const noexcept;

private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

}
