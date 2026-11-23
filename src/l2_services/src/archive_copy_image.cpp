// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/archive_copy_image.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "infra/span_cut.h"

namespace mister::svc {
namespace {

using infra::affine::first_clamped;
using infra::affine::from;

constexpr proto::FileSize kBlockBytes{512};

constexpr proto::FileSize kChunkBytes{64 * 1024};

}

Ex<std::unique_ptr<IDiskImage>> ArchiveCopyImage::open(
    std::unique_ptr<IFile> member,
    proto::FileSize declared
) {
    TASTY_SEAT_BODY(ArchiveCopyImage);
    std::vector<std::byte> bytes;

    for (proto::FileSize got{}; got < declared;) {
        const std::size_t old = bytes.size();
        bytes.resize(old + declared.after_header(got).min(kChunkBytes).file_word<std::size_t>());
        const std::span<std::byte> window = std::span{bytes}.subspan(old);
        auto n = member->read_at(proto::kFileStart + got, window);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), got.detail()});
        bytes.resize(old + n->first_in(window).size());
        got += proto::FileSize::of(*n);
    }
    return std::unique_ptr<IDiskImage>{new ArchiveCopyImage(std::move(bytes))};
}

FileSize ArchiveCopyImage::size() const noexcept {
    TASTY_SEAT_BODY(ArchiveCopyImage);
    return FileSize::of(std::span{bytes_});
}

bool ArchiveCopyImage::writable() const noexcept {
    TASTY_SEAT_BODY(ArchiveCopyImage);
    return false;
}

Ex<infra::ByteCount> ArchiveCopyImage::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(ArchiveCopyImage);
    if (!size().holds(at))
        return infra::ByteCount{};
    const std::span<const std::byte> rest = from(std::span<const std::byte>{bytes_}, at);
    const std::span<const std::byte> served = rest.first(std::min(rest.size(), dst.size()));
    std::ranges::copy(served, dst.begin());
    return infra::ByteCount{served.size()};
}

Ex<infra::ByteCount> ArchiveCopyImage::write_at(
    proto::FileOffset at,
    std::span<const std::byte> src
) {
    TASTY_SEAT_BODY(ArchiveCopyImage);
    for (std::span<const std::byte> rest = src; !rest.empty();) {
        const std::span<const std::byte> act = first_clamped(rest, kBlockBytes);
        if (size().clamp_write(at, act.size()) == act.size())
            std::ranges::copy(act, from(std::span<std::byte>{bytes_}, at).begin());
        at += proto::FileSize::of(act);
        rest = rest.subspan(act.size());
    }
    return infra::ByteCount{src.size()};
}

}
