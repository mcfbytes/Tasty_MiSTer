// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>

#include "cores/archie/kart_byte.h"
#include "proto/mouse_delta.h"

namespace mister::cores::archie {

struct KartAxis {

    static const KartAxis kMin;
    static const KartAxis kMax;
    static constexpr unsigned kWireMask = 0x7F;

    int v = 0;

    [[nodiscard]] static constexpr KartAxis x_of(proto::MouseDelta report) noexcept {
        return KartAxis{report.dx};
    }

    [[nodiscard]] static constexpr KartAxis y_of(proto::MouseDelta report) noexcept {
        return KartAxis{-report.dy};
    }

    [[nodiscard]] constexpr KartAxis plus(KartAxis more) const noexcept {
        return std::clamp(KartAxis{v + more.v}, kMin, kMax);
    }

    [[nodiscard]] constexpr bool at_rest() const noexcept { return v == 0; }

    [[nodiscard]] constexpr KartByte byte() const noexcept {
        return KartByte{static_cast<std::uint8_t>(static_cast<unsigned>(v) & kWireMask)};
    }

    friend constexpr bool operator==(KartAxis, KartAxis) = default;
    friend constexpr auto operator<=>(KartAxis, KartAxis) = default;
};

inline constexpr KartAxis KartAxis::kMin{-64};
inline constexpr KartAxis KartAxis::kMax{63};

}
