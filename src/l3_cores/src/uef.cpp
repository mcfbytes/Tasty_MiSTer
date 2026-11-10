// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/uef.h"

#include <algorithm>
#include <optional>

#include "infra/endian.h"

namespace mister::cores::uef {
namespace {

[[nodiscard]] std::uint16_t read_u16(
    std::span<const std::uint8_t> image,
    std::size_t pos
) noexcept {
    return infra::load_le<std::uint16_t>(image.subspan(pos).first<2>());
}

[[nodiscard]] std::uint32_t read_u32(
    std::span<const std::uint8_t> image,
    std::size_t pos
) noexcept {
    return infra::load_le<std::uint32_t>(image.subspan(pos).first<4>());
}

[[nodiscard]] constexpr std::uint32_t tape_byte_at(std::uint32_t bit_pos) noexcept {
    return bit_pos / kBitsPerTapeByte;
}

[[nodiscard]] constexpr std::uint32_t tape_slot_at(std::uint32_t bit_pos) noexcept {
    return bit_pos % kBitsPerTapeByte;
}

struct Walk {
    std::optional<Located> located;
    TapeBit remaining_bits{};
};

[[nodiscard]] Walk walk_chunks(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    TapeBit from
) noexcept {
    TapeBit bit_pos = from;
    if (bit_pos.within(
            TapeBit{cache.bit_offset_start},
            TapeBitCount{cache.bit_offset_end - cache.bit_offset_start}
        )) {
        return Walk{Located{cache, bit_pos.offset_from(TapeBit{cache.bit_offset_start})}, from};
    }

    std::size_t pos;
    TapeBit chunk_start{};
    if (cache.bit_offset_end != 0 && bit_pos.at_or_past(TapeBit{cache.bit_offset_end})) {
        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
        chunk_start = TapeBit{cache.bit_offset_end};
        bit_pos.rebase_on(chunk_start);
    } else {
        pos = kHeaderBytes;
    }
    cache.bit_offset_end = 0;

    for (;;) {
        if (pos + kChunkHeaderBytes > image.size())
            break;
        const ChunkId id = chunk_id(read_u16(image, pos));

        if (id == ChunkId::ShortRead)
            break;
        const std::uint32_t length = read_u32(image, pos + 2);
        pos += kChunkHeaderBytes;
        cache.id = id;
        cache.length = length;
        cache.file_offset = static_cast<std::uint32_t>(pos);

        if (id == ChunkId::Tape
            || id == ChunkId::Gap
            || id == ChunkId::HighTone
            || id == ChunkId::HighDummy) {
            TapeBitCount chunk_bitlen{};
            if (id == ChunkId::Tape) {
                chunk_bitlen = TapeBitCount{length * kBitsPerTapeByte};
            } else if (id == ChunkId::Gap || id == ChunkId::HighTone) {
                if (pos + 2 > image.size())
                    break;
                chunk_bitlen = TapeBitCount{ms_to_bits(read_u16(image, pos))};
            } else {
                if (pos + 4 > image.size())
                    break;
                cache.pre_carrier = ms_to_bits(read_u16(image, pos));
                const std::uint32_t post_carrier = ms_to_bits(read_u16(image, pos + 2));
                chunk_bitlen = TapeBitCount{
                    cache.pre_carrier + kHighDummyCarrierBits + post_carrier
                };
            }

            if (bit_pos.before_count(chunk_bitlen)) {
                cache.record(chunk_start, chunk_bitlen);
                return Walk{Located{cache, bit_pos.chunk_offset()}, bit_pos};
            }
            bit_pos.rewind(chunk_bitlen);
            chunk_start.advance(chunk_bitlen);
        }

        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
    }

    return Walk{std::nullopt, bit_pos};
}

}

bool ChunkBit::tape_level_high(std::span<const std::uint8_t> tape) const noexcept {
    const std::uint32_t slot = tape_slot_at(v);
    if (slot == kStartSlot)
        return false;
    if (slot == kStopSlot)
        return true;
    const std::size_t byte_at = tape_byte_at(v);

    const std::uint8_t byte = byte_at < tape.size() ? tape[byte_at] : std::uint8_t{0};
    const std::uint32_t shift = slot - 1u;
    return (byte & (1u << shift)) != 0;
}

bool ChunkBit::carrier_level_high(ChunkBit carrier) const noexcept {
    if (v < carrier.v || v >= carrier.v + kHighDummyCarrierBits)
        return true;
    const std::uint32_t slot = tape_slot_at(v - carrier.v);
    if (slot == kStartSlot)
        return false;
    if (slot == kStopSlot)
        return true;
    const std::uint32_t shift = slot - 1u;
    return (kHighDummyByte & (1u << shift)) != 0;
}

bool valid_header(std::span<const std::uint8_t> image) noexcept {
    if (image.size() < kHeaderBytes)
        return false;
    static constexpr std::array<std::uint8_t, 10>
        kTag{'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', '\0'};
    return std::equal(kTag.begin(), kTag.end(), image.begin());
}

std::optional<Located> locate_chunk(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    TapeBit bit_pos
) noexcept {
    return walk_chunks(image, cache, bit_pos).located;
}

std::uint8_t bit_at(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    TapeBit bit_pos
) noexcept {
    const std::optional<Located> located = locate_chunk(image, cache, bit_pos);
    if (!located)
        return 0;
    const ChunkInfo& info = located->chunk;
    const ChunkBit pos_in_chunk = located->offset_in_chunk;

    if (info.id == ChunkId::Gap)
        return 0;
    if (info.id == ChunkId::HighTone)
        return 1;

    if (info.id == ChunkId::Tape) {
        const std::size_t data_at = static_cast<std::size_t>(info.file_offset);
        return pos_in_chunk.tape_level_high(image.subspan(data_at)) ? kStopBit : kStartBit;
    }

    return pos_in_chunk.carrier_level_high(ChunkBit{info.pre_carrier}) ? kStopBit : kStartBit;
}

std::uint32_t total_bits(std::span<const std::uint8_t> image) noexcept {
    ChunkInfo cache{};
    return walk_chunks(image, cache, TapeBit{TapeBit::kNone}).remaining_bits.sizing_result();
}

}
