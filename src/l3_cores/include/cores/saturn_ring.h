// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "svc/track.h"

namespace mister::cores::saturn {

inline constexpr std::size_t kRingBodyFirst = 12;
inline constexpr std::size_t kRingBodyLast = 2348;
inline constexpr std::uint16_t kRingLfsrSeed = 1;

[[nodiscard]] constexpr std::uint16_t ring_lfsr_step(std::uint16_t lfsr) noexcept {
    const auto x = static_cast<std::uint16_t>((lfsr >> 1) ^ lfsr);
    const auto fed = static_cast<std::uint16_t>(lfsr | static_cast<std::uint16_t>(x << 15));
    return static_cast<std::uint16_t>(fed >> 1);
}

static_assert(ring_lfsr_step(1) == 0x4000);
static_assert(ring_lfsr_step(0) == 0);

[[nodiscard]] constexpr std::array<std::uint8_t, svc::kRawFrameBytes> compute_ring_body() noexcept {
    std::array<std::uint8_t, svc::kRawFrameBytes> out{};
    std::uint16_t lfsr = kRingLfsrSeed;
    for (std::size_t i = kRingBodyFirst; i < kRingBodyLast; ++i) {
        auto a = static_cast<std::uint8_t>((i & 1u) != 0u ? 0x59u : 0xA8u);
        for (int j = 0; j < 8; ++j) {
            a = static_cast<std::uint8_t>(a ^ (lfsr & 1u));
            a = static_cast<std::uint8_t>((a >> 1) | (a << 7));
            lfsr = ring_lfsr_step(lfsr);
        }
        out[i] = a;
    }
    return out;
}

inline constexpr std::array<std::uint8_t, svc::kRawFrameBytes> kRingSector = compute_ring_body();

constexpr void fill_ring_body(std::span<std::uint8_t, svc::kRawFrameBytes> buf) noexcept {
    for (std::size_t i = kRingBodyFirst; i < kRingBodyLast; ++i)
        buf[i] = kRingSector[i];
}

}
