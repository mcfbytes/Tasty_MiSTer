// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "proto/types.h"
#include "svc/file.h"

namespace mister::cores::uef {

inline constexpr std::size_t kHeaderBytes = 12;

inline constexpr std::size_t kChunkHeaderBytes = 6;

enum class ChunkId : std::uint16_t {
    Info = 0x0000,
    Tape = 0x0100,
    HighTone = 0x0110,
    HighDummy = 0x0111,
    Gap = 0x0112,
    FreqChg = 0x0113,
    Security = 0x0114,
    FloatGap = 0x0116,
    ShortRead = 0xFFFF,
};

[[nodiscard]] constexpr ChunkId chunk_id(std::uint16_t raw) noexcept {
    return static_cast<ChunkId>(raw);
}

inline constexpr std::uint32_t kBitsPerTapeByte = 10;
inline constexpr std::uint32_t kStartSlot = 0;
inline constexpr std::uint32_t kStopSlot = 9;

inline constexpr std::uint32_t kHighDummyCarrierBits = 20;

inline constexpr double kBaud = 1000000.0 / (16.0 * 52.0);

[[nodiscard]] constexpr std::uint32_t ms_to_bits(std::uint16_t ms) noexcept {
    return static_cast<std::uint32_t>(static_cast<double>(ms) * (kBaud / 1000.0));
}

inline constexpr std::uint8_t kStartBit = 0;
inline constexpr std::uint8_t kStopBit = 1;

inline constexpr std::uint8_t kHighDummyByte = 'A';

[[nodiscard]] bool valid_header(std::span<const std::uint8_t> image) noexcept;

struct ChunkInfo;

struct TapeBit;
struct TapeBitCount;
struct ChunkBit;

struct ChunkInfo {
    ChunkId id{};
    std::uint32_t length = 0;
    std::uint32_t file_offset = 0;
    std::uint32_t bit_offset_start = 0;
    std::uint32_t bit_offset_end = 0;
    std::uint32_t pre_carrier = 0;

    constexpr void record(TapeBit start, TapeBitCount length) noexcept;
};

struct TapeBitCount {
    std::uint32_t v = 0;
    friend constexpr bool operator==(TapeBitCount, TapeBitCount) = default;
    friend constexpr auto operator<=>(TapeBitCount, TapeBitCount) = default;
};

struct TapeBit {
    std::uint32_t v = 0;
    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

    constexpr TapeBit& advance(TapeBitCount n) noexcept {
        v += n.v;
        return *this;
    }

    constexpr TapeBit& rewind(TapeBitCount n) noexcept {
        v -= n.v;
        return *this;
    }

    constexpr TapeBit& rebase_on(TapeBit origin) noexcept {
        v -= origin.v;
        return *this;
    }

    [[nodiscard]] constexpr bool within(TapeBit origin, TapeBitCount n) const noexcept {
        return origin.v <= v && v - origin.v < n.v;
    }

    [[nodiscard]] constexpr bool before(TapeBit end) const noexcept { return v < end.v; }

    [[nodiscard]] constexpr bool at_or_past(TapeBit end) const noexcept { return v >= end.v; }

    [[nodiscard]] constexpr bool before_count(TapeBitCount n) const noexcept { return v < n.v; }

    [[nodiscard]] constexpr ChunkBit offset_from(TapeBit origin) const noexcept;

    [[nodiscard]] constexpr ChunkBit chunk_offset() const noexcept;

    template <std::integral Word = std::uint32_t>
    [[nodiscard]] constexpr Word sizing_result() const noexcept {
        return static_cast<Word>(~v);
    }

    friend constexpr bool operator==(TapeBit, TapeBit) = default;
    friend constexpr auto operator<=>(TapeBit, TapeBit) = default;
};

struct ChunkBit {
    std::uint32_t v = 0;
    friend constexpr bool operator==(ChunkBit, ChunkBit) = default;

    [[nodiscard]] bool tape_level_high(std::span<const std::uint8_t> tape) const noexcept;

    [[nodiscard]] bool carrier_level_high(ChunkBit carrier) const noexcept;

private:
    [[nodiscard]] std::uint32_t frame_slot() const noexcept;

    [[nodiscard]] std::size_t data_byte() const noexcept;
};

constexpr void ChunkInfo::record(TapeBit start, TapeBitCount count) noexcept {
    bit_offset_start = start.v;
    bit_offset_end = start.v + count.v;
}

constexpr ChunkBit TapeBit::offset_from(TapeBit origin) const noexcept {
    return ChunkBit{v - origin.v};
}

constexpr ChunkBit TapeBit::chunk_offset() const noexcept {
    return ChunkBit{v};
}

struct Located {
    const ChunkInfo& chunk;
    ChunkBit offset_in_chunk{};
};

struct TapeBits {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr proto::FileSize bytes() const noexcept {
        constexpr std::uint32_t kByteBits = std::numeric_limits<std::uint8_t>::digits;
        return proto::FileSize{(v + kByteBits - 1u) / kByteBits};
    }
};

inline constexpr proto::FileSize kMinImageBytes{2};

inline constexpr proto::FileSize kMaxImageBytes{16u * 1024u * 1024u};

[[nodiscard]] TapeBits sized(std::span<const std::uint8_t> image) noexcept;

[[nodiscard]] std::unique_ptr<svc::IFile> make_tape_file(std::vector<std::uint8_t>&& image);

}
