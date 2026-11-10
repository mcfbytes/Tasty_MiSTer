// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "svc/track.h"

namespace mister::cores {

struct TocPoint {
    static constexpr std::uint8_t kFirstTrackInfo = 0xA0;
    static constexpr std::uint8_t kLastTrackInfo = 0xA1;
    static constexpr std::uint8_t kLeadOut = 0xA2;
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr TocPoint of_track(svc::TrackNumber number) noexcept {
        return TocPoint{number.bcd_byte()};
    }

    [[nodiscard]] static constexpr TocPoint first_track_info() noexcept {
        return TocPoint{kFirstTrackInfo};
    }

    [[nodiscard]] static constexpr TocPoint last_track_info() noexcept {
        return TocPoint{kLastTrackInfo};
    }

    [[nodiscard]] static constexpr TocPoint lead_out() noexcept { return TocPoint{kLeadOut}; }

    constexpr void store(std::span<std::uint8_t, 2> dst) const noexcept {
        infra::store_be<std::uint16_t>(dst, v);
    }

    friend constexpr bool operator==(TocPoint, TocPoint) = default;
};

}
