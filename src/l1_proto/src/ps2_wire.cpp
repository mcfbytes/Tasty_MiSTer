// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_wire.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "infra/prelude.h"

namespace mister::proto {

namespace {

constexpr hal::LinkWord kReplyWordBase{0xFF00u};

constexpr std::size_t kPairBytes = 2;
}

std::size_t ps2_frame_word_count(const Ps2Frame& f) noexcept {
    if (f.len == 0
        || f.len > kPs2FrameBytes
        || infra::ordinal(f.stream) >= infra::ordinal(Ps2Stream::kCount)) {
        return 0;
    }
    if (ps2_stream_facts(f.stream).framing != Ps2Framing::LePairs)
        return f.len;
    return (f.len % kPairBytes) == 0u ? f.len / kPairBytes : 0;
}

hal::LinkWord ps2_frame_word(const Ps2Frame& f, std::size_t i) noexcept {
    switch (ps2_stream_facts(f.stream).framing) {
        case Ps2Framing::LePairs:
            return hal::LinkWord::load_le(
                std::span<const std::uint8_t>(f.bytes).subspan(i * kPairBytes).first<kPairBytes>()
            );
        case Ps2Framing::ReplyBytes:
            return kReplyWordBase.with(hal::LinkWord{f.bytes[i]});
        case Ps2Framing::Bytes:
        case Ps2Framing::kCount:
            break;
    }
    return hal::LinkWord{f.bytes[i]};
}

[[nodiscard]] Ex<void> write_ps2_frame(ILinkFrame& frame, const Ps2Frame& f) {
    const std::size_t n = ps2_frame_word_count(f);
    if (n == 0)
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    hal::LinkWord words[kPs2FrameBytes];
    for (std::size_t i = 0; i < n; ++i)
        words[i] = ps2_frame_word(f, i);
    return send_frame(
        frame,
        ps2_stream_facts(f.stream).opcode,
        std::span<const hal::LinkWord>(words, n)
    );
}

}
