// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/wav_data.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>

#include "infra/endian.h"

namespace mister::cores::wav {
namespace {

[[nodiscard]] std::unexpected<Error> refuse(Refusal why, infra::ErrorSite site) noexcept {
    return std::unexpected(Error{Errc::bad_format, site, static_cast<std::uint32_t>(why)});
}

}

bool is_data_chunk(ChunkHeader header) noexcept {
    constexpr std::string_view kId{"data"};
    for (std::size_t i = 0; i < kId.size(); ++i) {
        if (static_cast<char>(header[i]) != kId[i])
            return false;
    }
    return true;
}

std::uint32_t declared_size(ChunkHeader header) noexcept {
    return infra::load_le<std::uint32_t>(header.subspan(4).first<4>());
}

[[nodiscard]] Ex<WavData> find_data_chunk(std::span<const std::byte> image) noexcept {
    if (image.size() < kChunkHeaderBytes)
        return refuse(Refusal::kTooSmall, ERR_SITE());

    const auto file_size = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(image.size(), std::numeric_limits<std::uint32_t>::max())
    );
    std::uint32_t pos = kPrefixBytes;
    while (can_read_header(pos, proto::FileSize{file_size})) {
        const ChunkHeader header = image.subspan(pos).first<kChunkHeaderBytes>();
        const std::uint32_t size = declared_size(header);
        if (is_data_chunk(header))
            return WavData{pos + kChunkHeaderBytes, size};

        const std::uint64_t next = std::uint64_t{pos} + kChunkHeaderBytes + size;
        if (next > std::numeric_limits<std::uint32_t>::max())
            return refuse(Refusal::kSizeOverflows, ERR_SITE());
        pos = static_cast<std::uint32_t>(next);
    }
    return refuse(Refusal::kNoDataChunk, ERR_SITE());
}

}
