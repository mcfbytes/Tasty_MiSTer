// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/rec_control.h"
#include "infra/endian.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

enum class ChunkKind : std::uint8_t { Open, Data, Close, Refuse };

struct ChunkSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::size_t kMaxChunks = 4096;

    static constexpr std::uint32_t kKey = 0x8000'0000u;

    [[nodiscard]] static std::uint32_t desc_word(
        std::span<const std::byte> table,
        std::size_t i
    ) noexcept {
        return infra::load_le<std::uint32_t>(table.subspan(i * 4u).first<4>());
    }

    static void set_desc_word(std::span<std::byte> table, std::size_t i, std::uint32_t w) noexcept {
        infra::store_le<std::uint32_t>(table.subspan(i * 4u).first<4>(), w);
    }

    ChunkKind kind = ChunkKind::Data;
    std::uint8_t scale = 1;
    RecCodec codec = RecCodec::Cscd;
    std::uint8_t pad_ = 0;
    std::uint16_t gen = 0;
    std::uint16_t segment = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t frame_mul = 1;
    std::uint32_t vtime = 0;
    std::uint32_t used = 0;
    std::uint32_t chunks = 0;
    std::span<const std::byte> bytes{};
    std::span<const std::byte> desc{};
    RecPath path{};
};

inline constexpr std::size_t kChunkSlots = 32;

inline constexpr std::uint32_t kChunksPerSegment = 65536;
using ChunkChannel = xthread::
    LoanChannel<ChunkSlot, kChunkSlots, SeatTag::Encode, SeatTag::RecWrite>;

}
