// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"

#include "video_service_detail.h"

namespace mister::svc {

[[nodiscard]] Ex<void> emit_video_words(
    proto::ILinkFrame& frame,
    proto::UioOp opcode,
    std::span<const std::uint16_t> words
) {
    if (words.empty() || words.size() > kVideoWireWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    return proto::send_frame(frame, opcode, words);
}

[[nodiscard]] Ex<std::uint16_t> emit_fbuf(proto::ILinkFrame& frame, std::uint16_t word) {

    const std::uint16_t words[1] = {word};
    if (auto r = proto::send_frame(
            frame,
            proto::UioOp::SetFbuf,
            std::span<const std::uint16_t>(words, 1)
        );
        !r) {
        return std::unexpected(r.error());
    }

    return frame.opcode_reply().value_or(0);
}

[[nodiscard]] Ex<void> emit_ar_custom(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> words
) {
    if (words.size() != 4) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    return proto::send_frame(frame, proto::UioOp::SetArCust, words);
}

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    proto::ILinkFrame& frame,
    std::span<const std::uint16_t> chunk
) {

    if (chunk.size() > kFilterCoeffWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(chunk.size()));
    }
    return proto::send_frame(frame, proto::UioOp::SetFltCoef, chunk);
}

}
