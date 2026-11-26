// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video/video_wire_emit.h"

#include "infra/prelude.h"
#include "svc/video/filter_kernel.h"
#include "video_service_detail.h"

namespace mister::svc::video {

[[nodiscard]] Ex<void> emit_video_words(
    proto::ILinkFrame& frame,
    proto::UioOp opcode,
    std::span<const hal::LinkWord> words
) {
    if (words.empty() || words.size() > kVideoWireWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    return proto::send_frame(frame, opcode, words);
}

[[nodiscard]] Ex<hal::LinkWord> emit_fbuf(proto::ILinkFrame& frame, hal::LinkWord word) {

    const hal::LinkWord words[1] = {word};
    if (auto r = proto::send_frame(
            frame,
            proto::UioOp::SetFbuf,
            std::span<const hal::LinkWord>(words, 1)
        );
        !r) {
        return std::unexpected(r.error());
    }

    return frame.opcode_reply().value_or(hal::LinkWord{});
}

[[nodiscard]] Ex<void> emit_ar_custom(
    proto::ILinkFrame& frame,
    std::span<const hal::LinkWord> words
) {
    if (words.size() != 4) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    return proto::send_frame(frame, proto::UioOp::SetArCust, words);
}

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    proto::ILinkFrame& frame,
    std::span<const hal::LinkWord> chunk
) {

    if (chunk.size() > kFilterCoeffWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(chunk.size()));
    }
    return proto::send_frame(frame, proto::UioOp::SetFltCoef, chunk);
}

}
