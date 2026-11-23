// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/woz525_codec.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "infra/span_cut.h"
#include "svc/six_and_two.h"
#include "svc/woz_format.h"

namespace mister::svc {
namespace {

using infra::affine::first;
using infra::affine::first_clamped;
using infra::affine::from;
using Bytes = std::span<const std::byte>;
using Out = std::span<std::byte>;

constexpr proto::BlockCount kBlocksPerTrack{13};
constexpr proto::FileOffset kFirstTrack = woz::kBlock.position_of(woz::kFirstBitsBlock);
static_assert(
    woz::kBlock.bytes_of(kBlocksPerTrack) == kNibTrackBytes,
    "iigs_fmt.cpp:413-415: a nibble track fills its 13 blocks exactly"
);
static_assert(
    kFirstTrack + kNibImageBytes == proto::kFileStart + kWoz525ImageBytes,
    "iigs_fmt.cpp:417: WOZ525_SIZE is the 1,536 header bytes and 35 tracks"
);

struct TrackSector {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr Out in(Out track) const noexcept {
        return first(from(track, kDosSectorBytes * std::uint64_t{v}), kDosSectorBytes);
    }
};

using SectorMap = std::array<TrackSector, kDosTrackBytes.whole_records(kDosSectorBytes)>;
constexpr SectorMap kDosToProDos{
    {{0}, {8}, {1}, {9}, {2}, {10}, {3}, {11}, {4}, {12}, {5}, {13}, {6}, {14}, {7}, {15}}
};
constexpr SectorMap kProDosToDos{
    {{0}, {2}, {4}, {6}, {8}, {10}, {12}, {14}, {1}, {3}, {5}, {7}, {9}, {11}, {13}, {15}}
};

void reorder_track(Out dst, Bytes src, const SectorMap& map) noexcept {
    for (Bytes rest = src; const TrackSector to : map) {
        std::ranges::copy(first(rest, kDosSectorBytes), to.in(dst).begin());
        rest = from(rest, kDosSectorBytes);
    }
}

proto::FileSize decode_track(Bytes nibbles, Out sectors) noexcept {
    proto::FileSize landed{};
    NibSectorWalk walk{nibbles};
    for (std::optional<NibSectorWalk::Found> s = walk.next(); s; s = walk.next()) {
        const std::optional<proto::FileSize> place = s->place_in_track();
        if (!place)
            continue;
        std::ranges::copy(s->data, from(sectors, *place).begin());
        landed += kDosSectorBytes;
    }
    return landed;
}

constexpr std::byte kZeroByte{};
constexpr woz::InfoFields kInfoFields{
    .type = woz::DiskType::Inch525,
    .creator = "MiSTer IIgs easy-WOZ 5.25",
    .sides = std::byte{1},
    .bit_timing = std::byte{32}
};

[[nodiscard]] bool lay_out_woz(Out woz, Bytes nib) noexcept {
    std::ranges::fill(woz, kZeroByte);
    woz::put_header(woz, kInfoFields);
    woz::put_track_data_size(woz, woz::kBlock.blocks_in(kNibImageBytes));
    const Out tmap = woz::tmap_of(woz);
    std::ranges::fill(tmap, woz::kNoTrack);
    const Out dir = woz::trks_of(woz);

    constexpr std::uint32_t kBitsPerNibble = 8;
    constexpr woz::BitCount kTrackBits{kNibTrackBytes.file_word<std::uint32_t>() * kBitsPerNibble};
    proto::Lba start = woz::kFirstBitsBlock;
    for (woz::TrackSlot slot{}; slot.nib_track(); slot = slot.next()) {
        slot.map_whole_track(tmap);
        if (!woz::put_track_entry(
                dir,
                slot,
                woz::TrackEntry{.start = start, .blocks = kBlocksPerTrack, .bits = kTrackBits}
            ))
            return false;
        start += kBlocksPerTrack;
    }
    std::ranges::copy(first(nib, kNibImageBytes), from(woz, kFirstTrack).begin());
    if (!woz::put_largest_track(woz, kBlocksPerTrack))
        return false;
    woz::seal(woz);
    return true;
}

[[nodiscard]] Error not_convertible(infra::ErrorSite at) noexcept {
    return Error{Errc::bad_format, at, 0};
}

}

Woz525Codec::Woz525Codec(std::unique_ptr<IDiskImage> source, SectorOrder order, bool writes_back)
    : source_(std::move(source)),
      order_(order),
      writes_back_(writes_back),
      woz_(kWoz525ImageBytes.zeroed<std::vector<std::byte>>()),
      dos_(kDosDiskBytes.zeroed<std::vector<std::byte>>()) {}

Ex<std::unique_ptr<IDiskImage>> Woz525Codec::open(
    std::unique_ptr<IDiskImage> source,
    SectorOrder order,
    bool writes_back
) {
    TASTY_SEAT_BODY(Woz525Codec);
    auto payload = read_whole(*source);
    if (!payload)
        return std::unexpected(payload.error());
    const Bytes given{*payload};
    const proto::FileSize length = proto::FileSize::of(given);

    std::vector<std::byte> dos = kDosDiskBytes.zeroed<std::vector<std::byte>>();
    const Out disk{dos};
    switch (order) {
        case SectorOrder::Nibble: {
            if (length != kNibImageBytes)
                return std::unexpected(not_convertible(ERR_SITE()));
            bool whole = true;
            for (std::optional<NibTrack> t = NibTrack{}; t; t = t->next())
                whole = decode_track(t->nibbles_in(given), t->sectors_in(disk)) >= kDosTrackBytes
                    && whole;
            if (!whole)
                return std::unexpected(not_convertible(ERR_SITE()));
            break;
        }
        case SectorOrder::ProDos:
            if (length != kDosDiskBytes)
                return std::unexpected(not_convertible(ERR_SITE()));
            for (std::optional<NibTrack> t = NibTrack{}; t; t = t->next())
                reorder_track(t->sectors_in(disk), t->sectors_in(given), kProDosToDos);
            break;
        case SectorOrder::Dos:
            if (length != kDosDiskBytes)
                return std::unexpected(not_convertible(ERR_SITE()));
            std::ranges::copy(given, disk.begin());
            break;
    }
    std::vector<std::byte> nib = kNibImageBytes.zeroed<std::vector<std::byte>>();
    for (std::optional<NibTrack> t = NibTrack{}; t; t = t->next())
        encode_nib_track(*t, t->sectors_in(Bytes{disk}), t->nibbles_in(Out{nib}));
    std::unique_ptr<Woz525Codec> codec{new Woz525Codec(std::move(source), order, writes_back)};
    if (!lay_out_woz(codec->woz_, nib))
        return std::unexpected(not_convertible(ERR_SITE()));
    return std::unique_ptr<IDiskImage>{std::move(codec)};
}

FileSize Woz525Codec::size() const noexcept {
    TASTY_SEAT_BODY(Woz525Codec);
    return kWoz525ImageBytes;
}

bool Woz525Codec::writable() const noexcept {
    TASTY_SEAT_BODY(Woz525Codec);
    return writes_back_;
}

Ex<infra::ByteCount> Woz525Codec::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(Woz525Codec);
    if (!kWoz525ImageBytes.holds(at))
        return infra::ByteCount{};
    const Bytes rest = from(Bytes{woz_}, at);
    const Bytes served = rest.first(std::min(rest.size(), dst.size()));
    std::ranges::copy(served, dst.begin());
    return infra::ByteCount{served.size()};
}

