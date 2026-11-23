// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/indexed_array.h"
#include "proto/types.h"
#include "svc/nib_track.h"
#include "svc/types.h"

namespace mister::svc::woz {

using Bytes = std::span<const std::byte>;

using infra::affine::operator+;
using infra::affine::operator-;

enum class DiskType : std::uint8_t { Unset = 0, Inch525 = 1, Inch35 = 2 };

using ChunkId = std::array<std::byte, 4>;
inline constexpr ChunkId kInfo{std::byte{'I'}, std::byte{'N'}, std::byte{'F'}, std::byte{'O'}};
inline constexpr ChunkId kTmap{std::byte{'T'}, std::byte{'M'}, std::byte{'A'}, std::byte{'P'}};
inline constexpr ChunkId kTrks{std::byte{'T'}, std::byte{'R'}, std::byte{'K'}, std::byte{'S'}};
inline constexpr ChunkId kWoz1{std::byte{'W'}, std::byte{'O'}, std::byte{'Z'}, std::byte{'1'}};
inline constexpr ChunkId kWoz2{std::byte{'W'}, std::byte{'O'}, std::byte{'Z'}, std::byte{'2'}};

inline constexpr proto::FileSize kHeaderBytes{12};
inline constexpr proto::FileSize kChunkHeadBytes{8};
inline constexpr proto::BlockSize kBlock{512};

struct TrackSlot {
    std::uint8_t v = 0;
    static const TrackSlot kEnd;
    static constexpr proto::FileSize kEntryBytes{8};

    [[nodiscard]] constexpr TrackSlot next() const noexcept {
        return TrackSlot{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr proto::FileSize entry_at() const noexcept {
        return kEntryBytes * std::uint64_t{v};
    }

    [[nodiscard]] constexpr std::optional<NibTrack> nib_track() const noexcept {
        return NibTrack::holding(proto::kFileStart + kNibTrackBytes * std::uint64_t{v});
    }

    void map_whole_track(std::span<std::byte> tmap) const noexcept {
        constexpr std::size_t kQuarters = 4;
        const std::size_t centre = std::size_t{v} * kQuarters;
        for (std::size_t q = centre == 0 ? 0 : centre - 1; q <= centre + 1 && q < tmap.size(); ++q)
            tmap[q] = std::byte{v};
    }

    friend constexpr bool operator==(TrackSlot, TrackSlot) = default;
    friend constexpr auto operator<=>(TrackSlot, TrackSlot) = default;
};

inline constexpr TrackSlot TrackSlot::kEnd{160};

struct BitCount {
    std::uint32_t v = 0;
    using affine_count = BitCount;

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr proto::FileSize bytes_covering() const noexcept {
        return proto::FileSize{(std::uint64_t{v} + CHAR_BIT - 1) / CHAR_BIT};
    }

    [[nodiscard]] constexpr infra::ByteOffset byte_at() const noexcept {
        return infra::ByteOffset{v / CHAR_BIT};
    }

    [[nodiscard]] constexpr std::byte mask() const noexcept {
        return std::byte{1} << (CHAR_BIT - 1 - v % CHAR_BIT);
    }

    [[nodiscard]] constexpr bool bit_in(std::span<const std::byte> bytes) const noexcept {
        if (!infra::affine::in_bounds(infra::ByteCount{bytes.size()}, byte_at()))
            return false;
        return (infra::at_index(bytes, byte_at()) & mask()) != std::byte{};
    }

    [[nodiscard]] static constexpr BitCount load(std::span<const std::byte, 4> bytes) noexcept {
        return BitCount{infra::load_le<std::uint32_t>(bytes)};
    }

    void store(std::span<std::byte, 4> bytes) const noexcept {
        infra::store_le<std::uint32_t>(bytes, v);
    }

    friend constexpr bool operator==(BitCount, BitCount) = default;
    friend constexpr auto operator<=>(BitCount, BitCount) = default;
};

struct TrackEntry {
    proto::Lba start{};
    proto::BlockCount blocks{};
    BitCount bits{};

    [[nodiscard]] static TrackEntry load(std::span<const std::byte, 8> entry) noexcept;

    [[nodiscard]] bool store(std::span<std::byte, 8> entry) const noexcept;
};

[[nodiscard]] constexpr std::byte info_byte(DiskType type) noexcept {
    return std::byte{static_cast<std::uint8_t>(type)};
}

[[nodiscard]] std::optional<DiskType> disk_type(Bytes file) noexcept;

[[nodiscard]] std::optional<Bytes> find_chunk(Bytes file, const ChunkId& id) noexcept;

[[nodiscard]] std::optional<TrackEntry> track_entry(Bytes trks, TrackSlot track) noexcept;

[[nodiscard]] std::optional<TrackSlot> track_holding(Bytes file, proto::Lba lba) noexcept;

[[nodiscard]] Crc32 crc32(Bytes bytes) noexcept;

inline constexpr proto::Lba kFirstBitsBlock{3};

inline constexpr std::byte kNoTrack{0xFF};

struct InfoFields {
    DiskType type{};
    std::string_view creator{};
    std::byte sides{};
    std::byte bit_timing{};
};

void put_header(std::span<std::byte> file, const InfoFields& info) noexcept;

[[nodiscard]] std::span<std::byte> tmap_of(std::span<std::byte> file) noexcept;
[[nodiscard]] std::span<std::byte> trks_of(std::span<std::byte> file) noexcept;

[[nodiscard]] bool put_track_entry(
    std::span<std::byte> trks,
    TrackSlot track,
    const TrackEntry& entry
) noexcept;

[[nodiscard]] bool put_largest_track(std::span<std::byte> file, proto::BlockCount blocks) noexcept;

void put_track_data_size(std::span<std::byte> file, proto::BlockCount bits) noexcept;

void seal(std::span<std::byte> file) noexcept;

}
