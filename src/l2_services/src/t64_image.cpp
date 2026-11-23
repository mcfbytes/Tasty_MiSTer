// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/t64_image.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "infra/span_cut.h"
#include "svc/t64.h"

namespace mister::svc {

Ex<std::unique_ptr<IDiskImage>> T64Image::open(std::unique_ptr<IFile> file, FileSize size) {
    TASTY_SEAT_BODY(T64Image);
    if (file == nullptr)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    if (size > t64::kMostTapeBytes)
        return std::unexpected(size.refusal(Errc::bad_format, ERR_SITE()));
    std::vector<std::byte> tape = size.zeroed<std::vector<std::byte>>();
    std::span<std::byte> rest{tape};
    proto::FileOffset at = proto::kFileStart;
    while (!rest.empty()) {
        const auto n = file->read_at(at, rest);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            break;
        at += FileSize::of(*n);
        rest = n->past_in(rest);
    }

    tape.resize(tape.size() - rest.size());
    std::optional<std::vector<std::byte>> image = t64::to_d64(tape);
    if (!image)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return std::unique_ptr<IDiskImage>(new T64Image(std::move(*image)));
}

T64Image::T64Image(std::vector<std::byte> image) noexcept : image_(std::move(image)) {}

FileSize T64Image::size() const noexcept {
    TASTY_SEAT_BODY(T64Image);
    return FileSize::of(std::span<const std::byte>{image_});
}

bool T64Image::writable() const noexcept {
    TASTY_SEAT_BODY(T64Image);
    return false;
}

Ex<infra::ByteCount> T64Image::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(T64Image);
    const auto held = infra::ByteCount{dst.size()}.first_in(
        infra::affine::from_clamped(std::span<const std::byte>{image_}, at)
    );
    if (held.empty())
        return infra::ByteCount{};
    std::ranges::copy(held, dst.begin());
    return infra::ByteCount{held.size()};
}

Ex<infra::ByteCount> T64Image::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(T64Image);

    if (size().clamp_write(at, src.size()) != src.size()) {
        return std::unexpected(at.refusal(Errc::io, ERR_SITE()));
    }
    std::ranges::copy(src, infra::affine::from(std::span<std::byte>{image_}, at).begin());
    return infra::ByteCount{src.size()};
}

}