Ex<infra::ByteCount> Woz525Codec::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(Woz525Codec);
    for (Bytes rest = src; !rest.empty();) {
        const Bytes act = first_clamped(rest, woz::kBlock.bytes_of(proto::BlockCount{1}));
        if (auto r = write_act_(at, act); !r)
            return std::unexpected(r.error());
        at += proto::FileSize::of(act);
        rest = rest.subspan(act.size());
    }
    return infra::ByteCount{src.size()};
}

Ex<void> Woz525Codec::write_act_(proto::FileOffset at, std::span<const std::byte> act) {
    const proto::FileSize block = woz::kBlock.bytes_of(proto::BlockCount{1});
    if (kWoz525ImageBytes.clamp_write(at, block.file_word<std::size_t>())
        == block.file_word<std::size_t>())
        std::ranges::copy(act, from(Out{woz_}, at).begin());
    if (!writes_back_)
        return {};
    const proto::Lba lba = proto::Lba{} + woz::kBlock.blocks_in(at - proto::kFileStart);
    const std::optional<woz::TrackSlot> slot = woz::track_holding(woz_, lba);
    if (!slot)
        return {};

    const std::optional<NibTrack> track = slot->nib_track();
    if (woz::disk_type(woz_) != woz::DiskType::Inch525 || !track)
        return {};
    const std::optional<Bytes> trks = woz::find_chunk(woz_, woz::kTrks);
    if (!trks)
        return {};
    const std::optional<woz::TrackEntry> e = woz::track_entry(*trks, *slot);
    if (!e || e->blocks == proto::BlockCount{})
        return {};
    const proto::FileOffset bits = woz::kBlock.position_of(e->start);
    if (bits + kNibTrackBytes > proto::kFileStart + kWoz525ImageBytes)
        return {};
    const Out dos = track->sectors_in(Out{dos_});
    if (decode_track(first(from(Bytes{woz_}, bits), kNibTrackBytes), dos).empty())
        return {};
    std::array<std::byte, kDosTrackBytes.file_word<std::size_t>()> out{};
    if (order_ == SectorOrder::ProDos) {
        reorder_track(out, dos, kDosToProDos);
    } else {
        std::ranges::copy(dos, out.begin());
    }
    auto w = source_->write_at(track->source_start(), out);
    if (!w)
        return std::unexpected(w.error());
    return {};
}

}
