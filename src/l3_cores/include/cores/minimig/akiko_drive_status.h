// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mister::cores::minimig::akiko {

struct DriveStatus {
    std::uint8_t v = 0;

    static const DriveStatus kError;
    static const DriveStatus kPlaying;
    static const DriveStatus kPlayEnd;

    static const DriveStatus kBadCommand;
    static const DriveStatus kChecksum;
    static const DriveStatus kNoDisk;

    static const DriveStatus kDoorClosed;

    static const DriveStatus kPlayStarted;

    static const DriveStatus kPlayCommandStarting;

    [[nodiscard]] constexpr std::byte wire_byte() const noexcept { return std::byte{v}; }

    [[nodiscard]] constexpr DriveStatus with(DriveStatus other) const noexcept {
        return DriveStatus{static_cast<std::uint8_t>(v | other.v)};
    }

    friend constexpr bool operator==(DriveStatus, DriveStatus) = default;
};

inline constexpr DriveStatus DriveStatus::kError{0x80};
inline constexpr DriveStatus DriveStatus::kPlaying{0x08};
inline constexpr DriveStatus DriveStatus::kPlayEnd{0x00};
inline constexpr DriveStatus DriveStatus::kBadCommand{0x80};
inline constexpr DriveStatus DriveStatus::kChecksum{0x88};
inline constexpr DriveStatus DriveStatus::kNoDisk{0xF8};
inline constexpr DriveStatus DriveStatus::kDoorClosed{0x01};
inline constexpr DriveStatus DriveStatus::kPlayStarted{0x02};
inline constexpr DriveStatus DriveStatus::kPlayCommandStarting{0x42};

}
