// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>

#include "app/frame_ticks.h"
#include "app/picture_size.h"
#include "app/rec_every.h"
#include "app/rec_options.h"
#include "app/recording_name.h"
#include "app/segment_index.h"
#include "app/types.h"
#include "infra/endian.h"
#include "infra/loan_channel.h"
#include "infra/message_sum.h"
#include "infra/seat.h"

namespace mister::app {

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

    enum class Kind : std::uint8_t { Open, Data, Close, Refuse, kCount };

    using Head = ControlGeneration;
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

    struct Open {
        static constexpr Kind kKind = Kind::Open;
        FrameTicks rate{};
        PictureSize size{};
        SegmentIndex segment{};
        RecEvery frame_mul{};
        PictureScale scale = PictureScale::Native;
        RecCodec codec = RecCodec::Cscd;
        std::array<std::byte, 2> pad_{};
        RecPath path{};

        std::array<std::byte, 4> pad_tail_{};
    };

    struct Data {
        TASTY_SEAT_EXEMPT(component);
        static constexpr Kind kKind = Kind::Data;
        std::span<const std::byte> bytes{};
        std::span<const std::byte> desc{};
        FrameTicks rate{};
        std::uint32_t used = 0;
        std::uint32_t chunks = 0;
#if __SIZEOF_POINTER__ == 8
        std::array<std::byte, 4> pad_lp64_{};
#endif
    };

    struct Close {
        static constexpr Kind kKind = Kind::Close;
    };

    struct Refuse {
        static constexpr Kind kKind = Kind::Refuse;
    };

    using Alternatives = std::tuple<Open, Data, Close, Refuse>;

    Kind kind = Kind::Open;
    std::array<std::byte, 1> pad_{};
    Head head{};
    std::array<std::byte, 4> pad2_{};

    enum Extent : std::size_t { kStore = sizeof(Open) };

    alignas(std::uint64_t) std::array<std::byte, kStore> store{};
};

static_assert(
    infra::MessageSum<ChunkSlot>
    && infra::HasHead<ChunkSlot>
    && infra::alternatives_are_total<ChunkSlot>()
);
static_assert(sizeof(ChunkSlot::Open) == 280);
static_assert(sizeof(ChunkSlot) == 288);
static_assert(std::is_trivially_copyable_v<ChunkSlot>);

inline constexpr std::size_t kChunkSlots = 32;

inline constexpr std::uint32_t kChunksPerSegment = 65536;
using ChunkChannel = xthread::
    LoanChannel<ChunkSlot, kChunkSlots, SeatTag::Encode, SeatTag::RecWrite>;

}
