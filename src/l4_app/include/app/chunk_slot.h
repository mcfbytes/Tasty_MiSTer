// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "app/picture_size.h"
#include "app/rec_control.h"
#include "app/types.h"
#include "infra/endian.h"
#include "infra/loan_channel.h"
#include "infra/seat.h"

namespace mister::app {

enum class ChunkKind : std::uint8_t { Open, Data, Close, Refuse };

struct ChunkDesc {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr ChunkDesc of(std::uint32_t payload, bool key) noexcept {
        return ChunkDesc{payload | (key ? kKey : 0u)};
    }

    [[nodiscard]] constexpr std::uint32_t payload() const noexcept { return v & ~kKey; }

    [[nodiscard]] constexpr bool key() const noexcept { return (v & kKey) != 0; }

    [[nodiscard]] static constexpr ChunkDesc load(
        std::span<const std::byte, sizeof(std::uint32_t)> word
    ) noexcept {
        return ChunkDesc{infra::load_le<std::uint32_t>(word)};
    }

    constexpr void store(std::span<std::byte, sizeof(std::uint32_t)> word) const noexcept {
        infra::store_le<std::uint32_t>(word, v);
    }

    friend constexpr bool operator==(ChunkDesc, ChunkDesc) = default;

private:
    static constexpr std::uint32_t kKey = 0x8000'0000u;
};

struct ChunkSlot {
    TASTY_SEAT_MEDIATOR(Any, Any);

    static constexpr std::size_t kMaxChunks = 4096;

    [[nodiscard]] static ChunkDesc desc_word(
        std::span<const std::byte> table,
        std::size_t i
    ) noexcept {
        constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
        return ChunkDesc::load(table.subspan(i * kWordBytes).first<kWordBytes>());
    }

    static void set_desc_word(std::span<std::byte> table, std::size_t i, ChunkDesc w) noexcept {
        constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
        w.store(table.subspan(i * kWordBytes).first<kWordBytes>());
    }

    ChunkKind kind = ChunkKind::Data;

    PictureScale scale = PictureScale::Native;
    RecCodec codec = RecCodec::Cscd;
    std::uint8_t pad_ = 0;
    ControlGeneration gen{};
    std::uint16_t segment = 0;
    PictureSize size{};
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
