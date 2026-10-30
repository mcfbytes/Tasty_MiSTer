// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "hal/uio_frame_lane.h"
#include "infra/error.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

using FrameSel = hal::UioFrameLane::Sel;

class ILinkFrame {
public:
    virtual ~ILinkFrame() = default;

    [[nodiscard]] virtual Ex<void> begin(FrameSel sel, std::uint16_t op) = 0;

    [[nodiscard]] Ex<void> begin(UioOp op) { return begin(FrameSel::Uio, proto::word(op).v); }

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
    UioOp op,
    std::span<const std::uint16_t> words
) {
    return send_frame(frame, FrameSel::Uio, proto::word(op).v, words);
}

}
