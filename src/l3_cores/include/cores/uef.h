// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mister::cores::uef {

inline constexpr std::size_t kHeaderBytes = 12;

inline constexpr std::size_t kChunkHeaderBytes = 6;

enum ChunkId : std::uint16_t {
    kInfoId = 0x0000,
    kTapeId = 0x0100,
    kHighToneId = 0x0110,
    kHighDummyId = 0x0111,
    kGapId = 0x0112,
    kFreqChgId = 0x0113,
    kSecurityId = 0x0114,
    kFloatGapId = 0x0116,
};

inline constexpr double kBaud = 1000000.0 / (16.0 * 52.0);

[[nodiscard]] constexpr std::uint32_t ms_to_bits(std::uint16_t ms) noexcept {
    return static_cast<std::uint32_t>(static_cast<double>(ms) * (kBaud / 1000.0));
}

inline constexpr std::uint8_t kStartBit = 0;
inline constexpr std::uint8_t kStopBit = 1;

inline constexpr std::uint8_t kHighDummyByte = 'A';

[[nodiscard]] bool valid_header(std::span<const std::uint8_t> image) noexcept;

struct ChunkInfo {
    std::uint16_t id = 0;
    std::uint32_t length = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t bit_offset_start = 0;
    std::uint32_t bit_offset_end = 0;
    std::uint32_t pre_carrier = 0;
};

[[nodiscard]] const ChunkInfo* locate_chunk(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    std::uint32_t& bit_pos
) noexcept;

[[nodiscard]] std::uint8_t bit_at(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    std::uint32_t bit_pos
) noexcept;

[[nodiscard]] std::uint32_t total_bits(std::span<const std::uint8_t> image) noexcept;

}
