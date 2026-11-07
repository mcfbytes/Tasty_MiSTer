// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/uef.h"

#include <algorithm>

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

}

bool valid_header(std::span<const std::uint8_t> image) noexcept {
    if (image.size() < kHeaderBytes)
        return false;
    static constexpr std::array<std::uint8_t, 10>
        kTag{'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', '\0'};
    return std::equal(kTag.begin(), kTag.end(), image.begin());
}

const ChunkInfo* locate_chunk(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    std::uint32_t& bit_pos
) noexcept {
    if (cache.bit_offset_start <= bit_pos && bit_pos < cache.bit_offset_end) {
        bit_pos -= cache.bit_offset_start;
        return &cache;
    }

    std::size_t pos;
    std::uint32_t chunk_start = 0;
    if (cache.bit_offset_end != 0 && bit_pos >= cache.bit_offset_end) {
        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
        chunk_start = cache.bit_offset_end;
        bit_pos -= chunk_start;
    } else {
        pos = kHeaderBytes;
    }
    cache.bit_offset_end = 0;

    for (;;) {
        if (pos + kChunkHeaderBytes > image.size())
            break;
        const ChunkId id = chunk_id(read_u16(image, pos));

        if (id == kShortReadId)
            break;
        const std::uint32_t length = read_u32(image, pos + 2);
        pos += kChunkHeaderBytes;
        cache.id = id;
        cache.length = length;
        cache.file_offset = static_cast<std::uint32_t>(pos);

        if (id == kTapeId || id == kGapId || id == kHighToneId || id == kHighDummyId) {
            std::uint32_t chunk_bitlen = 0;
            if (id == kTapeId) {
                chunk_bitlen = length * kBitsPerTapeByte;
            } else if (id == kGapId || id == kHighToneId) {
                if (pos + 2 > image.size())
                    break;
                chunk_bitlen = ms_to_bits(read_u16(image, pos));
            } else {
                if (pos + 4 > image.size())
                    break;
                cache.pre_carrier = ms_to_bits(read_u16(image, pos));
                const std::uint32_t post_carrier = ms_to_bits(read_u16(image, pos + 2));
                chunk_bitlen = cache.pre_carrier + kHighDummyCarrierBits + post_carrier;
            }

            if (bit_pos < chunk_bitlen) {
                cache.bit_offset_start = chunk_start;
                cache.bit_offset_end = chunk_start + chunk_bitlen;
                break;
            }
            bit_pos -= chunk_bitlen;
            chunk_start += chunk_bitlen;
        }

        pos = static_cast<std::size_t>(cache.file_offset) + cache.length;
    }

    return cache.bit_offset_end != 0 ? &cache : nullptr;
}

std::uint8_t bit_at(
    std::span<const std::uint8_t> image,
    ChunkInfo& cache,
    std::uint32_t bit_pos
) noexcept {
    const ChunkInfo* info = locate_chunk(image, cache, bit_pos);
    if (info == nullptr)
        return 0;

    if (info->id == kGapId)
        return 0;
    if (info->id == kHighToneId)
        return 1;

    if (info->id == kTapeId) {
        const std::uint32_t slot = tape_slot_at(bit_pos);
        if (slot == kStartSlot)
            return kStartBit;
        if (slot == kStopSlot)
            return kStopBit;
        const std::size_t byte_pos = static_cast<std::size_t>(info->file_offset)
            + tape_byte_at(bit_pos);

        const std::uint8_t byte = byte_pos < image.size() ? image[byte_pos] : std::uint8_t{0};
        const std::uint32_t shift = slot - 1;
        return (byte & (1u << shift)) ? 1 : 0;
    }

    if (bit_pos < info->pre_carrier || bit_pos >= info->pre_carrier + kHighDummyCarrierBits)
        return 1;
    const std::uint32_t slot = tape_slot_at(bit_pos - info->pre_carrier);
    if (slot == kStartSlot)
        return kStartBit;
    if (slot == kStopSlot)
        return kStopBit;
    const std::uint32_t shift = slot - 1;
    return (kHighDummyByte & (1u << shift)) ? 1 : 0;
}

std::uint32_t total_bits(std::span<const std::uint8_t> image) noexcept {
    ChunkInfo cache{};
    std::uint32_t bit_pos = 0xFFFFFFFFu;
    (void)locate_chunk(image, cache, bit_pos);
    return ~bit_pos;
}

}
