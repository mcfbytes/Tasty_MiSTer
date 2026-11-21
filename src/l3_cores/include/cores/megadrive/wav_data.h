// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/types.h"

namespace mister::cores::megadrive::wav {

inline constexpr std::uint32_t kPrefixBytes = 12;
inline constexpr std::uint32_t kChunkHeaderBytes = 8;
inline constexpr proto::FileSize kPrefix{kPrefixBytes};
inline constexpr proto::FileSize kChunkHeader{kChunkHeaderBytes};

inline constexpr proto::FileSize kRiffReach{std::numeric_limits<std::uint32_t>::max()};
inline constexpr proto::FileOffset kRiffPositionLimit = proto::kFileStart + kRiffReach;

using ChunkHeader = std::span<const std::byte, kChunkHeaderBytes>;

enum class Refusal : std::uint32_t {

    kTooSmall = 1,
    kNoDataChunk = 2,
    kSizeOverflows = 3,
};

struct WavData {
    proto::FileOffset start = proto::kFileStart;
    std::uint32_t size = 0;
};

[[nodiscard]] constexpr bool can_read_header(
    proto::FileOffset pos,
    proto::FileSize file_size
) noexcept {
    return file_size.holds(pos + kChunkHeader);
}

[[nodiscard]] bool is_data_chunk(ChunkHeader header) noexcept;
[[nodiscard]] std::uint32_t declared_size(ChunkHeader header) noexcept;

[[nodiscard]] Ex<WavData> find_data_chunk(std::span<const std::byte> image) noexcept;

}
