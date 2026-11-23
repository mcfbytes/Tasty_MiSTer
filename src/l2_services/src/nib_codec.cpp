// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/nib_codec.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "infra/errno.h"
#include "infra/prelude.h"
#include "infra/span_cut.h"
#include "svc/six_and_two.h"

namespace mister::svc {
namespace {

using infra::affine::first_clamped;
using infra::affine::from;

using Bytes = std::span<const std::byte>;

constexpr proto::FileSize kWriteActBytes{512};

using DosTrack = std::array<std::byte, kDosTrackBytes.file_word<std::size_t>()>;

constexpr std::byte kZeroByte{};

[[nodiscard]] Ex<void> read_zero_filled(
    IFile& file,
    proto::FileOffset at,
    std::span<std::byte> dst
) {
    while (!dst.empty()) {
        const auto got = file.read_at(at, dst);
        if (!got)
            return std::unexpected(got.error());
        if (got->empty())
            break;
        at += proto::FileSize::of(*got);
        dst = got->past_in(dst);
    }
    std::ranges::fill(dst, kZeroByte);
    return {};
}

[[nodiscard]] Ex<void> encode_track(IFile& file, NibTrack track, std::span<std::byte> image) {
    DosTrack source{};
    if (auto r = read_zero_filled(file, track.source_start(), source); !r)
        return r;
    encode_nib_track(track, source, track.nibbles_in(image));
    return {};
}

}

NibCodec::NibCodec(std::unique_ptr<IFile> file, bool writable) noexcept
    : file_(std::move(file)),
      writable_(writable) {}

Ex<std::unique_ptr<IDiskImage>> NibCodec::open(std::unique_ptr<IFile> file, bool writable) {
    TASTY_SEAT_BODY(NibCodec);
    std::unique_ptr<NibCodec> codec{new NibCodec(std::move(file), writable)};
    for (std::optional<NibTrack> track = NibTrack{}; track; track = track->next()) {
        if (auto e = encode_track(*codec->file_, *track, codec->image_); !e)
            return std::unexpected(e.error());
    }
    return std::unique_ptr<IDiskImage>{std::move(codec)};
}

FileSize NibCodec::size() const noexcept {
    TASTY_SEAT_BODY(NibCodec);
    return kNibImageBytes;
}

bool NibCodec::writable() const noexcept {
    TASTY_SEAT_BODY(NibCodec);
    return writable_;
}

Ex<infra::ByteCount> NibCodec::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(NibCodec);
    if (!kNibImageBytes.holds(at))
        return infra::ByteCount{};
    const Bytes rest = from(Bytes{image_}, at);
    const Bytes served = rest.first(std::min(rest.size(), dst.size()));
    std::ranges::copy(served, dst.begin());
    return infra::ByteCount{served.size()};
}

Ex<infra::ByteCount> NibCodec::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(NibCodec);
    if (!writable_)
        return std::unexpected(infra::Errno{EROFS}.refusal(Errc::io, ERR_SITE()));
    for (Bytes rest = src; !rest.empty();) {
        const Bytes act = first_clamped(rest, kWriteActBytes);
        if (auto r = write_act_(at, act); !r)
            return std::unexpected(r.error());
        at += proto::FileSize::of(act);
        rest = rest.subspan(act.size());
    }
    return infra::ByteCount{src.size()};
}

Ex<void> NibCodec::write_act_(proto::FileOffset at, std::span<const std::byte> act) {
    const std::optional<NibTrack> track = NibTrack::holding(at);
    if (!track)
        return {};
    if (pending_.track_start != track->image_start())
        pending_ = Pending{.track_start = track->image_start()};
    const std::span<std::byte> room = from(
        std::span<std::byte>{pending_.bytes},
        at - track->image_start()
    );
    const Bytes taken = act.first(std::min(act.size(), room.size()));
    std::ranges::copy(taken, room.begin());
    pending_.filled += proto::FileSize::of(taken);

    bool wrote = false;
    NibSectorWalk walk{first_clamped(Bytes{pending_.bytes}, pending_.filled)};
    for (std::optional<NibSectorWalk::Found> found = walk.next(); found; found = walk.next()) {
        if (found->track != track->address())
            continue;
        const std::optional<proto::FileSize> place = found->place_in_track();
        if (!place)
            continue;
        if (auto w = file_->write_all_at(track->source_start() + *place, found->data); !w)
            return w;
        wrote = true;
    }
    if (!wrote)
        return {};
    if (auto f = file_->flush(); !f)
        return f;
    return encode_track(*file_, *track, image_);
}

}
