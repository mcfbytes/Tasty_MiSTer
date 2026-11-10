// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/msf.h"
#include "svc/track.h"

namespace mister::cores {

struct PceSubqReply {
    enum : std::size_t {
        kLength = 0,
        kControl = 1,
        kStatus = 2,
        kZero = 3,
        kTrack = 4,
        kIndex = 5,
        kRelMinute = 6,
        kRelSecond = 7,
        kRelFrame = 8,
        kAbsMinute = 9,
        kAbsSecond = 10,
        kAbsFrame = 11,
        kBytes = 12,
    };

    enum : std::uint8_t { kLengthValue = 0x0A, kControlValue = 0x80, kIndexValue = 1 };

    enum class PlayState : std::uint8_t { Playing = 0x00, Paused = 0x02, Stopped = 0x03 };

    PceSubqReply() noexcept;
    void set_play_state(PlayState state) noexcept;
    void set_track(svc::TrackNumber number) noexcept;
    void set_relative(svc::Msf msf) noexcept;
    void set_absolute(svc::Msf msf) noexcept;
    [[nodiscard]] std::span<const std::uint8_t, kBytes> bytes() const noexcept;

private:
    std::array<std::uint8_t, kBytes> bytes_{};
};

}
