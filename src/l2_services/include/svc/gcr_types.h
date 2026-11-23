// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::svc::gcr {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator/;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

enum TableEntry : std::size_t { kTableEntryBytes = 4 };

inline constexpr proto::FileOffset kTablesAt{12};

enum TrackExtent : std::size_t {
    kLongestTrackBytes = 52 * 256,
    kWorkBytes = 2 * kLongestTrackBytes,

    kWorkBufferBytes = kWorkBytes + 512,
};

struct FullTrack {
    std::uint8_t v = 0;

    static const FullTrack kSecondSide;

    [[nodiscard]] constexpr FullTrack next() const noexcept {
        return FullTrack{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr bool second_side() const noexcept;
    [[nodiscard]] constexpr FullTrack on_side() const noexcept;
    friend constexpr bool operator==(FullTrack, FullTrack) = default;
    friend constexpr auto operator<=>(FullTrack, FullTrack) = default;
};

inline constexpr FullTrack FullTrack::kSecondSide{42};

constexpr bool FullTrack::second_side() const noexcept {
    return *this >= kSecondSide;
}

constexpr FullTrack FullTrack::on_side() const noexcept {
    return second_side() ? FullTrack{static_cast<std::uint8_t>(v - kSecondSide.v)} : *this;
}

struct TrackTotal;

struct HalfTrack {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr FullTrack full() const noexcept {
        return FullTrack{static_cast<std::uint8_t>(v >> 1)};
    }

    [[nodiscard]] constexpr bool between_tracks() const noexcept { return (v & 1u) != 0; }

    [[nodiscard]] constexpr HalfTrack next() const noexcept {
        return HalfTrack{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr infra::ByteCount table_bytes_before() const noexcept {
        return infra::ByteCount{v * kTableEntryBytes};
    }

    [[nodiscard]] constexpr proto::FileOffset position_entry_at() const noexcept {
        return kTablesAt + proto::FileSize::of(table_bytes_before());
    }

    [[nodiscard]] constexpr proto::FileOffset speed_entry_at(TrackTotal slots) const noexcept;

    [[nodiscard]] constexpr std::span<std::byte, kTableEntryBytes> entry_in(
        std::span<std::byte> table
    ) const noexcept {
        return infra::affine::from(table, infra::ByteOffset{} + table_bytes_before())
            .first<kTableEntryBytes>();
    }

    [[nodiscard]] constexpr std::span<const std::byte, kTableEntryBytes> entry_in(
        std::span<const std::byte> table
    ) const noexcept {
        return infra::affine::from(table, infra::ByteOffset{} + table_bytes_before())
            .first<kTableEntryBytes>();
    }

    friend constexpr bool operator==(HalfTrack, HalfTrack) = default;
    friend constexpr auto operator<=>(HalfTrack, HalfTrack) = default;
};

struct TrackTotal {
    std::uint8_t v = 0;

    static const TrackTotal kOneSide;
    static const TrackTotal kSlots1541;
    static const TrackTotal kSlots1571;

    [[nodiscard]] static constexpr TrackTotal of(std::byte b) noexcept {
        return TrackTotal{std::to_integer<std::uint8_t>(b)};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return static_cast<std::byte>(v); }

    [[nodiscard]] constexpr bool holds(HalfTrack at) const noexcept { return at.v < v; }

    [[nodiscard]] constexpr bool holds(FullTrack at) const noexcept { return at.v < v; }

    [[nodiscard]] constexpr HalfTrack end() const noexcept { return HalfTrack{v}; }

    [[nodiscard]] constexpr infra::ByteCount table_bytes() const noexcept {
        return end().table_bytes_before();
    }

    [[nodiscard]] constexpr TrackTotal first_side() const noexcept {
        constexpr std::uint8_t kSides = 2;
        return TrackTotal{static_cast<std::uint8_t>(v / kSides)};
    }

    [[nodiscard]] constexpr TrackTotal doubled() const noexcept {
        constexpr std::uint8_t kSides = 2;
        return TrackTotal{static_cast<std::uint8_t>(v * kSides)};
    }

    [[nodiscard]] constexpr proto::FileSize tables_bytes() const noexcept {
        return proto::FileSize::of(table_bytes() + table_bytes());
    }

    [[nodiscard]] constexpr proto::FileSize header_bytes() const noexcept {
        return tables_bytes() + (kTablesAt - proto::kFileStart);
    }

    friend constexpr bool operator==(TrackTotal, TrackTotal) = default;
    friend constexpr auto operator<=>(TrackTotal, TrackTotal) = default;
};

inline constexpr TrackTotal TrackTotal::kOneSide{42};
inline constexpr TrackTotal TrackTotal::kSlots1541{84};
inline constexpr TrackTotal TrackTotal::kSlots1571{168};

constexpr proto::FileOffset HalfTrack::speed_entry_at(TrackTotal slots) const noexcept {
    return position_entry_at() + proto::FileSize::of(slots.table_bytes());
}

struct TrackSpace;

struct ImagePos {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr ImagePos load(
        std::span<const std::byte, kTableEntryBytes> entry
    ) noexcept {
        return ImagePos{infra::load_le<std::uint32_t>(entry)};
    }

    constexpr void store(std::span<std::byte, kTableEntryBytes> entry) const noexcept {
        infra::store_le<std::uint32_t>(entry, v);
    }

    [[nodiscard]] constexpr TrackSpace space_to(ImagePos end) const noexcept;

    [[nodiscard]] static constexpr ImagePos end_of(proto::FileSize file) noexcept {
        return ImagePos{file.low_word()};
    }

    [[nodiscard]] constexpr proto::FileOffset offset() const noexcept {
        return proto::FileOffset{v};
    }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr ImagePos after(infra::ByteCount bytes) const noexcept {
        return ImagePos{v + static_cast<std::uint32_t>(bytes.v)};
    }

    friend constexpr bool operator==(ImagePos, ImagePos) = default;
    friend constexpr auto operator<=>(ImagePos, ImagePos) = default;
};

struct SpeedZone {
    std::uint32_t v = 0;
    static const SpeedZone kMfm;

    [[nodiscard]] static constexpr SpeedZone load(
        std::span<const std::byte, kTableEntryBytes> entry
    ) noexcept {
        return SpeedZone{infra::load_le<std::uint32_t>(entry)};
    }

    constexpr void store(std::span<std::byte, kTableEntryBytes> entry) const noexcept {
        infra::store_le<std::uint32_t>(entry, v);
    }

    [[nodiscard]] constexpr ImagePos as_position() const noexcept { return ImagePos{v}; }

    friend constexpr bool operator==(SpeedZone, SpeedZone) = default;
};

struct TrackLength {
    static constexpr std::size_t kPrefixBytes = 2;
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr TrackLength load(
        std::span<const std::byte, kPrefixBytes> prefix
    ) noexcept {
        return TrackLength{infra::load_le<std::uint16_t>(prefix)};
    }

    constexpr void store(std::span<std::byte, kPrefixBytes> prefix) const noexcept {
        infra::store_le<std::uint16_t>(prefix, v);
    }

    [[nodiscard]] static constexpr TrackLength of(infra::ByteCount bytes) noexcept {
        return TrackLength{static_cast<std::uint16_t>(bytes.v)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr infra::ByteCount stream() const noexcept { return infra::ByteCount{v}; }

    [[nodiscard]] constexpr infra::ByteCount stored() const noexcept {
        return infra::ByteCount{v + kPrefixBytes};
    }

    [[nodiscard]] constexpr TrackLength clamped_to(infra::ByteCount room) const noexcept {
        return stored() > room ? of(infra::ByteCount{room.v - kPrefixBytes}) : *this;
    }

    [[nodiscard]] static constexpr std::span<std::byte, kPrefixBytes> prefix_in(
        std::span<std::byte> buf
    ) noexcept {
        return buf.first<kPrefixBytes>();
    }

    [[nodiscard]] constexpr std::span<std::byte> stream_in(
        std::span<std::byte> buf
    ) const noexcept {
        return infra::affine::from(buf, infra::ByteOffset{} + infra::ByteCount{kPrefixBytes});
    }

    [[nodiscard]] constexpr infra::ByteCount doubled() const noexcept {
        return stored() + stream();
    }

    [[nodiscard]] constexpr bool fits_work() const noexcept {
        return stored() <= infra::ByteCount{kWorkBytes};
    }

    [[nodiscard]] constexpr bool doubled_fits_work() const noexcept {
        return doubled() <= infra::ByteCount{kWorkBytes};
    }

    constexpr void double_in(std::span<std::byte> buf) const noexcept {
        const infra::ByteOffset at = infra::ByteOffset{} + infra::ByteCount{kPrefixBytes};
        std::ranges::copy(
            infra::affine::first(infra::affine::from(buf, at), stream()),
            infra::affine::from(buf, at + stream()).begin()
        );
    }

    friend constexpr bool operator==(TrackLength, TrackLength) = default;
    friend constexpr auto operator<=>(TrackLength, TrackLength) = default;
};

struct TrackSpace {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr TrackSpace padded_for(TrackLength length) noexcept {
        const TrackLength padded = length.v <= kGcr ? TrackLength{kGcr}
            : length.v <= kMfm                      ? TrackLength{kMfm}
                                                    : length;
        return TrackSpace{static_cast<std::uint32_t>(padded.stored().v)};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return v == 0; }

    [[nodiscard]] constexpr bool holds(TrackLength length) const noexcept {
        return length.stored().v <= v;
    }

    [[nodiscard]] constexpr infra::ByteCount bytes() const noexcept { return infra::ByteCount{v}; }

    [[nodiscard]] constexpr infra::ByteCount fill_behind(TrackLength length) const noexcept {
        const auto unused = static_cast<std::int32_t>(
            v - static_cast<std::uint32_t>(length.stored().v)
        );
        if (unused <= 0)
            return {};
        return infra::ByteCount{
            std::min<std::size_t>(static_cast<std::size_t>(unused), kWorkBytes)
        };
    }

    [[nodiscard]] static constexpr bool is_mfm(TrackLength length) noexcept {
        return length.v > kGcr;
    }

    friend constexpr bool operator==(TrackSpace, TrackSpace) = default;

private:
    enum Padded : std::uint16_t { kGcr = 7930, kMfm = 12500 };
};

constexpr TrackSpace ImagePos::space_to(ImagePos end) const noexcept {
    return TrackSpace{infra::affine::sub(end.v, v)};
}

struct Interleave {
    std::uint8_t v = 0;

    static const Interleave kFile;
    static const Interleave kDirectory;
    friend constexpr bool operator==(Interleave, Interleave) = default;
};

inline constexpr Interleave Interleave::kFile{10};
inline constexpr Interleave Interleave::kDirectory{3};

struct SectorNumber {
    std::uint8_t v = 0;

    static const SectorNumber kNone;

    [[nodiscard]] static constexpr SectorNumber of(std::byte b) noexcept {
        return SectorNumber{std::to_integer<std::uint8_t>(b)};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return static_cast<std::byte>(v); }

    [[nodiscard]] constexpr SectorNumber next() const noexcept {
        return SectorNumber{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr SectorNumber advanced(
        Interleave step,
        proto::BlockCount on_track
    ) const noexcept {
        return SectorNumber{static_cast<std::uint8_t>((v + step.v) % on_track.v)};
    }

    [[nodiscard]] constexpr SectorNumber interleaved(
        Interleave step,
        proto::BlockCount on_track
    ) const noexcept {
        SectorNumber next = advanced(step, on_track);
        constexpr std::uint8_t kSwapPair = 2;
        if ((on_track.v & 1u) == 0 && (step.v & 1u) == 0 && next.v < kSwapPair)
            next.v ^= 1u;
        return next;
    }

    [[nodiscard]] constexpr infra::ByteOffset free_map_byte() const noexcept {
        return infra::ByteOffset{static_cast<std::size_t>(v / CHAR_BIT)};
    }

    [[nodiscard]] constexpr std::byte free_map_bit() const noexcept {
        return std::byte{1} << (v % CHAR_BIT);
    }

    [[nodiscard]] constexpr proto::BlockCount before() const noexcept {
        return proto::BlockCount{v};
    }

    friend constexpr bool operator==(SectorNumber, SectorNumber) = default;
};

inline constexpr SectorNumber SectorNumber::kNone{0xFF};
inline constexpr SpeedZone SpeedZone::kMfm{8};

struct LogicalTrack {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr LogicalTrack of(FullTrack track, TrackTotal tracks) noexcept {
        return track.second_side() ? second_side_of(track, tracks)
                                   : LogicalTrack{static_cast<std::uint8_t>(track.v + 1u)};
    }

    [[nodiscard]] static constexpr LogicalTrack of_written(
        FullTrack track,
        TrackTotal tracks
    ) noexcept {
        return track > FullTrack::kSecondSide
            ? second_side_of(track, tracks)
            : LogicalTrack{static_cast<std::uint8_t>(track.v + 1u)};
    }

    [[nodiscard]] constexpr SpeedZone speed() const noexcept {
        return SpeedZone{
            v < kZone2First       ? kSpeed3
                : v < kZone1First ? kSpeed2
                : v < kZone0First ? kSpeed1
                                  : kSpeed0
        };
    }

    [[nodiscard]] constexpr infra::ByteCount sector_gap() const noexcept {
        return infra::ByteCount{
            v < kZone2First       ? kGap3
                : v < kZone1First ? kGap2
                : v < kZone0First ? kGap1
                                  : kGap0
        };
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return static_cast<std::byte>(v); }

    [[nodiscard]] constexpr FullTrack on_first_side() const noexcept {
        return FullTrack{static_cast<std::uint8_t>(v - 1u)};
    }

    [[nodiscard]] constexpr LogicalTrack next() const noexcept {
        return LogicalTrack{static_cast<std::uint8_t>(v + 1u)};
    }

    [[nodiscard]] constexpr LogicalTrack next_wrapping(TrackTotal tracks) const noexcept {
        return LogicalTrack{static_cast<std::uint8_t>(v % tracks.v + 1u)};
    }

    friend constexpr bool operator==(LogicalTrack, LogicalTrack) = default;

private:
    enum ZoneStart : std::uint8_t { kZone2First = 18, kZone1First = 25, kZone0First = 31 };

    enum Speed : std::uint32_t { kSpeed0 = 0, kSpeed1 = 1, kSpeed2 = 2, kSpeed3 = 3 };

    enum Gap : std::size_t { kGap3 = 8, kGap2 = 17, kGap1 = 12, kGap0 = 9 };

    [[nodiscard]] static constexpr LogicalTrack second_side_of(
        FullTrack track,
        TrackTotal tracks
    ) noexcept {
        return LogicalTrack{
            static_cast<std::uint8_t>(track.on_side().v + tracks.first_side().v + 1u)
        };
    }
};

struct TrackWord {
    std::uint32_t v = 0;

    [[nodiscard]] static constexpr TrackWord of(proto::Lba word) noexcept {
        return TrackWord{word.v};
    }

    [[nodiscard]] constexpr HalfTrack half_track() const noexcept {
        return HalfTrack{static_cast<std::uint8_t>(v & kHalfTrackMask)};
    }

    [[nodiscard]] constexpr bool drive_1571() const noexcept { return (v & kDrive1571) != 0; }

    [[nodiscard]] constexpr SpeedZone given_speed() const noexcept {
        return SpeedZone{(v >> kSpeedShift) & kSpeedMask};
    }

    friend constexpr bool operator==(TrackWord, TrackWord) = default;

private:
    enum Field : std::uint32_t {
        kHalfTrackMask = 0xFF,
        kSpeedShift = 8,
        kSpeedMask = 0x3,
        kDrive1571 = 0x400,
    };
};

struct DiskId {
    std::array<std::byte, 2> v{};

    static const proto::FileOffset kPosition;

    [[nodiscard]] static constexpr DiskId load(std::span<const std::byte, 2> bytes) noexcept {
        DiskId id{};
        std::ranges::copy(bytes, id.v.begin());
        return id;
    }

    [[nodiscard]] static constexpr DiskId of_header(std::span<const std::byte, 2> header) noexcept {
        return DiskId{{header[1], header[0]}};
    }

    [[nodiscard]] constexpr std::array<std::byte, 2> header_order() const noexcept {
        return {v[1], v[0]};
    }

    [[nodiscard]] constexpr std::byte xor_sum() const noexcept { return v[0] ^ v[1]; }

    friend constexpr bool operator==(const DiskId&, const DiskId&) = default;
};

inline constexpr proto::FileOffset DiskId::kPosition{0x165A2};

struct Group {
    static constexpr std::size_t kDataBytes = 4;
    static constexpr std::size_t kGcrBytes = 5;
    std::array<std::byte, kGcrBytes> v{};

    [[nodiscard]] static constexpr Group encode(
        std::span<const std::byte, kDataBytes> data
    ) noexcept;

    [[nodiscard]] constexpr std::array<std::byte, kDataBytes> decode() const noexcept;

    [[nodiscard]] static constexpr Group load(std::span<const std::byte, kGcrBytes> coded) noexcept;
    constexpr void store(std::span<std::byte, kGcrBytes> coded) const noexcept;

    [[nodiscard]] static constexpr infra::ByteCount coded(infra::ByteCount data) noexcept;
    friend constexpr bool operator==(const Group&, const Group&) = default;

private:
    enum Bits : unsigned {
        kCodeBits = 5,
        kCodeMask = 0x1F,
        kNybbleBits = 4,
        kNybbleMask = 0xF,
        kByteBits = 8,
        kByteMask = 0xFF,
    };

    static constexpr std::array<std::uint8_t, 16> kCode{
        0x0a,
        0x0b,
        0x12,
        0x13,
        0x0e,
        0x0f,
        0x16,
        0x17,
        0x09,
        0x19,
        0x1a,
        0x1b,
        0x0d,
        0x1d,
        0x1e,
        0x15
    };
    static constexpr std::array<std::uint8_t, 32> kNybble{0, 0, 0,  0, 0,  0,  0, 0,  0,  8, 0,
                                                          1, 0, 12, 4, 5,  0,  0, 2,  3,  0, 15,
                                                          6, 7, 0,  9, 10, 11, 0, 13, 14, 0};
};

constexpr Group Group::encode(std::span<const std::byte, kDataBytes> data) noexcept {
    std::uint64_t bits = 0;
    for (const std::byte b : data) {
        const unsigned byte = std::to_integer<unsigned>(b);
        bits = (bits << kCodeBits) | kCode[(byte >> kNybbleBits) & kNybbleMask];
        bits = (bits << kCodeBits) | kCode[byte & kNybbleMask];
    }
    Group g{};
    for (std::size_t i = kGcrBytes; i-- > 0;) {
        g.v[i] = static_cast<std::byte>(bits & kByteMask);
        bits >>= kByteBits;
    }
    return g;
}

constexpr std::array<std::byte, Group::kDataBytes> Group::decode() const noexcept {
    std::uint64_t bits = 0;
    for (const std::byte b : v)
        bits = (bits << kByteBits) | std::to_integer<unsigned>(b);
    std::array<std::byte, kDataBytes> data{};
    for (std::size_t i = kDataBytes; i-- > 0;) {
        const unsigned low = kNybble[bits & kCodeMask];
        bits >>= kCodeBits;
        const unsigned high = kNybble[bits & kCodeMask];
        bits >>= kCodeBits;
        data[i] = static_cast<std::byte>((high << kNybbleBits) | low);
    }
    return data;
}

constexpr Group Group::load(std::span<const std::byte, kGcrBytes> coded) noexcept {
    Group g{};
    std::ranges::copy(coded, g.v.begin());
    return g;
}

constexpr void Group::store(std::span<std::byte, kGcrBytes> coded) const noexcept {
    std::ranges::copy(v, coded.begin());
}

constexpr infra::ByteCount Group::coded(infra::ByteCount data) noexcept {
    return data / kDataBytes * kGcrBytes;
}

static_assert(
    Group::encode(std::array{std::byte{0x08}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01}})
        == Group{std::array{
            std::byte{0x52},
            std::byte{0x54},
            std::byte{0xA5},
            std::byte{0x29},
            std::byte{0x4B}
        }},
    "c64.cpp bin2gcr: 08 00 00 01 is 01010 01001 then six codes ending 01011"
);
static_assert(
    Group::encode(std::array{std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD}})
        .decode()
    == std::array{std::byte{0x12}, std::byte{0x34}, std::byte{0xAB}, std::byte{0xCD}}
);
static_assert(
    LogicalTrack::of(FullTrack{42}, TrackTotal{70}) == LogicalTrack{36}
        && LogicalTrack::of_written(FullTrack{42}, TrackTotal{70}) == LogicalTrack{43},
    "c64.cpp: the synthesiser tests >= 42 where c64_get_track_speed tests > 42"
);
static_assert(
    LogicalTrack{17}.speed() == SpeedZone{3}
        && LogicalTrack{18}.speed() == SpeedZone{2}
        && LogicalTrack{31}.speed() == SpeedZone{}
        && LogicalTrack{24}.sector_gap() == infra::ByteCount{17},
    "c64.cpp c64_synthesize_gcr_track: zones end before 18, 25 and 31"
);

}
