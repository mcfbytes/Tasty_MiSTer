// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/sector_mirror.h"

#include <algorithm>
#include <span>
#include <tuple>

#include "infra/affine.h"
#include "infra/span_cut.h"

namespace mister::svc {

SectorMirror::SectorMirror(proto::FileSize size, std::vector<std::byte> sectors)
    : sectors_(std::move(sectors)),
      synth_(size, disk_id(sectors_)) {}

gcr::DiskId SectorMirror::disk_id(const std::vector<std::byte>& sectors) noexcept {
    const std::span<const std::byte> image{sectors};
    if (gcr::DiskId::kPosition.remaining_in(proto::FileSize::of(image))
        >= proto::FileSize{std::tuple_size_v<decltype(gcr::DiskId::v)>}) {
        return gcr::DiskId::load(
            infra::affine::from(image, gcr::DiskId::kPosition)
                .first<std::tuple_size_v<decltype(gcr::DiskId::v)>>()
        );
    }
    return gcr::DiskId{};
}

Ex<std::optional<MirrorPiece>> SectorMirror::next() {
    TASTY_SEAT_BODY(SectorMirror);
    if (header_out_)
        return std::optional<MirrorPiece>{};
    if (const auto read = synth_.next_read()) {
        const std::span<const std::byte> from = infra::affine::from_clamped(
            std::span<const std::byte>{sectors_},
            read->at
        );
        const std::span<const std::byte> got = infra::affine::first_clamped(
            from,
            proto::FileSize::of(read->into)
        );
        std::ranges::copy(got, read->into.begin());
        return std::optional<MirrorPiece>{synth_.track()};
    }
    header_out_ = true;
    return std::optional<MirrorPiece>{synth_.header()};
}

}
