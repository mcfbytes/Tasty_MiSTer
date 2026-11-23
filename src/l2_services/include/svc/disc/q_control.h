// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "proto/types.h"
#include "svc/disc/track.h"

namespace mister::svc::disc {

struct QControl {
    std::uint8_t v = 0;

    static const QControl kAudio;
    static const QControl kData;

    [[nodiscard]] static constexpr QControl of(TrackType type) noexcept {
        return type == TrackType::Cdda ? kAudio : kData;
    }

    [[nodiscard]] constexpr std::byte q_byte() const noexcept {
        constexpr std::uint8_t kAdr = 1;
        return std::byte{static_cast<std::uint8_t>((v << proto::PackedBcd::kNibbleBits) | kAdr)};
    }

    [[nodiscard]] constexpr std::byte adr_control_byte() const noexcept {
        constexpr std::uint8_t kAdr = 1;
        return std::byte{static_cast<std::uint8_t>((kAdr << proto::PackedBcd::kNibbleBits) | v)};
    }

    friend constexpr bool operator==(QControl, QControl) = default;
};

inline constexpr QControl QControl::kAudio{0};
inline constexpr QControl QControl::kData{4};

}
