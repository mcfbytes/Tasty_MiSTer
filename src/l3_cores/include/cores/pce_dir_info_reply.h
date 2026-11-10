// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/msf.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::cores {

struct PceDirInfoReply {
    enum : std::size_t {
        kLength = 0,
        kControl = 1,
        kMinute = 2,
        kSecond = 3,
        kFrame = 4,
        kType = 5,
        kBytes = 6,
    };

    enum : std::uint8_t {
        kLengthValue = 4,
        kControlValue = 0x80,
        kFirstTrack = 1,
        kMode1Type = 0x04,
        kMode2Type = 0x08,
    };

    PceDirInfoReply() noexcept;

    void set_disc_end(svc::Msf at) noexcept;

    void set_track(svc::Msf at, svc::TrackType type) noexcept;

    void set_summary(svc::TrackCount last) noexcept;
    [[nodiscard]] std::span<const std::uint8_t, kBytes> bytes() const noexcept;

private:
    void set_position(svc::Msf at) noexcept;
    std::array<std::uint8_t, kBytes> bytes_{};
};

}
