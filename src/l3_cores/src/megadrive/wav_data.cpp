// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/megadrive/wav_data.h"
#include "infra/span_cut.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>

#include "infra/endian.h"
#include "infra/prelude.h"

namespace mister::cores::megadrive::wav {
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

    const proto::FileSize file_size = proto::FileSize::of(image).min(kRiffReach);
    proto::FileOffset pos = proto::kFileStart + kPrefix;
    while (can_read_header(pos, file_size)) {
        const ChunkHeader header = infra::affine::from(image, pos).first<kChunkHeaderBytes>();
        const std::uint32_t size = declared_size(header);
        if (is_data_chunk(header))
            return WavData{pos + kChunkHeader, size};

        const proto::FileOffset next = pos + kChunkHeader + proto::FileSize{size};
        if (next > kRiffPositionLimit)
            return refuse(Refusal::kSizeOverflows, ERR_SITE());
        pos = next;
    }
    return refuse(Refusal::kNoDataChunk, ERR_SITE());
}

}
