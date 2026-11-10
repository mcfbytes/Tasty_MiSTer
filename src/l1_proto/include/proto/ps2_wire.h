// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "infra/error.h"
#include "infra/message_sum.h"
#include "proto/link_frame.h"
#include "proto/ps2_frame.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

enum class Ps2Framing : std::uint8_t { Bytes, ReplyBytes, LePairs, kCount };

struct Ps2StreamFacts {
    UioOp opcode;
    Ps2Framing framing;
};

inline constexpr auto kPs2StreamFacts = std::to_array<Ps2StreamFacts>({
    {.opcode = UioOp::Keyboard, .framing = Ps2Framing::Bytes},
    {.opcode = UioOp::Keyboard, .framing = Ps2Framing::ReplyBytes},
    {.opcode = UioOp::Mouse, .framing = Ps2Framing::LePairs},
    {.opcode = UioOp::Mouse, .framing = Ps2Framing::ReplyBytes},
});

consteval bool ps2_stream_facts_are_ordinal() {
    if (kPs2StreamFacts.size() != infra::ordinal(Ps2Stream::kCount))
        return false;
    for (const Ps2StreamFacts& row : kPs2StreamFacts)
        if (row.framing == Ps2Framing::kCount)
            return false;
    return true;
}

static_assert(ps2_stream_facts_are_ordinal(), "kPs2StreamFacts is one row per Ps2Stream");

[[nodiscard]] constexpr const Ps2StreamFacts& ps2_stream_facts(Ps2Stream s) noexcept {
    return kPs2StreamFacts[infra::ordinal(s)];
}

[[nodiscard]] Ex<void> write_ps2_frame(ILinkFrame& frame, const Ps2Frame& f);

}
