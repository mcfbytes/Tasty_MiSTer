// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/types.h"
#include "infra/endian.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/fio_opcodes.h"
#include "proto/link_sel.h"
#include "proto/osd_opcodes.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "proto/wire_word.h"

namespace mister::proto {

class ILinkFrame {
public:
    virtual ~ILinkFrame() = default;

    [[nodiscard]] virtual Ex<void> begin(LinkSel sel, hal::SpiWord op) = 0;

    [[nodiscard]] Ex<void> begin(UioOp op) { return begin(LinkSel::Io, proto::word(op)); }

    [[nodiscard]] Ex<void> begin(FioOp op) { return begin(LinkSel::Fpga, proto::word(op)); }

    [[nodiscard]] Ex<void> begin(OsdOp op) { return begin(LinkSel::Osd, proto::word(op)); }

    [[nodiscard]] virtual Ex<void> word(hal::SpiWord w) = 0;

    [[nodiscard]] virtual Ex<void> end() = 0;

    [[nodiscard]] virtual std::optional<hal::SpiWord> opcode_reply() const noexcept = 0;

protected:
    ILinkFrame() = default;
    ILinkFrame(const ILinkFrame&) = default;
    ILinkFrame& operator=(const ILinkFrame&) = default;
};

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    LinkSel sel,
    hal::SpiWord op,
    std::span<const hal::SpiWord> words
) {
    if (auto r = frame.begin(sel, op); !r)
        return r;
    for (const hal::SpiWord w : words) {
        if (auto r = frame.word(w); !r)
            return r;
    }
    return frame.end();
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    UioOp op,
    std::span<const hal::SpiWord> words
) {
    return send_frame(frame, LinkSel::Io, proto::word(op), words);
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    FioOp op,
    std::span<const hal::SpiWord> words
) {
    return send_frame(frame, LinkSel::Fpga, proto::word(op), words);
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    OsdOp op,
    std::span<const hal::SpiWord> words
) {
    return send_frame(frame, LinkSel::Osd, proto::word(op), words);
}

[[nodiscard]] inline Ex<void> emit_u32(ILinkFrame& frame, std::uint32_t v, hal::Width framing) {
    std::array<std::uint8_t, sizeof(std::uint32_t)> bytes{};
    infra::store_le<std::uint32_t>(std::span<std::uint8_t, sizeof(std::uint32_t)>(bytes), v);
    auto walk = for_each_word(framing, bytes, [&frame](hal::SpiWord word) {
        if (auto r = frame.word(word); !r)
            return Ex<bool>{std::unexpected(r.error())};
        return Ex<bool>{true};
    });
    if (!walk)
        return std::unexpected(walk.error());
    return {};
}

[[nodiscard]] inline Ex<void> FileSize::emit_words(ILinkFrame& frame, hal::Width framing) const {
    if (auto r = emit_u32(frame, low_word(), framing); !r)
        return r;
    return emit_u32(frame, high_word(), framing);
}

}
