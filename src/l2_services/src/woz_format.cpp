// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/woz_format.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/span_cut.h"

namespace mister::svc::woz {
namespace {

using infra::affine::first;
using infra::affine::first_clamped;
using infra::affine::from;

constexpr proto::FileSize kChunkSizeAt{4};

constexpr proto::FileSize kInfoTypeAt{1};
constexpr proto::FileSize kInfoTypeNeeds{2};

constexpr proto::FileSize kInfoAt = kHeaderBytes;
constexpr proto::FileSize kInfoBytes{60};
constexpr proto::FileSize kTmapAt = kInfoAt + kChunkHeadBytes + kInfoBytes;

constexpr proto::FileSize kTmapBytes{160};
constexpr proto::FileSize kTrksAt = kTmapAt + kChunkHeadBytes + kTmapBytes;
constexpr proto::FileSize kDirectoryBytes = TrackSlot::kEnd.entry_at();
constexpr proto::FileSize kEntryBytes = TrackSlot::kEntryBytes;
static_assert(
    proto::kFileStart + kTrksAt + kChunkHeadBytes + kDirectoryBytes
        == kBlock.position_of(kFirstBitsBlock),
    "iigs_fmt.cpp:408-409: INFO, TMAP and TRKS's directory fill the three header blocks"
);

constexpr proto::FileSize kVersionAt{};
constexpr proto::FileSize kCreatorAt{5};
constexpr proto::FileSize kCreatorBytes{32};
constexpr proto::FileSize kSidesAt{37};
constexpr proto::FileSize kBitTimingAt{39};
constexpr proto::FileSize kLargestAt{44};
constexpr proto::FileSize kWord16{2};
constexpr proto::FileSize kWord32{4};

[[nodiscard]] std::span<std::byte> info_of(std::span<std::byte> file) noexcept {
    return first(from(file, kInfoAt + kChunkHeadBytes), kInfoBytes);
}

void put_byte(std::span<std::byte> field, proto::FileSize at, std::byte b) noexcept {
    from(field, at).front() = b;
}

void put_chunk_head(std::span<std::byte> at, const ChunkId& id, proto::FileSize size) noexcept {
    std::ranges::copy(id, at.begin());
    infra::store_le<std::uint32_t>(
        first(from(at, kChunkSizeAt), kWord32).first<4>(),
        size.file_word<std::uint32_t>()
    );
}

[[nodiscard]] bool opens_with(Bytes file, const ChunkId& id) noexcept {
    return proto::FileSize::of(file) >= proto::FileSize{id.size()}
    && std::ranges::equal(file.first(id.size()), id);
}

[[nodiscard]] proto::FileSize chunk_size_at(Bytes file, proto::FileSize at) noexcept {
    return proto::FileSize{
        infra::load_le<std::uint32_t>(first(from(file, at), kWord32).first<4>())
    };
}

template <class Hit>
[[nodiscard]] auto walk_chunks(Bytes file, Hit hit) noexcept -> decltype(hit(proto::FileSize{})) {
    const proto::FileSize end = proto::FileSize::of(file);
    for (proto::FileSize at = kHeaderBytes; at + kChunkHeadBytes <= end;) {
        if (auto found = hit(at))
            return found;
        at = at + kChunkHeadBytes + chunk_size_at(file, at + kChunkSizeAt);
    }
    return std::nullopt;
}

}

TrackEntry TrackEntry::load(std::span<const std::byte, 8> entry) noexcept {
    constexpr std::size_t kCountAt = 2;
    constexpr std::size_t kBitsAt = 4;
    return TrackEntry{
        .start = proto::Lba{infra::load_le<std::uint16_t>(entry.first<2>())},
        .blocks = proto::BlockCount{infra::load_le<std::uint16_t>(entry.subspan<kCountAt, 2>())},
        .bits = BitCount::load(entry.subspan<kBitsAt, 4>())
    };
}

bool TrackEntry::store(std::span<std::byte, 8> entry) const noexcept {
    if (!start.store_le16(entry.first<2>()) || !blocks.store_le16(entry.subspan<2, 2>()))
        return false;
    bits.store(entry.subspan<4, 4>());
    return true;
}

std::optional<DiskType> disk_type(Bytes file) noexcept {
    if (proto::FileSize::of(file) < kHeaderBytes)
        return std::nullopt;
    if (!opens_with(file, kWoz1) && !opens_with(file, kWoz2))
        return std::nullopt;

    const std::optional<Bytes> info = find_chunk(file, kInfo);
    if (!info || proto::FileSize::of(*info) < kInfoTypeNeeds)
        return std::nullopt;
    return DiskType{std::to_integer<std::uint8_t>((*info)[kInfoTypeAt.file_word<std::size_t>()])};
}

std::optional<Bytes> find_chunk(Bytes file, const ChunkId& id) noexcept {
    return walk_chunks(file, [file, &id](proto::FileSize at) -> std::optional<Bytes> {
        if (!opens_with(from(file, at), id))
            return std::nullopt;
        return from(file, at + kChunkHeadBytes);
    });
}

std::optional<TrackEntry> track_entry(Bytes trks, TrackSlot track) noexcept {
    const proto::FileSize at = track.entry_at();
    if (at + kEntryBytes > proto::FileSize::of(trks))
        return std::nullopt;
    return TrackEntry::load(first(from(trks, at), kEntryBytes).first<8>());
}

std::optional<TrackSlot> track_holding(Bytes file, proto::Lba lba) noexcept {
    const std::optional<Bytes> trks = find_chunk(file, kTrks);
    if (!trks)
        return std::nullopt;
    for (TrackSlot t{}; t < TrackSlot::kEnd; t = t.next()) {
        const std::optional<TrackEntry> e = track_entry(*trks, t);
        if (!e || e->blocks == proto::BlockCount{})
            continue;
        if (lba >= e->start && lba < e->start + e->blocks)
            return t;
    }
    return std::nullopt;
}

void put_header(std::span<std::byte> file, const InfoFields& fields) noexcept {
    constexpr std::array
        kMagicTail{std::byte{0xFF}, std::byte{0x0A}, std::byte{0x0D}, std::byte{0x0A}};
    constexpr std::byte kInfoVersion{2};
    std::ranges::copy(kMagicTail, std::ranges::copy(kWoz2, file.begin()).out);
    put_chunk_head(from(file, kInfoAt), kInfo, kInfoBytes);
    const std::span<std::byte> info = info_of(file);
    put_byte(info, kVersionAt, kInfoVersion);
    put_byte(info, kInfoTypeAt, info_byte(fields.type));
    const std::span<std::byte> creator = first(from(info, kCreatorAt), kCreatorBytes);
    std::ranges::fill(creator, std::byte{' '});
    std::ranges::copy(
        first_clamped(std::as_bytes(std::span{fields.creator}), kCreatorBytes),
        creator.begin()
    );
    put_byte(info, kSidesAt, fields.sides);
    put_byte(info, kBitTimingAt, fields.bit_timing);
    put_chunk_head(from(file, kTmapAt), kTmap, kTmapBytes);
    put_track_data_size(file, proto::BlockCount{});
}

std::span<std::byte> tmap_of(std::span<std::byte> file) noexcept {
    return first(from(file, kTmapAt + kChunkHeadBytes), kTmapBytes);
}

std::span<std::byte> trks_of(std::span<std::byte> file) noexcept {
    return from(file, kTrksAt + kChunkHeadBytes);
}

bool put_track_entry(std::span<std::byte> trks, TrackSlot track, const TrackEntry& entry) noexcept {
    return entry.store(first(from(trks, track.entry_at()), kEntryBytes).first<8>());
}

bool put_largest_track(std::span<std::byte> file, proto::BlockCount blocks) noexcept {
    return blocks.store_le16(first(from(info_of(file), kLargestAt), kWord16).first<2>());
}

void put_track_data_size(std::span<std::byte> file, proto::BlockCount bits) noexcept {
    put_chunk_head(from(file, kTrksAt), kTrks, kDirectoryBytes + kBlock.bytes_of(bits));
}

Crc32 crc32(Bytes bytes) noexcept {
    return crc32_extend(Crc32{}, infra::byte_cast<const std::uint8_t>(bytes));
}

void seal(std::span<std::byte> file) noexcept {
    constexpr proto::FileSize kCrcAt{8};
    crc32(from(std::span<const std::byte>{file}, kHeaderBytes))
        .store_le32(first(from(file, kCrcAt), kWord32).first<4>());
}

}
