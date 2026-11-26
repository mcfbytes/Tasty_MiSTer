// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

#include "cores/minimig/io_frame_sink_op.h"
#include "cores/minimig/minimig_ladder_bus_addr.h"
#include "cores/minimig/minimig_ladder_bus_bytes.h"
#include "cores/wire_chunk.h"
#include "hal/link_halves.h"
#include "hal/types.h"
#include "infra/seat.h"

namespace mister::cores::minimig {

class FrameQueue {
    TASTY_SEAT_EXEMPT(component);

public:
    struct Run {
        std::vector<std::uint8_t> bytes;
        WireChunk frame{};
    };

    static constexpr std::size_t kBlockBytes = 512;

    static constexpr std::size_t kBlockFrameWords = 1 + 4 + kBlockBytes;

    void reg(FrameOp op, hal::LinkWord parm);

    void mem(BusAddr at, std::span<const std::byte> data);

    void mem_blocks(BusAddr at, std::span<const std::byte> data);

    void mem_zeros(BusAddr at, BusBytes len);

    void mem_words(BusAddr at, std::span<const std::uint16_t> words);

    void discard_residents();

    void ext_cfg(hal::LinkHalves halves);

    [[nodiscard]] bool empty() const noexcept { return runs_.empty(); }

    [[nodiscard]] Run take();

private:
    void push_(std::span<const hal::LinkWord> frame);

    std::deque<Run> runs_{};
    std::vector<hal::LinkWord> scratch_{};
};

static_assert(FrameQueue::kBlockFrameWords == 517, "an F0 block is 517 words on the wire");

}
