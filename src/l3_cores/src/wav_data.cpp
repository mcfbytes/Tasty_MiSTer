// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/wav_data.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace mister::cores::wav {
namespace {

[[nodiscard]] std::unexpected<Error> refuse(Refusal why, std::uint16_t site) noexcept {
    return std::unexpected(Error{Errc::bad_format, site, static_cast<std::uint32_t>(why)});
}

}  // namespace

bool is_data_chunk(std::span<const std::byte> header) noexcept {
    constexpr std::string_view kId{"data"};
    if (header.size() < kId.size()) return false;
    for (std::size_t i = 0; i < kId.size(); ++i) {
        if (static_cast<char>(header[i]) != kId[i]) return false;
    }
    return true;
}

std::uint32_t declared_size(std::span<const std::byte> header) noexcept {
    if (header.size() < kChunkHeaderBytes) return 0;
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(header[4 + i]) << (8 * i);
    }
    return v;
}

[[nodiscard]] Ex<WavData> find_data_chunk(std::span<const std::byte> image) noexcept {
    if (image.size() < kChunkHeaderBytes) return refuse(Refusal::kTooSmall, ERR_SITE());

    const auto file_size =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(image.size(), 0xFFFFFFFFu));
    std::uint32_t pos = kPrefixBytes;
    while (can_read_header(pos, file_size)) {
        const std::span<const std::byte> header = image.subspan(pos, kChunkHeaderBytes);
        const std::uint32_t size = declared_size(header);
        if (is_data_chunk(header)) return WavData{pos + kChunkHeaderBytes, size};

        const std::uint64_t next = std::uint64_t{pos} + kChunkHeaderBytes + size;
        if (next > 0xFFFFFFFFull) return refuse(Refusal::kSizeOverflows, ERR_SITE());
        pos = static_cast<std::uint32_t>(next);
    }
    return refuse(Refusal::kNoDataChunk, ERR_SITE());
}

}  // namespace mister::cores::wav
