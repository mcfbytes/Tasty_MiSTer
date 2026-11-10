// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::svc {

inline constexpr std::size_t kVideoWireWords = 26;

[[nodiscard]] Ex<void> emit_video_words(
    proto::ILinkFrame& frame,
    proto::UioOp opcode,
    std::span<const std::uint16_t> words
);

[[nodiscard]] Ex<std::uint16_t> emit_fbuf(proto::ILinkFrame& frame, std::uint16_t word);

[[nodiscard]] Ex<void> emit_ar_custom(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> words
);

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> chunk
);

}
