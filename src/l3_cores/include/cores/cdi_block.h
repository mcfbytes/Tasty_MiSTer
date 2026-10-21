// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "proto/block_geometry.h"
#include "svc/toc.h"

namespace mister::cores::cdi {

inline constexpr std::size_t kSectorBytes = 2352;

inline constexpr std::size_t kQBytes = 24;
inline constexpr std::size_t kRwBytes = 192;
inline constexpr std::size_t kSubcodeBytes = kQBytes + kRwBytes;
inline constexpr std::size_t kBlockBytes = kSectorBytes + kSubcodeBytes;
inline constexpr std::size_t kRawSubcodeBytes = 96;

consteval std::array<std::uint16_t, 256> make_crc_table() {
    std::array<std::uint16_t, 256> t{};
    for (unsigned i = 0; i < 256; ++i) {
        unsigned c = i << 8;
        for (int k = 0; k < 8; ++k)
            c = (c & 0x8000u) ? ((c << 1) ^ 0x1021u) : (c << 1);
        t[i] = static_cast<std::uint16_t>(c & 0xFFFFu);
    }
    return t;
}

inline constexpr auto kCrcTable = make_crc_table();

constexpr std::uint8_t bcd(unsigned v) noexcept {
    return static_cast<std::uint8_t>(((v / 10) << 4) | (v % 10));
}

constexpr std::uint32_t hunk_window(std::uint32_t hunk_bytes) noexcept {
    return hunk_bytes != 0 && hunk_bytes <= proto::kBlockStagingBytes
        ? hunk_bytes / static_cast<std::uint32_t>(kBlockBytes)
        : 0;
}

inline constexpr std::array<std::uint8_t, 15> kBootHeader =
    {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x02, 0x16};

inline constexpr std::size_t kTocEntries = 306;
using TocEntry = std::array<std::uint8_t, 5>;

void compose_q(std::int32_t lba, const svc::Toc& toc, std::span<std::uint8_t, kQBytes> q) noexcept;

void compose_toc_q(
    std::int32_t lba,
    std::span<const TocEntry> entries,
    std::span<std::uint8_t, kQBytes> q
) noexcept;

void widen_rw(
    std::span<const std::uint8_t, kRawSubcodeBytes> symbols,
    std::span<std::uint8_t, kRwBytes> rw
) noexcept;

std::size_t build_toc(
    const svc::Toc& toc,
    std::uint8_t disc_type,
    std::span<TocEntry, kTocEntries> out
) noexcept;

[[nodiscard]] std::uint8_t disc_type(const svc::Toc& toc, std::uint8_t previous) noexcept;

}
