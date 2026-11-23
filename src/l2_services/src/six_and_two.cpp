// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/six_and_two.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>

#include "infra/prelude.h"
#include "infra/span_cut.h"

namespace mister::svc {
namespace {

using infra::affine::first;
using infra::affine::first_clamped;
using infra::affine::from;
using infra::affine::from_clamped;

using Bytes = std::span<const std::byte>;

constexpr proto::FileSize kLowSixes{86};
constexpr proto::FileSize kSectorSixes = kLowSixes + kDosSectorBytes;

constexpr proto::FileSize kPairBytes{2};
constexpr proto::FileSize kAddressBytes = kPairBytes * std::uint64_t{4};

constexpr proto::FileSize kParseFloor{400};

using SectorSixes = std::array<std::byte, kSectorSixes.file_word<std::size_t>()>;

static_assert(
    kNibImageBytes == kNibTrackBytes * kDosDiskBytes.whole_records(kDosTrackBytes),
    "dsk2nib_lib.cpp:14, :26: a 140K disk's 35 tracks, as nibble tracks"
);

template <std::size_t N>
consteval std::array<std::byte, N> run_of(std::byte b) {
    std::array<std::byte, N> run{};
    run.fill(b);
    return run;
}

constexpr std::byte kZeroByte{};
constexpr std::byte kGapByte{0xFF};
constexpr auto kGap1 = run_of<48>(kGapByte);
constexpr auto kGap2 = run_of<5>(kGapByte);
constexpr std::array kAddressProlog{std::byte{0xD5}, std::byte{0xAA}, std::byte{0x96}};
constexpr std::array kDataProlog{std::byte{0xD5}, std::byte{0xAA}, std::byte{0xAD}};
constexpr std::array kEpilog{std::byte{0xDE}, std::byte{0xAA}, std::byte{0xEB}};

constexpr std::array kEpilogSeen{std::byte{0xDE}, std::byte{0xAA}};
constexpr std::byte kVolume{254};

static_assert(
    proto::FileSize{
        kGap1.size()
        + kAddressProlog.size()
        + kEpilog.size()
        + kGap2.size()
        + kDataProlog.size()
        + 1
        + kEpilog.size()
    }
            + kAddressBytes
            + kSectorSixes
        == kNibSectorBytes,
    "dsk2nib_lib.cpp:31-52: nib_sector_t is 416 bytes, gap to data epilog"
);
static_assert(std::ranges::equal(kEpilogSeen, std::span{kEpilog}.first(kEpilogSeen.size())));

consteval auto disk_bytes_by_six() {
    constexpr auto table = std::to_array<std::uint8_t>(
        {0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac, 0xad, 0xae,
         0xaf, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe,
         0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3, 0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd,
         0xde, 0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf2,
         0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff}
    );
    std::array<std::byte, table.size()> bytes{};
    for (std::size_t six = 0; six < table.size(); ++six)
        bytes[six] = std::byte{table[six]};
    return bytes;
}

constexpr auto kDiskByteOfSix = disk_bytes_by_six();

using SixMap = std::
    array<std::optional<std::byte>, std::size_t{std::numeric_limits<std::uint8_t>::max()} + 1>;

consteval SixMap invert_disk_bytes() {
    SixMap six_of{};
    for (std::uint8_t six = 0; six < kDiskByteOfSix.size(); ++six)
        six_of[std::to_integer<std::size_t>(kDiskByteOfSix[six])] = std::byte{six};
    return six_of;
}

constexpr auto kSixOfDiskByte = invert_disk_bytes();

constexpr std::byte kSixBits{0x3F};

[[nodiscard]] constexpr std::byte disk_byte(std::byte six) noexcept {
    return kDiskByteOfSix[std::to_integer<std::size_t>(six & kSixBits)];
}

[[nodiscard]] constexpr std::byte six_of(std::byte disk) noexcept {
    return kSixOfDiskByte[std::to_integer<std::size_t>(disk)].value_or(kZeroByte);
}

consteval bool sixes_round_trip() {
    for (std::uint8_t six = 0; six < kDiskByteOfSix.size(); ++six)
        if (six_of(disk_byte(std::byte{six})) != std::byte{six})
            return false;
    return six_of(kGapByte) == kSixBits && six_of(kZeroByte) == kZeroByte;
}

static_assert(
    sixes_round_trip(),
    "dsk2nib_lib.cpp:65-74, :235-243: the 6-and-2 table and its tolerant inverse"
);

[[nodiscard]] constexpr std::byte swapped_pair(std::byte b) noexcept {
    constexpr std::byte kBit0{1};
    constexpr std::byte kBit1{2};
    return ((b & kBit1) >> 1) | ((b & kBit0) << 1);
}

constexpr std::byte kClockBits{0xAA};
constexpr std::byte kDataBits{0x55};

[[nodiscard]] constexpr std::array<std::byte, 2> four_and_four(std::byte value) noexcept {
    return {((value >> 1) & kDataBits) | kClockBits, (value & kDataBits) | kClockBits};
}

[[nodiscard]] constexpr std::byte four_and_four_value(Bytes pair) noexcept {
    return ((pair[0] << 1) & kClockBits) | (pair[1] & kDataBits);
}

constexpr auto kVolumePair = four_and_four(kVolume);
static_assert(
    four_and_four_value(kVolumePair) == kVolume,
    "dsk2nib_lib.cpp:81-86, :228-233: odd_even_decode undoes odd_even_encode"
);

consteval bool tracks_fill_the_image() {
    const proto::FileOffset end = proto::kFileStart + kNibImageBytes;
    const std::optional<NibTrack> last = NibTrack::holding(end - kNibTrackBytes);
    return !NibTrack::holding(end)
        && last
        && !last->next()
        && last->image_start() + kNibTrackBytes == end
        && last->source_start() + kDosTrackBytes == proto::kFileStart + kDosDiskBytes;
}

static_assert(
    tracks_fill_the_image(),
    "dsk2nib_lib.cpp:14, :26: 35 tracks of 6,656 nibble bytes are the whole image"
);

struct Sector {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr std::byte address() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(Sector, Sector) = default;
};

struct SectorPlace {
    Sector sector{};
    proto::FileSize in_source{};
};

using TrackSectors = std::array<SectorPlace, kDosTrackBytes.whole_records(kDosSectorBytes)>;

consteval TrackSectors physical_order() {

    constexpr auto soft_of_logical = std::to_array<std::uint8_t>(
        {0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF}
    );
    constexpr auto physical_of_logical = std::to_array<std::uint8_t>(
        {0, 0xD, 0xB, 9, 7, 5, 3, 1, 0xE, 0xC, 0xA, 8, 6, 4, 2, 0xF}
    );
    TrackSectors order{};
    static_assert(
        soft_of_logical.size() == order.size() && physical_of_logical.size() == order.size()
    );
    for (std::uint8_t logical = 0; logical < order.size(); ++logical) {
        order[physical_of_logical[logical]] = SectorPlace{
            .sector = Sector{logical},
            .in_source = kDosSectorBytes * std::uint64_t{soft_of_logical[logical]}
        };
    }
    return order;
}

constexpr TrackSectors kPhysicalOrder = physical_order();

consteval bool every_place_holds_its_own_sector() {
    for (const SectorPlace& a : kPhysicalOrder) {
        std::size_t same_number = 0;
        std::size_t same_source = 0;
        for (const SectorPlace& b : kPhysicalOrder) {
            if (a.sector == b.sector)
                ++same_number;
            if (a.in_source == b.in_source)
                ++same_source;
        }
        if (same_number != 1 || same_source != 1)
            return false;
        if (a.in_source + kDosSectorBytes > kDosTrackBytes)
            return false;
    }
    return true;
}

static_assert(
    every_place_holds_its_own_sector(),
    "dsk2nib_lib.cpp:59-62, :126-134: both interleaves are permutations, so "
    "phys_to_logical_sector's first match is its only one"
);

[[nodiscard]] SectorSixes sixes_of(Bytes sector) noexcept {
    constexpr unsigned kPairBits = 2;
    SectorSixes sixes{};
    const std::span<std::byte> low = first(std::span<std::byte>{sixes}, kLowSixes);
    unsigned shift = 0;
    for (Bytes part = sector; !part.empty(); part = from_clamped(part, kLowSixes)) {
        auto slot = low.begin();
        for (const std::byte b : first_clamped(part, kLowSixes))
            *slot++ |= swapped_pair(b) << shift;
        shift += kPairBits;
    }
    auto high = from(std::span<std::byte>{sixes}, kLowSixes).begin();
    for (const std::byte b : sector)
        *high++ = b >> kPairBits;
    return sixes;
}

void encode_sector(
    NibTrack track,
    const SectorPlace& place,
    Bytes source,
    std::span<std::byte> field
) noexcept {
    auto out = field.begin();
    const auto put = [&out](Bytes bytes) {
        out = std::ranges::copy(bytes, out).out;
    };
    put(kGap1);
    put(kAddressProlog);
    put(kVolumePair);
    put(four_and_four(track.address()));
    put(four_and_four(place.sector.address()));
    put(four_and_four(kVolume ^ track.address() ^ place.sector.address()));
    put(kEpilog);
    put(kGap2);
    put(kDataProlog);

    std::byte before{};
    for (const std::byte six : sixes_of(source)) {
        *out++ = disk_byte(six ^ before);
        before = six;
    }
    *out++ = disk_byte(before);
    put(kEpilog);
}

template <std::size_t N>
[[nodiscard]] std::optional<Bytes> skip_past(
    Bytes in,
    const std::array<std::byte, N>& mark
) noexcept {
    auto want = mark.begin();
    for (auto it = in.begin(); it != in.end(); ++it) {
        want = *it == *want ? std::next(want) : mark.begin();
        if (want == mark.end())
            return Bytes{std::next(it), in.end()};
    }
    return std::nullopt;
}

[[nodiscard]] DosSectorBytes sector_of(Bytes disk_bytes) noexcept {
    constexpr unsigned kPairBits = 2;
    SectorSixes sixes{};
    std::byte running{};
    auto six = sixes.begin();
    for (const std::byte disk : disk_bytes)
        *six++ = running ^= six_of(disk);
    const Bytes low = first(Bytes{sixes}, kLowSixes);
    DosSectorBytes sector{};
    auto out = sector.begin();
    unsigned shift = 0;
    for (Bytes part = from(Bytes{sixes}, kLowSixes); !part.empty();
         part = from_clamped(part, kLowSixes)) {
        auto pair = low.begin();
        for (const std::byte high : first_clamped(part, kLowSixes))
            *out++ = (high << kPairBits) | swapped_pair(*pair++ >> shift);
        shift += kPairBits;
    }
    return sector;
}

[[nodiscard]] std::optional<NibSectorWalk::Found> parse_sector(Bytes nib) noexcept {
    const std::optional<Bytes> address = skip_past(nib, kAddressProlog);
    if (!address || proto::FileSize::of(*address) < kAddressBytes)
        return std::nullopt;
    Bytes rest = from(*address, kPairBytes);
    const std::byte track = four_and_four_value(first(rest, kPairBytes));
    rest = from(rest, kPairBytes);
    const std::byte sector = four_and_four_value(first(rest, kPairBytes));
    rest = from(rest, kPairBytes + kPairBytes);
    const std::optional<Bytes> epilog = skip_past(rest, kEpilogSeen);
    if (!epilog)
        return std::nullopt;
    const std::optional<Bytes> data = skip_past(*epilog, kDataProlog);

    if (!data || proto::FileSize::of(*data) <= kSectorSixes)
        return std::nullopt;
    return NibSectorWalk::Found{
        .track = track,
        .sector = sector,
        .data = sector_of(first(*data, kSectorSixes))
    };
}

}

std::byte disk_byte_of_six(std::byte six) noexcept {
    return disk_byte(six);
}

std::optional<std::byte> six_of_disk_byte(std::byte disk) noexcept {
    return kSixOfDiskByte[std::to_integer<std::size_t>(disk)];
}

void encode_nib_track(
    NibTrack track,
    std::span<const std::byte> sectors,
    std::span<std::byte> nibbles
) noexcept {
    std::span<std::byte> rest = nibbles;
    for (const SectorPlace& place : kPhysicalOrder) {
        encode_sector(
            track,
            place,
            first(from(sectors, place.in_source), kDosSectorBytes),
            first(rest, kNibSectorBytes)
        );
        rest = from(rest, kNibSectorBytes);
    }
}

std::optional<proto::FileSize> NibSectorWalk::Found::place_in_track() const noexcept {
    for (const SectorPlace& place : kPhysicalOrder)
        if (place.sector.address() == sector)
            return place.in_source;
    return std::nullopt;
}

std::optional<NibSectorWalk::Found> NibSectorWalk::next() noexcept {
    while (proto::FileSize::of(window_) > kParseFloor) {
        const std::optional<Found> found = parse_sector(window_);
        if (!found) {
            window_ = window_.subspan(1);
            continue;
        }
        window_ = from_clamped(window_, kNibSectorBytes);
        return found;
    }
    return std::nullopt;
}

}
