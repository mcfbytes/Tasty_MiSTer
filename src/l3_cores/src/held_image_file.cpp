// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/held_image_file.h"

#include <algorithm>
#include <utility>

#include "infra/affine.h"
#include "infra/span_cut.h"

namespace mister::cores {

HeldImageFile::HeldImageFile(std::vector<std::uint8_t>&& image) noexcept
    : image_(std::move(image)) {}

Ex<infra::ByteCount> HeldImageFile::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(HeldImageFile);
    const std::span<const std::byte> image = std::as_bytes(std::span(image_));
    const std::span<const std::byte> from = infra::affine::from_clamped(image, at);
    const std::span<const std::byte> piece = infra::affine::first_clamped(
        from,
        proto::FileSize::of(dst)
    );
    std::ranges::copy(piece, dst.begin());
    return infra::ByteCount{infra::ByteCount{piece.size()}};
}

Ex<infra::ByteCount> HeldImageFile::write_at(proto::FileOffset, std::span<const std::byte>) {
    TASTY_SEAT_BODY(HeldImageFile);
    return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
}

Ex<proto::FileSize> HeldImageFile::size() const {
    TASTY_SEAT_BODY(HeldImageFile);
    return proto::FileSize::of(std::span(image_));
}

Ex<void> HeldImageFile::flush() {
    TASTY_SEAT_BODY(HeldImageFile);
    return {};
}

}
