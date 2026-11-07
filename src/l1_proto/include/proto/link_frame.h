// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "hal/types.h"
#include "hal/uio_frame_lane.h"
#include "infra/error.h"
#include "proto/fio_opcodes.h"
#include "proto/link_route.h"
#include "proto/osd_opcodes.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using FrameSel = hal::UioFrameLane::Sel;

class ILinkFrame {
public:
    virtual ~ILinkFrame() = default;

    [[nodiscard]] virtual Ex<void> begin(FrameSel sel, std::uint16_t op) = 0;

    [[nodiscard]] virtual Ex<void> begin(LinkRoute route, std::uint16_t op) = 0;

    [[nodiscard]] Ex<void> begin(UioOp op) { return begin(LinkRoute::Uio, proto::word(op).v); }

    [[nodiscard]] Ex<void> begin(FioOp op) { return begin(LinkRoute::Fio, proto::word(op).v); }

    [[nodiscard]] Ex<void> begin(OsdOp op) { return begin(LinkRoute::Osd, proto::word(op).v); }

    [[nodiscard]] virtual Ex<void> word(std::uint16_t w) = 0;

    [[nodiscard]] virtual Ex<void> end() = 0;

    [[nodiscard]] virtual std::optional<std::uint16_t> opcode_reply() const noexcept = 0;

protected:
    ILinkFrame() = default;
    ILinkFrame(const ILinkFrame&) = default;
    ILinkFrame& operator=(const ILinkFrame&) = default;
};

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    FrameSel sel,
    std::uint16_t op,
    std::span<const std::uint16_t> words
) {
    if (auto r = frame.begin(sel, op); !r)
        return r;
    for (const std::uint16_t w : words) {
        if (auto r = frame.word(w); !r)
            return r;
    }
    return frame.end();
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    LinkRoute route,
    std::uint16_t op,
    std::span<const std::uint16_t> words
) {
    if (auto r = frame.begin(route, op); !r)
        return r;
    for (const std::uint16_t w : words) {
        if (auto r = frame.word(w); !r)
            return r;
    }
    return frame.end();
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    UioOp op,
    std::span<const std::uint16_t> words
) {
    return send_frame(frame, LinkRoute::Uio, proto::word(op).v, words);
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    FioOp op,
    std::span<const std::uint16_t> words
) {
    return send_frame(frame, LinkRoute::Fio, proto::word(op).v, words);
}

[[nodiscard]] inline Ex<void> send_frame(
    ILinkFrame& frame,
    OsdOp op,
    std::span<const std::uint16_t> words
) {
    return send_frame(frame, LinkRoute::Osd, proto::word(op).v, words);
}

[[nodiscard]] inline Ex<void> emit_u32(ILinkFrame& frame, std::uint32_t v, hal::Width framing) {
    constexpr unsigned kByteBits = 8;
    constexpr unsigned kHalfBits = 16;
    constexpr std::uint32_t kByteMask = 0xFFu;
    if (framing == hal::Width::Word) {
        if (auto r = frame.word(static_cast<std::uint16_t>(v)); !r)
            return r;
        return frame.word(static_cast<std::uint16_t>(v >> kHalfBits));
    }
    for (unsigned shift = 0; shift < kHalfBits * 2u; shift += kByteBits) {
        if (auto r = frame.word(static_cast<std::uint16_t>((v >> shift) & kByteMask)); !r)
            return r;
    }
    return {};
}

}
