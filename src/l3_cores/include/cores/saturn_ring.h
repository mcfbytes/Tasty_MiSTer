// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::cores::saturn {

inline constexpr std::size_t kFrameBytes = 12;

inline constexpr std::size_t kFrameSumBytes = 11;

[[nodiscard]] constexpr std::uint8_t frame_checksum(
    std::span<const std::uint8_t, kFrameSumBytes> frame
) noexcept {
    std::uint8_t sum = 0;
    for (const std::uint8_t b : frame)
        sum = static_cast<std::uint8_t>(sum + b);
    return static_cast<std::uint8_t>(~sum);
}

inline constexpr std::size_t kSectorBytes = 2352;

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

[[nodiscard]] constexpr std::array<std::uint8_t, kSectorBytes> compute_ring_body() noexcept {
    std::array<std::uint8_t, kSectorBytes> out{};
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

inline constexpr std::array<std::uint8_t, kSectorBytes> kRingSector = compute_ring_body();

constexpr void fill_ring_body(std::span<std::uint8_t, kSectorBytes> buf) noexcept {
    for (std::size_t i = kRingBodyFirst; i < kRingBodyLast; ++i)
        buf[i] = kRingSector[i];
}

inline constexpr std::uint32_t kEdcPoly = 0xD801'8001u;

[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_edc_table() noexcept {
    std::array<std::uint32_t, 256> tab{};
    for (std::uint32_t i = 0; i < 256u; ++i) {
        std::uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c >> 1) ^ ((c & 1u) != 0u ? kEdcPoly : 0u);
        tab[i] = c;
    }
    return tab;
}

inline constexpr std::array<std::uint32_t, 256> kEdcTable = make_edc_table();

static_assert(kEdcTable[0] == 0u);
static_assert(kEdcTable[1] == 0x90910101u);

[[nodiscard]] constexpr std::uint32_t edc(std::span<const std::uint8_t> data) noexcept {
    std::uint32_t crc = 0;
    for (const std::uint8_t b : data) {
        crc ^= b;
        crc = (crc >> 8) ^ kEdcTable[crc & 0xFFu];
    }
    return crc;
}

inline constexpr std::size_t kMode1EdcSpan = 2064;
inline constexpr std::size_t kMode2EdcSpan = 2348;
inline constexpr std::size_t kMode1EdcOffset = 2064;
inline constexpr std::size_t kMode1TailFirst = 2068;

static_assert(kMode1EdcOffset == kMode1EdcSpan);
static_assert(kMode1TailFirst == kMode1EdcOffset + 4);
static_assert(kMode2EdcSpan + 4 == kSectorBytes);

}
