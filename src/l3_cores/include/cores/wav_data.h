// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/types.h"

namespace mister::cores::wav {

inline constexpr std::uint32_t kPrefixBytes = 12;
inline constexpr std::uint32_t kChunkHeaderBytes = 8;

using ChunkHeader = std::span<const std::byte, kChunkHeaderBytes>;

enum class Refusal : std::uint32_t {

    kTooSmall = 1,
    kNoDataChunk = 2,
    kSizeOverflows = 3,
};

struct WavData {
    std::uint32_t start = 0;
    std::uint32_t size = 0;
};

[[nodiscard]] constexpr bool can_read_header(
    std::uint32_t pos,
    proto::FileSize file_size
) noexcept {
    return file_size.v >= kChunkHeaderBytes && pos < file_size.v - kChunkHeaderBytes;
}

[[nodiscard]] bool is_data_chunk(ChunkHeader header) noexcept;
[[nodiscard]] std::uint32_t declared_size(ChunkHeader header) noexcept;

[[nodiscard]] Ex<WavData> find_data_chunk(std::span<const std::byte> image) noexcept;

}
