// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "hal/types.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/link_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::svc::video {

inline constexpr std::size_t kVideoWireWords = 26;

[[nodiscard]] Ex<void> emit_video_words(
    proto::ILinkFrame& frame,
    proto::UioOp opcode,
    std::span<const hal::SpiWord> words
);

[[nodiscard]] Ex<hal::SpiWord> emit_fbuf(proto::ILinkFrame& frame, hal::SpiWord word);

[[nodiscard]] Ex<void> emit_ar_custom(
    proto::ILinkFrame& frame,
    std::span<const hal::SpiWord> words
);

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    proto::ILinkFrame& frame,
    std::span<const hal::SpiWord> chunk
);

}
