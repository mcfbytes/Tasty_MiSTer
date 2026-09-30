// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"

namespace mister::cores::wav {

inline constexpr std::uint32_t kPrefixBytes = 12;
inline constexpr std::uint32_t kChunkHeaderBytes = 8;

enum class Refusal : std::uint32_t {

    kTooSmall = 1,
    kNoDataChunk = 2,
    kSizeOverflows = 3,
};

struct WavData {
    std::uint32_t start = 0;
    std::uint32_t size = 0;
};

[[nodiscard]] constexpr bool can_read_header(std::uint32_t pos, std::uint32_t file_size) noexcept {
    return file_size >= kChunkHeaderBytes && pos < file_size - kChunkHeaderBytes;
}

[[nodiscard]] bool is_data_chunk(std::span<const std::byte> header) noexcept;
[[nodiscard]] std::uint32_t declared_size(std::span<const std::byte> header) noexcept;

[[nodiscard]] Ex<WavData> find_data_chunk(std::span<const std::byte> image) noexcept;

}  // namespace mister::cores::wav
