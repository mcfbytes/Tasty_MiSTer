// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "proto/link/pad_ports.h"

namespace mister::svc::input {

struct StickIndex {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::optional<proto::StickSide> side() const noexcept;
    friend constexpr bool operator==(StickIndex, StickIndex) = default;
};

inline constexpr StickIndex kLeftStick{0};
inline constexpr StickIndex kRightStick{1};
inline constexpr std::array<StickIndex, 2> kSticks{kLeftStick, kRightStick};

constexpr std::optional<proto::StickSide> StickIndex::side() const noexcept {
    if (*this == kLeftStick)
        return proto::StickSide::Left;
    if (*this == kRightStick)
        return proto::StickSide::Right;
    return std::nullopt;
}

}
