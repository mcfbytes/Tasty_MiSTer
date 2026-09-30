// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/seat.h"

namespace mister::svc {

struct ScalingPolicy {
    TASTY_SEAT_EXEMPT(component);
    std::uint16_t scrw = 0;
    std::uint16_t scrh = 0;
    std::uint8_t vscale_mode = 0;
    std::uint8_t vscale_border = 0;
    std::uint8_t filter_mode = 0;
    bool front_end = false;

    std::uint64_t to_bits() const noexcept {
        return static_cast<std::uint64_t>(scrw) | (static_cast<std::uint64_t>(scrh) << 16) |
               (static_cast<std::uint64_t>(vscale_mode) << 32) |
               (static_cast<std::uint64_t>(vscale_border) << 40) |
               (static_cast<std::uint64_t>(filter_mode) << 48) |
               (static_cast<std::uint64_t>(front_end ? 1u : 0u) << 56);
    }
    static ScalingPolicy from_bits(std::uint64_t b) noexcept {
        ScalingPolicy p;
        p.scrw = static_cast<std::uint16_t>(b & 0xFFFFu);
        p.scrh = static_cast<std::uint16_t>((b >> 16) & 0xFFFFu);
        p.vscale_mode = static_cast<std::uint8_t>((b >> 32) & 0xFFu);
        p.vscale_border = static_cast<std::uint8_t>((b >> 40) & 0xFFu);
        p.filter_mode = static_cast<std::uint8_t>((b >> 48) & 0xFFu);
        p.front_end = ((b >> 56) & 1u) != 0u;
        return p;
    }
};

}  // namespace mister::svc
