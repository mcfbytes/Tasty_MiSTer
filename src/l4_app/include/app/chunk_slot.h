// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "app/rec_control.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

enum class ChunkKind : std::uint8_t { Open, Data, Close };

struct ChunkSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::size_t kMaxChunks = 4096;

    static constexpr std::uint32_t kKey = 0x8000'0000u;

    ChunkKind kind = ChunkKind::Data;
    std::uint8_t scale = 1;
    std::uint16_t gen = 0;
    std::uint16_t segment = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t pad_ = 0;
    std::uint32_t vtime = 0;
    std::uint32_t used = 0;
    std::uint32_t chunks = 0;
    const std::byte* bytes = nullptr;
    const std::uint32_t* desc = nullptr;
    RecPath path{};
};

inline constexpr std::size_t kChunkSlots = 8;

inline constexpr std::uint32_t kChunksPerSegment = 65536;
using ChunkChannel =
    xthread::LoanChannel<ChunkSlot, kChunkSlots, SeatTag::Encode, SeatTag::RecWrite>;

}  // namespace mister::app
