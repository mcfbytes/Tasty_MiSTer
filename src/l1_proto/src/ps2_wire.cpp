// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_wire.h"

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/endian.h"
#include "infra/prelude.h"

namespace mister::proto {

namespace {

constexpr hal::SpiWord kReplyWordBase{0xFF00u};

constexpr std::size_t kPairBytes = 2;
}

[[nodiscard]] Ex<void> write_ps2_frame(ILinkFrame& frame, const Ps2Frame& f) {
    if (f.len == 0
        || f.len > kPs2FrameBytes
        || infra::ordinal(f.stream) >= infra::ordinal(Ps2Stream::kCount)) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    const Ps2StreamFacts& facts = ps2_stream_facts(f.stream);
    if (facts.framing == Ps2Framing::LePairs && (f.len % kPairBytes) != 0u) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    std::size_t n = 0;
    hal::SpiWord words[kPs2FrameBytes];
    switch (facts.framing) {
        case Ps2Framing::LePairs:
            for (std::uint8_t i = 0; i + 1u < f.len;
                 i = static_cast<std::uint8_t>(i + kPairBytes)) {
                words[n++] = hal::SpiWord::load_le(
                    std::span<const std::uint8_t, kPairBytes>(f.bytes.data() + i, kPairBytes)
                );
            }
            break;
        case Ps2Framing::ReplyBytes:
            for (std::uint8_t i = 0; i < f.len; ++i) {
                words[n++] = kReplyWordBase.with(hal::SpiWord{f.bytes[i]});
            }
            break;
        case Ps2Framing::Bytes:
            for (std::uint8_t i = 0; i < f.len; ++i)
                words[n++] = hal::SpiWord{f.bytes[i]};
            break;
        case Ps2Framing::kCount:
            return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    return send_frame(frame, facts.opcode, std::span<const hal::SpiWord>(words, n));
}

}
