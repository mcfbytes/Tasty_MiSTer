// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/track_file_mirror.h"

#include <span>
#include <utility>

#include "infra/affine.h"
#include "infra/span_cut.h"

namespace mister::svc {

TrackFileMirror::TrackFileMirror(std::unique_ptr<IFile> file, TrackFileMirrorExtent extent) noexcept
    : file_(std::move(file)),
      size_(extent.size),
      room_(extent.room) {}

Ex<std::optional<MirrorPiece>> TrackFileMirror::next() {
    TASTY_SEAT_BODY(TrackFileMirror);
    const proto::FileSize whole = size_.min(room_);
    const proto::FileSize left = at_.remaining_in(whole);
    if (left.empty())
        return std::optional<MirrorPiece>{};
    const proto::FileSize want = left.min(kTrackFilePieceBytes);
    const std::span<std::byte> dst = infra::affine::first(std::span<std::byte>{buf_}, want);
    auto got = file_->read_prefix_at(at_, dst);
    if (!got)
        return std::unexpected(got.error());
    if (got->empty())
        return std::optional<MirrorPiece>{};
    const MirrorPiece piece{
        .at = at_,
        .bytes = infra::affine::first(std::span<const std::byte>{dst}, *got)
    };
    at_ += proto::FileSize::of(*got);
    return std::optional<MirrorPiece>{piece};
}

}
