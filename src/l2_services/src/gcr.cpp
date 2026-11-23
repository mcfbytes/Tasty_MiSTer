// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/gcr.h"

#include <algorithm>
#include <array>

namespace mister::svc::gcr {

namespace {

inline constexpr std::byte kGap{0x55};
inline constexpr std::byte kHeaderPad{0x0F};

class Stream {
public:
    explicit Stream(std::span<std::byte> out) noexcept : out_(out) {}

    void run(std::byte value, infra::ByteCount count) noexcept {
        const infra::ByteCount n = std::min(count, room_());
        std::ranges::fill(infra::affine::first(infra::affine::from(out_, at_), n), value);
        at_ += n;
    }

    void coded(std::span<const std::byte> block) noexcept {
        for (std::size_t i = 0; i + Group::kDataBytes <= block.size(); i += Group::kDataBytes) {
            if (room_() < infra::ByteCount{Group::kGcrBytes})
                return;
            const Group g = Group::encode(block.subspan(i).first<Group::kDataBytes>());
            g.store(infra::affine::from(out_, at_).first<Group::kGcrBytes>());
            at_ += infra::ByteCount{Group::kGcrBytes};
        }
    }

    [[nodiscard]] infra::ByteCount written() const noexcept { return at_ - infra::ByteOffset{}; }

private:
    [[nodiscard]] infra::ByteCount room_() const noexcept {
        return infra::ByteOffset{} + infra::ByteCount{out_.size()} - at_;
    }

    std::span<std::byte> out_;
    infra::ByteOffset at_{};
};

}

infra::ByteCount synthesize_track(
    LogicalTrack track,
    const DiskId& id,
    std::span<const std::byte> sectors,
    std::span<std::byte> out
) noexcept {
    Stream stream{out};
    SectorNumber sector{};
    for (std::size_t at = 0; at + kSectorDataBytes <= sectors.size(); at += kSectorDataBytes) {
        const std::span<const std::byte, kSectorDataBytes> data = sectors.subspan(at)
                                                                      .first<kSectorDataBytes>();

        stream.run(kSync, kSyncRun);
        const std::array<std::byte, 2> id_bytes = id.header_order();
        const std::array<std::byte, kHeaderBlockBytes> header{
            kHeaderMark,
            sector.byte() ^ track.byte() ^ id.xor_sum(),
            sector.byte(),
            track.byte(),
            id_bytes[0],
            id_bytes[1],
            kHeaderPad,
            kHeaderPad
        };
        stream.coded(header);
        stream.run(kGap, kHeaderGap);

        stream.run(kSync, kSyncRun);
        std::array<std::byte, kDataBlockBytes> block{};
        block.front() = kDataMark;
        std::byte checksum{};
        for (std::size_t i = 0; i < kSectorDataBytes; ++i) {
            block[i + 1] = data[i];
            checksum ^= data[i];
        }
        block[kSectorDataBytes + 1] = checksum;
        stream.coded(block);
        stream.run(kGap, track.sector_gap());
        sector = sector.next();
    }
    return stream.written();
}

SpeedZone written_speed(TrackWord word, TrackTotal slots, TrackLength length) noexcept {
    if (TrackSpace::is_mfm(length))
        return SpeedZone::kMfm;
    if (word.drive_1571())
        return word.given_speed();
    return LogicalTrack::of_written(word.half_track().full(), slots).speed();
}

std::optional<TrackTotal> track_slots(std::span<const std::byte> head) noexcept {
    if (head.size() <= kSlotCountAt)
        return std::nullopt;
    const TrackTotal slots = TrackTotal::of(head[kSlotCountAt]);
    if (slots != TrackTotal::kSlots1541 && slots != TrackTotal::kSlots1571)
        return std::nullopt;
    return slots;
}

std::optional<GcrCaps> image_caps(
    GcrLayout layout,
    proto::FileSize size,
    std::span<const std::byte> head
) noexcept {
    switch (layout) {
        case GcrLayout::Sectors:
            return GcrCaps::of_sectors(SectorMap::of_image(size).tracks());
        case GcrLayout::Tracks:
            if (const auto slots = track_slots(head))
                return GcrCaps::of_tracks(*slots);
            return std::nullopt;
    }
    return std::nullopt;
}

}
