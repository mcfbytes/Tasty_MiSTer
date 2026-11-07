// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "svc/pll_solver.h"

namespace mister::svc {

enum class ReducedBlanking : std::uint8_t { Off = 0, On = 1, OnIfClockHigh = 2 };
static_assert(sizeof(ReducedBlanking) == sizeof(std::uint8_t), "the rb field is a byte");

[[nodiscard]] constexpr std::optional<ReducedBlanking> reduced_blanking_of(
    std::uint8_t rb
) noexcept {
    switch (rb) {
        case static_cast<std::uint8_t>(ReducedBlanking::Off):
            return ReducedBlanking::Off;
        case static_cast<std::uint8_t>(ReducedBlanking::On):
            return ReducedBlanking::On;
        case static_cast<std::uint8_t>(ReducedBlanking::OnIfClockHigh):
            return ReducedBlanking::OnIfClockHigh;
        default:
            return std::nullopt;
    }
}

[[nodiscard]] constexpr std::uint8_t rb_byte(ReducedBlanking rb) noexcept {
    return static_cast<std::uint8_t>(rb);
}

static_assert(
    reduced_blanking_of(rb_byte(ReducedBlanking::OnIfClockHigh)) == ReducedBlanking::OnIfClockHigh,
    "the byte codec round-trips"
);
static_assert(!reduced_blanking_of(3).has_value(), "3 is not a blanking request");

struct Modeline {
    std::uint32_t hact = 0, hfp = 0, hs = 0, hbp = 0;
    std::uint32_t vact = 0, vfp = 0, vs = 0, vbp = 0;
    double fpix_mhz = 0.0;
    bool hpol = false, vpol = false;
    std::uint8_t vic = 0;
    std::uint8_t rb = 0;
    bool pixel_repeat = false;

    std::array<std::uint32_t, 26> to_words(const PllBlock& pll) const;

    struct WireOptions {
        bool use_vrr = false;
        bool vsync_align = false;

        bool direct_video = false;
    };

    std::array<std::uint16_t, 26> to_wire(const PllBlock& pll, const WireOptions& opt) const;

    Modeline direct_video_fix() const;
};

}
