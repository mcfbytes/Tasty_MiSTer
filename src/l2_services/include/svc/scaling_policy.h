// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <utility>

#include "infra/seat.h"
#include "svc/types.h"

namespace mister::svc {

struct ScalingPolicy {
    TASTY_SEAT_EXEMPT(component);
    std::uint16_t scrw = 0;
    std::uint16_t scrh = 0;
    VscaleMode vscale_mode = VscaleMode::Fit;
    std::uint8_t vscale_border = 0;
    std::uint8_t filter_mode = 0;
    bool front_end = false;

private:
    enum class Lane : unsigned {
        Scrw = 0,
        Scrh = 16,
        VscaleMode = 32,
        VscaleBorder = 40,
        FilterMode = 48,
        FrontEnd = 56
    };

    template <Lane L, class T>
    static constexpr auto place(T field) noexcept {
        return std::uint64_t{static_cast<std::uint64_t>(field)} << std::to_underlying(L);
    }

    template <class T, Lane L, class B>
    static constexpr T take(B b) noexcept {
        return static_cast<T>(b >> std::to_underlying(L));
    }

public:
    [[nodiscard]] constexpr bool steps_height() const noexcept {
        return vscale_mode != VscaleMode::Fit;
    }

    [[nodiscard]] constexpr bool scales_by_resolution() const noexcept {
        return vscale_mode >= VscaleMode::ResolutionCoreAspect;
    }

    std::uint64_t to_bits() const noexcept {
        return place<Lane::Scrw>(scrw)
            | place<Lane::Scrh>(scrh)
            | place<Lane::VscaleMode>(std::to_underlying(vscale_mode))
            | place<Lane::VscaleBorder>(vscale_border)
            | place<Lane::FilterMode>(filter_mode)
            | place<Lane::FrontEnd>(front_end);
    }

    static ScalingPolicy from_bits(std::uint64_t b) noexcept {
        ScalingPolicy p;
        p.scrw = take<std::uint16_t, Lane::Scrw>(b);
        p.scrh = take<std::uint16_t, Lane::Scrh>(b);
        p.vscale_mode = VscaleMode{take<std::uint8_t, Lane::VscaleMode>(b)};
        p.vscale_border = take<std::uint8_t, Lane::VscaleBorder>(b);
        p.filter_mode = take<std::uint8_t, Lane::FilterMode>(b);
        p.front_end = ((b >> std::to_underlying(Lane::FrontEnd)) & 1u) != 0u;
        return p;
    }
};

}
