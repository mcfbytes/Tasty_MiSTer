// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/g64_synthesis.h"

#include <algorithm>
#include <string_view>

namespace mister::svc {

namespace {
inline constexpr std::string_view kSignature = "GCR-1541";
}

G64Synthesis::G64Synthesis(proto::FileSize size, const gcr::DiskId& id) noexcept
    : map_(gcr::SectorMap::of_image(size)),
      id_(id) {
    const gcr::TrackTotal slots = slots_();
    std::ranges::copy(std::as_bytes(std::span{kSignature}), header_.begin());
    header_[gcr::kSlotCountAt] = slots.byte();

    free_ = gcr::ImagePos{}.after(infra::ByteCount{header().bytes.size()});
}

gcr::TrackTotal G64Synthesis::slots_() const noexcept {
    return map_.tracks() > gcr::TrackTotal::kOneSide ? gcr::TrackTotal::kSlots1571
                                                     : gcr::TrackTotal::kSlots1541;
}

std::optional<G64Synthesis::Read> G64Synthesis::next_read() noexcept {

    for (; slots_().holds(next_); next_ = next_.next()) {
        if (next_.between_tracks())
            continue;
        const proto::BlockCount on_track = map_.sectors_on(next_.full());
        if (on_track == proto::BlockCount{})
            continue;
        return Read{
            .at = map_.position_of(next_.full()),
            .into = std::span<std::byte>{sectors_}.first(
                gcr::SectorMap::kSector.window_bytes(on_track)
            )
        };
    }
    return std::nullopt;
}

std::span<std::byte, gcr::kTableEntryBytes> G64Synthesis::position_entry_(
    gcr::HalfTrack slot
) noexcept {
    return slot.entry_in(infra::affine::from(std::span<std::byte>{header_}, gcr::kTablesAt));
}

std::span<std::byte, gcr::kTableEntryBytes> G64Synthesis::speed_entry_(
    gcr::HalfTrack slot
) noexcept {
    const std::span<std::byte> speeds = infra::affine::from(
        std::span<std::byte>{header_},
        gcr::kTablesAt + proto::FileSize::of(slots_().table_bytes())
    );
    return slot.entry_in(speeds);
}

MirrorPiece G64Synthesis::track() noexcept {
    const gcr::FullTrack full = next_.full();
    const gcr::LogicalTrack logical = gcr::LogicalTrack::of(full, map_.tracks());
    const std::span<const std::byte> sectors = std::span<const std::byte>{sectors_}.first(
        gcr::SectorMap::kSector.window_bytes(map_.sectors_on(full))
    );
    const std::span<std::byte> out{track_};
    const gcr::TrackLength length = gcr::TrackLength::of(
        gcr::synthesize_track(logical, id_, sectors, out.subspan(gcr::TrackLength::kPrefixBytes))
    );
    length.store(gcr::TrackLength::prefix_in(out));

    free_.store(position_entry_(next_));
    logical.speed().store(speed_entry_(next_));

    const MirrorPiece piece{
        .at = free_.offset(),
        .bytes = infra::affine::first(out, length.stored())
    };
    free_ = free_.after(length.stored());
    next_ = next_.next();
    return piece;
}

MirrorPiece G64Synthesis::header() const noexcept {
    return MirrorPiece{
        .at = proto::kFileStart,
        .bytes = infra::affine::first(std::span<const std::byte>{header_}, slots_().header_bytes())
    };
}

}
